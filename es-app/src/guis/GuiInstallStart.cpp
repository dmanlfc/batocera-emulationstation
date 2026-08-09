#include "guis/GuiInstallStart.h"

#include "ApiSystem.h"
#include "components/OptionListComponent.h"
#include "guis/GuiInstall.h"
#include "views/ViewController.h"
#include "utils/StringUtil.h"
#include "LocaleES.h"
#include "guis/GuiMsgBox.h"
#include "components/SwitchComponent.h"
#include "components/SliderComponent.h"
#include "components/MenuComponent.h"

class GuiAndroidResize : public GuiComponent
{
public:
    GuiAndroidResize(Window* window, std::string disk, ApiSystem::DiskInfo info, std::function<void(int)> onComplete)
        : GuiComponent(window), mMenu(window, _("ANDROID PARTITION SIZE").c_str())
    {
        addChild(&mMenu);
        mOnComplete = onComplete;

        mSlider = std::make_shared<SliderComponent>(window, (float)info.minAndroidGb, (float)info.maxAndroidGb, 1.0f, "GB");
        mSlider->setValue((float)info.minAndroidGb);
        mMenu.addWithLabel(_("ANDROID STORAGE SIZE"), mSlider);

        mMenu.addButton(_("NEXT"), "next", [this] {
            mOnComplete((int)mSlider->getValue());
            delete this;
        });
        mMenu.addButton(_("BACK"), "back", [this] { delete this; });

        setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
        mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2, (mSize.y() - mMenu.getSize().y()) / 2);
    }

    bool input(InputConfig* config, Input input) override
    {
        if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input)) {
            delete this;
            return true;
        }
        return GuiComponent::input(config, input);
    }

    void render(const Transform4x4f& parentTrans) override
    {
        Transform4x4f trans = parentTrans * getTransform();
        renderChildren(trans);
    }

private:
    MenuComponent mMenu;
    std::shared_ptr<SliderComponent> mSlider;
    std::function<void(int)> mOnComplete;
};

class GuiDataMigration : public GuiComponent
{
public:
    GuiDataMigration(Window* window, std::string disk, std::string installMode, int androidSizeGb, ApiSystem::DiskInfo info, unsigned long long targetShareKb, std::function<void(std::string)> onComplete)
        : GuiComponent(window), mMenu(window, _("DATA MIGRATION").c_str())
    {
        addChild(&mMenu);
        mOnComplete = onComplete;

        moptionsCopy = std::make_shared<OptionListComponent<std::string>>(window, _("COPY LEVEL"), false);

        // Calculate size requirements
        if (targetShareKb >= info.totalUserdataKb) {
            moptionsCopy->add(_("All Data (ROMs, BIOS, Settings)"), "all", true);
        } else {
            moptionsCopy->add(_("All Data (Not Enough Space)"), "all_disabled", false);
        }

        if (targetShareKb >= info.essentialUserdataKb) {
            moptionsCopy->add(_("Settings & BIOS Only"), "bios", !moptionsCopy->hasSelection());
        } else {
            moptionsCopy->add(_("Settings & BIOS (Not Enough Space)"), "bios_disabled", false);
        }

        moptionsCopy->add(_("None (Clean Install)"), "none", !moptionsCopy->hasSelection());

        mMenu.addWithLabel(_("SELECT DATA TO COPY"), moptionsCopy);

        mMenu.addButton(_("INSTALL"), "install", [this] {
            std::string selection = moptionsCopy->getSelected();
            if (selection == "all_disabled" || selection == "bios_disabled") {
                mWindow->pushGui(new GuiMsgBox(mWindow, _("SELECTED OPTION CANNOT FIT ON DESTINATION.")));
                return;
            }
            mOnComplete(selection);
            delete this;
        });
        mMenu.addButton(_("BACK"), "back", [this] { delete this; });

        setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());
        mMenu.setPosition((mSize.x() - mMenu.getSize().x()) / 2, (mSize.y() - mMenu.getSize().y()) / 2);
    }

    bool input(InputConfig* config, Input input) override
    {
        if (input.value != 0 && config->isMappedTo(BUTTON_BACK, input)) {
            delete this;
            return true;
        }
        return GuiComponent::input(config, input);
    }

    void render(const Transform4x4f& parentTrans) override
    {
        Transform4x4f trans = parentTrans * getTransform();
        renderChildren(trans);
    }

private:
    MenuComponent mMenu;
    std::shared_ptr<OptionListComponent<std::string>> moptionsCopy;
    std::function<void(std::string)> mOnComplete;
};

