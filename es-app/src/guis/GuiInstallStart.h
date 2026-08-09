#pragma once

#include "GuiComponent.h"
#include "components/MenuComponent.h"
#include "components/OptionListComponent.h"
#include "components/SwitchComponent.h"
#include "ApiSystem.h"

class GuiInstallStart : public GuiComponent
{
public:
	GuiInstallStart(Window* window);
	bool input(InputConfig* config, Input input) override;
	std::vector<HelpPrompt> getHelpPrompts() override;

private:
	void start();

	void evaluatePartitionLayout(std::string disk, ApiSystem::DiskInfo info);
	void confirmDangerousFormat(std::string disk, ApiSystem::DiskInfo info);
	void promptAndroidSizing(std::string disk, ApiSystem::DiskInfo info);
	void promptMigrationOptions(std::string disk, std::string installMode, int androidSizeGb, ApiSystem::DiskInfo info);

	MenuComponent mMenu;
	std::shared_ptr<OptionListComponent<std::string>> moptionsSource;
	std::shared_ptr<OptionListComponent<std::string>> moptionsStorage;
	std::shared_ptr<OptionListComponent<std::string>> moptionsArchitecture;
	std::shared_ptr<SwitchComponent> moptionsValidation;
};