GuiInstallStart::GuiInstallStart(Window* window) : GuiComponent(window),
mMenu(window, _("INSTALL ON A NEW DISK").c_str())
{
	addChild(&mMenu);

	std::vector<std::string> availableStorage = ApiSystem::getInstance()->getAvailableInstallDevices();
	std::vector<std::string> availableArchitecture = ApiSystem::getInstance()->getAvailableInstallArchitectures();
	std::string runningBoard = ApiSystem::getInstance()->getRunningBoard();

	bool installationPossible = (availableStorage.size() != 0);

	if (installationPossible) 
	{
		// Install Source Option
		moptionsSource = std::make_shared<OptionListComponent<std::string>>(window, _("INSTALL SOURCE"), false);
		// Default to local copy if the network list is empty
		moptionsSource->add(_("DOWNLOAD FROM INTERNET"), "online", availableArchitecture.size() != 0);
		moptionsSource->add(_("COPY LOCAL SYSTEM"), "local", availableArchitecture.size() == 0);
		mMenu.addWithLabel(_("INSTALL SOURCE"), moptionsSource);

		// Target Storage Device
		moptionsStorage = std::make_shared<OptionListComponent<std::string>>(window, _("TARGET DEVICE"), false);
		moptionsStorage->add(_("SELECT"), "", true);

		for (auto it = availableStorage.begin(); it != availableStorage.end(); it++) 
		{
			std::vector<std::string> tokens = Utils::String::split(*it, ' ');
			if (tokens.size() >= 2) {
				std::string vname = "";
				for (unsigned int i = 1; i < tokens.size(); i++) {
					if (i > 1) vname += " ";
					vname += tokens.at(i);
				}
				moptionsStorage->add(vname, tokens.at(0), false);
			}
		}
		mMenu.addWithLabel(_("TARGET DEVICE"), moptionsStorage);
	
		// Target Architecture Option
		moptionsArchitecture = std::make_shared<OptionListComponent<std::string>>(window, _("TARGET ARCHITECTURE"), false);
		
		if (availableArchitecture.size() > 0)
		{
			for (auto it = availableArchitecture.begin(); it != availableArchitecture.end(); it++)
				moptionsArchitecture->add(*it, *it, *it == runningBoard);
			if (!(moptionsArchitecture->hasSelection()))
				moptionsArchitecture->selectFirstItem();
		}
		else
		{
			moptionsArchitecture->add(_("NETWORK REQUIRED"), "", true);
		}

		mMenu.addWithLabel(_("TARGET ARCHITECTURE"), moptionsArchitecture);

		// Confirmation Switch
		moptionsValidation = std::make_shared<SwitchComponent>(mWindow);
		mMenu.addWithLabel(_("ARE YOU SURE?"), moptionsValidation);
		
		mMenu.addButton(_("INSTALL"), "install", std::bind(&GuiInstallStart::start, this));
		mMenu.addButton(_("BACK"), "back", [&] { delete this; });
	}
	else
		mMenu.addButton(_("NO TARGET DRIVES FOUND"), "back", [&] { delete this; });	

	if (Renderer::ScreenSettings::fullScreenMenus())
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, (Renderer::getScreenHeight() - mMenu.getSize().y()) / 2);
	else
		mMenu.setPosition((Renderer::getScreenWidth() - mMenu.getSize().x()) / 2, Renderer::getScreenHeight() * 0.1f);
}

void GuiInstallStart::start()
{
	if (!moptionsStorage->hasSelection() || moptionsStorage->getSelected() == "" || !moptionsValidation->getState())
	{
		mWindow->pushGui(new GuiMsgBox(mWindow, _("INVALID PARAMETERS")));
		return;
	}

	std::string selectedSource = moptionsSource->getSelected();
	std::string targetDevice = moptionsStorage->getSelected();

	if (selectedSource == "online")
	{
		if (!moptionsArchitecture->hasSelection() || moptionsArchitecture->getSelected() == "") {
			mWindow->pushGui(new GuiMsgBox(mWindow, _("NETWORK REQURED FOR ONLINE DOWNLOADS. PLEASE CHECK YOUR CONNECTION OR CHOOSE 'COPY LOCAL SYSTEM'.")));
			return;
		}
		mWindow->pushGui(new GuiInstall(mWindow, targetDevice, moptionsArchitecture->getSelected()));
		delete this;
	}
	else if (selectedSource == "local")
	{
		// Intercept and run dynamic layout analysis
		ApiSystem::DiskInfo info = ApiSystem::getInstance()->getDiskInfo(targetDevice);

		// Step A: Battery check warning for all portable platforms
		if (!info.powerConnected)
		{
			mWindow->pushGui(new GuiMsgBox(mWindow,
				_("WARNING: YOUR DEVICE IS NOT ON CHARGER POWER. RUNNING PARTITION MODIFICATIONS ON BATTERY RUNS THE RISK OF DATA CORRUPTION. DO YOU WANT TO PROCEED?"),
				_("YES"), [this, targetDevice, info] {
					this->evaluatePartitionLayout(targetDevice, info);
				},
				_("NO"), nullptr));
		}
		else
		{
			evaluatePartitionLayout(targetDevice, info);
		}
	}
}

void GuiInstallStart::evaluatePartitionLayout(std::string disk, ApiSystem::DiskInfo info)
{
	// Target Android partition preservation warning
	if (info.type == "android_resize")
	{
		mWindow->pushGui(new GuiMsgBox(mWindow,
			_("AN EXISTING ANDROID INSTALLATION WAS DETECTED. COMPLETELY WIPING THE DRIVE MAY REMOVE VITAL BOOTLOADERS OR CHIPSET FIRMWARE. IT IS STRONGLY RECOMMENDED TO KEEP AND RESIZE ANDROID."),
			_("RESIZE ANDROID"), [this, disk, info] {
				this->promptAndroidSizing(disk, info);
			},
			_("FORMAT ENTIRE DISK"), [this, disk, info] {
				this->confirmDangerousFormat(disk, info);
			}));
	}
	else
	{
		promptMigrationOptions(disk, "format", 0, info);
	}
}

void GuiInstallStart::confirmDangerousFormat(std::string disk, ApiSystem::DiskInfo info)
{
	mWindow->pushGui(new GuiMsgBox(mWindow,
		_("ARE YOU ABSOLUTELY SURE? DESTROYING ALL PARTITIONS ON AN ARM/ANDROID-BASED PLATFORM CAN BRICK YOUR DEVICE IF IT REQUIRES THE ORIGINAL SYSTEM PATHS TO START."),
		_("YES, FORMAT EVERYTHING"), [this, disk, info] {
			this->promptMigrationOptions(disk, "format", 0, info);
		},
		_("ABORT"), nullptr));
}

void GuiInstallStart::promptAndroidSizing(std::string disk, ApiSystem::DiskInfo info)
{
	mWindow->pushGui(new GuiAndroidResize(mWindow, disk, info, [this, disk, info](int androidSizeGb) {
		this->promptMigrationOptions(disk, "resize", androidSizeGb, info);
	}));
}

void GuiInstallStart::promptMigrationOptions(std::string disk, std::string installMode, int androidSizeGb, ApiSystem::DiskInfo info)
{
	// Compute capacity constraints on destination
	unsigned long long bootSizeKb = info.sourceBootSizeMib * 1024;
	unsigned long long androidSizeKb = (unsigned long long)androidSizeGb * 1024 * 1024;
	
	unsigned long long targetShareKb = 0;
	if (installMode == "resize") {
		targetShareKb = info.targetDiskSizeKb - bootSizeKb - androidSizeKb;
	} else {
		targetShareKb = info.targetDiskSizeKb - bootSizeKb;
	}

	mWindow->pushGui(new GuiDataMigration(mWindow, disk, installMode, androidSizeGb, info, targetShareKb, [this, disk, installMode, androidSizeGb](std::string copyLevel) {
		mWindow->pushGui(new GuiInstall(mWindow, disk, "local", installMode, androidSizeGb, copyLevel));
		delete this;
	}));
}

bool GuiInstallStart::input(InputConfig* config, Input input)
{
	bool consumed = GuiComponent::input(config, input);
	if(consumed)
		return true;
	
	if(input.value != 0 && config->isMappedTo(BUTTON_BACK, input))
	{
		delete this;
		return true;
	}

	if(config->isMappedTo("start", input) && input.value != 0)
	{
		// close everything
		Window* window = mWindow;
		while(window->peekGui() && window->peekGui() != ViewController::get())
			delete window->peekGui();
	}


	return false;
}

std::vector<HelpPrompt> GuiInstallStart::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts = mMenu.getHelpPrompts();
	prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK")));
	prompts.push_back(HelpPrompt("start", _("CLOSE")));
	return prompts;
}
