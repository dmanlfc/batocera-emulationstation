#include "Renderer_GLES30.h"

#ifdef RENDERER_GLES_30

#include "Renderer_GLES30.h"
#include <GLES3/gl3.h>
#include "renderers/Renderer.h"
#include "math/Transform4x4f.h"
#include "utils/StringUtil.h"
#include "utils/FileSystemUtil.h"
#include "Log.h"
#include "Settings.h"

#include <vector>
#include <unordered_map>
#include <set>
#include <fstream>

#include "Shader.h"
#include "resources/ResourceManager.h"

namespace Renderer
{

	struct TextureInfo
	{
		GLenum type;
		Vector2f size;
	};

	static SDL_GLContext	sdlContext       = nullptr;

	static Transform4x4f	projectionMatrix = Transform4x4f::Identity();
	static Transform4x4f	worldViewMatrix  = Transform4x4f::Identity();
	static Transform4x4f	mvpMatrix		 = Transform4x4f::Identity();

	static ShaderProgram    shaderProgramColorTexture;
	static ShaderProgram    shaderProgramColorNoTexture;
	static ShaderProgram    shaderProgramAlpha;

	static GLuint			vertexBuffer     = 0;
	static GLuint			vertexArrayObject = 0;

	static Rect cachedViewport = {0, 0, 0, 0};
	static Rect cachedScissor = {0, 0, 0, 0};

	static std::unordered_map<unsigned int, std::unique_ptr<TextureInfo>> _textures;

	static unsigned int		boundTexture = 0;

	extern std::string SHADER_VERSION_STRING;

//////////////////////////////////////////////////////////////////////////

	static ShaderProgram* currentProgram = nullptr;
	
	static void useProgram(ShaderProgram* program)
	{
		if (program == currentProgram)
		{
			if (currentProgram != nullptr)
				currentProgram->setMatrix(mvpMatrix);

			return;
		}
		
		if (program == nullptr && currentProgram != nullptr)
			currentProgram->unSelect();

		currentProgram = program;
		
		if (currentProgram != nullptr)
		{
			currentProgram->select();
			currentProgram->setMatrix(mvpMatrix);
		}
	}

	static std::map<std::string, ShaderProgram*> _customShaders;

	static ShaderProgram* getShaderProgram(const char* shaderFile)
	{
		if (shaderFile == nullptr || strlen(shaderFile) == 0)
			return nullptr;

		auto it = _customShaders.find(shaderFile);
		if (it != _customShaders.cend())
			return it->second;

		ShaderProgram* customShader = new ShaderProgram();
		if (!customShader->loadFromFile(shaderFile))
		{
			delete customShader;
			customShader = nullptr;
		}

		_customShaders[shaderFile] = customShader;

		return customShader;	
	}


	class ShaderBatch : public std::vector<ShaderProgram*>
	{
	public:
		static ShaderBatch* getShaderBatch(const char* shaderFile);

		std::map<std::string, std::string> parameters;
	};

	static std::map<std::string, ShaderBatch*> _customShaderBatch;

	ShaderBatch* ShaderBatch::getShaderBatch(const char* shaderFile)
	{
		if (shaderFile == nullptr)
			return nullptr;

		auto it = _customShaderBatch.find(shaderFile);
		if (it != _customShaderBatch.cend())
			return it->second;

		std::string fullPath = ResourceManager::getInstance()->getResourcePath(shaderFile);

		ShaderBatch* ret = new ShaderBatch();

		std::string ext = Utils::String::toLower(Utils::FileSystem::getExtension(fullPath));
		if (ext == ".glslp")
		{
			std::string path = Utils::FileSystem::getParent(fullPath);

			std::map<std::string, std::string> confMap;

			std::string line;
			std::ifstream systemConf(fullPath);
			if (systemConf && systemConf.is_open())
			{
				while (std::getline(systemConf, line))
				{
					int idx = line.find("=");
					if (idx == std::string::npos || line.find("#") == 0 || line.find(";") == 0)
						continue;

					std::string key = Utils::String::trim(line.substr(0, idx));
					std::string value = Utils::String::trim(Utils::String::replace(line.substr(idx + 1), "\"", ""));
					if (!key.empty() && !value.empty())
						confMap[key] = value;

				}
				systemConf.close();
			}

			int count = 0;

			auto it = confMap.find("shaders");
			if (it != confMap.cend())
				count = Utils::String::toInteger(it->second);

			for (int i = 0; i < count; i++)
			{
				auto name = "shader" + std::to_string(i);

				it = confMap.find(name);
				if (it == confMap.cend())
					continue;

				std::string relative = it->second;
				if (!Utils::String::startsWith(relative, ":") && !Utils::String::startsWith(relative, "/") && !Utils::String::startsWith(relative, "."))
					relative = "./" + relative;

				std::string full = Utils::FileSystem::resolveRelativePath(relative, path, true);

				ShaderProgram* customShader = getShaderProgram(full.c_str());
				if (customShader != nullptr)
					ret->push_back(customShader);
			}

			it = confMap.find("parameters");
			if (it != confMap.cend())
			{
				for (auto prm : Utils::String::split(it->second, ';', true))
				{
					it = confMap.find(prm);
					if (it != confMap.cend())
						ret->parameters[prm] = it->second;
				}
			}
		}
		else
		{
			ShaderProgram* customShader = getShaderProgram(fullPath.c_str());
			if (customShader != nullptr)
				ret->push_back(customShader);
		}

		_customShaderBatch[shaderFile] = ret;
		return ret;
	};

	static int getAvailableVideoMemory();

    static void setupDefaultShaders()
    {
        SHADER_VERSION_STRING = "#version 300 es\n";

        LOG(LogInfo) << "GLSL version preprocessor :     " << SHADER_VERSION_STRING;

        // vertex shader (no texture)
        std::string vertexSourceNoTexture =
            SHADER_VERSION_STRING +
            R"=====(
            uniform   mat4 MVPMatrix;
            in vec2 VertexCoord;
            in vec4 COLOR;
            out vec4 v_col;
            void main(void)
            {
                gl_Position = MVPMatrix * vec4(VertexCoord.xy, 0.0, 1.0);
                v_col = COLOR;
            }
            )=====";

        // fragment shader (no texture)
        std::string fragmentSourceNoTexture =
            SHADER_VERSION_STRING +
            R"=====(
            precision mediump float;
            in vec4 v_col;
            out vec4 fragColor;

            void main(void)          
            {                        
                fragColor = v_col;
            }                        
            )=====";

        // Compile each shader, link them to make a full program
        auto vertexShaderNoTexture = Shader::createShader(GL_VERTEX_SHADER, vertexSourceNoTexture);
        auto fragmentShaderColorNoTexture = Shader::createShader(GL_FRAGMENT_SHADER, fragmentSourceNoTexture);

        shaderProgramColorNoTexture.createShaderProgram(vertexShaderNoTexture, fragmentShaderColorNoTexture);
        
        // vertex shader (texture)
        std::string vertexSourceTexture =
            SHADER_VERSION_STRING +
            R"=====(
            uniform   mat4 MVPMatrix;
            in vec2 VertexCoord;
            in vec2 TexCoord;
            in vec4 COLOR;
            out vec2 v_tex;
            out vec4 v_col;
            out vec2 v_pos;

            void main(void)                                    
            {                                                  
                gl_Position = MVPMatrix * vec4(VertexCoord.xy, 0.0, 1.0);
                v_tex = TexCoord;                           
                v_col = COLOR;  
                v_pos = VertexCoord;                         
            }
            )=====";

        // fragment shader (texture)
        std::string fragmentSourceTexture =
            SHADER_VERSION_STRING +
            R"=====(
            precision mediump float;
            precision mediump sampler2D;

            in vec4 v_col;
            in vec2 v_tex;
            in vec2 v_pos;
            out vec4 fragColor;

            uniform   sampler2D u_tex;
            uniform   vec2      outputSize;
            uniform   vec2      outputOffset;
            uniform   float     saturation;
            uniform   float     es_cornerRadius;

            void main(void)                                    
            {                                                  
                vec4 clr = texture(u_tex, v_tex);
        
                if (saturation != 1.0) {
                    vec3 gray = vec3(dot(clr.rgb, vec3(0.34, 0.55, 0.11)));
                    vec3 blend = mix(gray, clr.rgb, saturation);
                    clr = vec4(blend, clr.a);
                }

                if (es_cornerRadius != 0.0) {
                    vec2 pos = abs(v_pos - outputOffset);
                    vec2 middle = vec2(abs(outputSize.x), abs(outputSize.y)) / 2.0;
                    vec2 center = abs(v_pos - outputOffset - middle);
                    vec2 q = center - middle + es_cornerRadius;
                    float distance = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - es_cornerRadius;    

                    if (distance > 0.0) {
                        discard;
                    } 
                    else if (pos.x >= 1.0 && pos.y >= 1.0 && pos.x <= outputSize.x - 1.0 && pos.y <= outputSize.y - 1.0)
                    {
                        float pixelValue = 1.0 - smoothstep(-0.75, 0.5, distance);
                        clr.a *= pixelValue;						
                    }
                }
            
                fragColor = clr * v_col;
            }
            )=====";

        // Compile each shader, link them to make a full program
        auto vertexShaderTexture = Shader::createShader(GL_VERTEX_SHADER, vertexSourceTexture);
        auto fragmentShaderColorTexture = Shader::createShader(GL_FRAGMENT_SHADER, fragmentSourceTexture);
        shaderProgramColorTexture.createShaderProgram(vertexShaderTexture, fragmentShaderColorTexture);
        
        // fragment shader (alpha texture)
        std::string fragmentSourceAlpha =
            SHADER_VERSION_STRING +
            R"=====(
            precision mediump float;
            precision mediump sampler2D;

            in vec4 v_col;
            in vec2 v_tex;
            out vec4 fragColor;
            uniform   sampler2D u_tex;

            void main(void)           
            {                         
                vec4 a = vec4(1.0, 1.0, 1.0, texture(u_tex, v_tex).a);
                fragColor = a * v_col; 
            }
            )=====";


        auto vertexShaderAlpha = Shader::createShader(GL_VERTEX_SHADER, vertexSourceTexture);
        auto fragmentShaderAlpha = Shader::createShader(GL_FRAGMENT_SHADER, fragmentSourceAlpha);

        shaderProgramAlpha.createShaderProgram(vertexShaderAlpha, fragmentShaderAlpha);
        
        useProgram(nullptr);
    
	} // setupDefaultShaders

//////////////////////////////////////////////////////////////////////////

	static void setupVertexBuffer()
	{
		GL_CHECK_ERROR(glGenVertexArrays(1, &vertexArrayObject));
		GL_CHECK_ERROR(glBindVertexArray(vertexArrayObject));
		GL_CHECK_ERROR(::glGenBuffers(1, &vertexBuffer));
		GL_CHECK_ERROR(::glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer));

	} // setupVertexBuffer

//////////////////////////////////////////////////////////////////////////

	static GLenum convertBlendFactor(const Blend::Factor _blendFactor)
	{
		switch(_blendFactor)
		{
			case Blend::ZERO:                { return GL_ZERO;                } break;
			case Blend::ONE:                 { return GL_ONE;                 } break;
			case Blend::SRC_COLOR:           { return GL_SRC_COLOR;           } break;
			case Blend::ONE_MINUS_SRC_COLOR: { return GL_ONE_MINUS_SRC_COLOR; } break;
			case Blend::SRC_ALPHA:           { return GL_SRC_ALPHA;           } break;
			case Blend::ONE_MINUS_SRC_ALPHA: { return GL_ONE_MINUS_SRC_ALPHA; } break;
			case Blend::DST_COLOR:           { return GL_DST_COLOR;           } break;
			case Blend::ONE_MINUS_DST_COLOR: { return GL_ONE_MINUS_DST_COLOR; } break;
			case Blend::DST_ALPHA:           { return GL_DST_ALPHA;           } break;
			case Blend::ONE_MINUS_DST_ALPHA: { return GL_ONE_MINUS_DST_ALPHA; } break;
			default:                         { return GL_ZERO;                }
		}

	} // convertBlendFactor

//////////////////////////////////////////////////////////////////////////

	static GLenum convertTextureType(const Texture::Type _type)
	{
		switch(_type)
		{
			case Texture::RGBA:  { return GL_RGBA;            } break;
			case Texture::ALPHA: { return GL_ALPHA; } break;
			default:             { return GL_ZERO;            }
		}

	} // convertTextureType

//////////////////////////////////////////////////////////////////////////

    static int getAvailableVideoMemory()
    {
        float total = 0;

        // Define the size of the textures used for estimation (4MB each)
        float megabytes = 4.0;
        int sz = sqrtf(megabytes * 1024.0 * 1024.0 / 4.0f);

        // Vector to hold texture IDs for cleanup
        std::vector<unsigned int> textures;
        textures.reserve(1000);

        // Attempt to generate textures until failure
        while (true)
        {
            unsigned int textureId = 0;
            glGenTextures(1, &textureId);

            if (textureId == 0 || glGetError() != GL_NO_ERROR)
                break;

            textures.push_back(textureId);

            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sz, sz, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            
            if (glGetError() != GL_NO_ERROR)
                break;

            total += megabytes;
        }

        // Clear any lingering OpenGL errors
        while (glGetError() != GL_NO_ERROR)
            ;

        // Cleanup created textures
        for (auto tx : textures)
            glDeleteTextures(1, &tx);

        // Return the estimated video memory in megabytes
        return total;
    }

//////////////////////////////////////////////////////////////////////////
	GLES30Renderer::GLES30Renderer() : mFrameBuffer(-1) {}

	unsigned int GLES30Renderer::getWindowFlags()
	{
		return SDL_WINDOW_OPENGL;

	} // getWindowFlags

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::setupWindow()
	{
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);

		SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);

		SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,       1);
		SDL_GL_SetAttribute(SDL_GL_RED_SIZE,           8);
		SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,         8);
		SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,          8);
		SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,        24);
		SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,       1);
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

	} // setupWindow

//////////////////////////////////////////////////////////////////////////
	std::string GLES30Renderer::getDriverName()
	{
		return "OPENGL ES 3.0";
	}

	std::vector<std::pair<std::string, std::string>> GLES30Renderer::getDriverInformation()
	{
		std::vector<std::pair<std::string, std::string>> info;

		info.push_back(std::pair<std::string, std::string>("GRAPHICS API", getDriverName()));

		const std::string vendor = glGetString(GL_VENDOR) ? (const char*)glGetString(GL_VENDOR) : "";
		if (!vendor.empty())
			info.push_back(std::pair<std::string, std::string>("VENDOR", vendor));

		const std::string renderer = glGetString(GL_RENDERER) ? (const char*)glGetString(GL_RENDERER) : "";
		if (!renderer.empty())
			info.push_back(std::pair<std::string, std::string>("RENDERER", renderer));

		const std::string version = glGetString(GL_VERSION) ? (const char*)glGetString(GL_VERSION) : "";
		if (!version.empty())
			info.push_back(std::pair<std::string, std::string>("VERSION", version));

		const std::string shaders = glGetString(GL_SHADING_LANGUAGE_VERSION) ? (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) : "";
		if (!shaders.empty())
			info.push_back(std::pair<std::string, std::string>("SHADERS", shaders));

		return info;
	}

	void GLES30Renderer::createContext()
	{
		sdlContext = SDL_GL_CreateContext(getSDLWindow());
		
		SDL_GL_MakeCurrent(getSDLWindow(), sdlContext);

		const std::string vendor     = glGetString(GL_VENDOR)     ? (const char*)glGetString(GL_VENDOR)     : "";
		const std::string renderer   = glGetString(GL_RENDERER)   ? (const char*)glGetString(GL_RENDERER)   : "";
		const std::string version    = glGetString(GL_VERSION)    ? (const char*)glGetString(GL_VERSION)    : "";
		const std::string extensions = glGetString(GL_EXTENSIONS) ? (const char*)glGetString(GL_EXTENSIONS) : "";
		const std::string shaders    = glGetString(GL_SHADING_LANGUAGE_VERSION) ? (const char*)glGetString(GL_SHADING_LANGUAGE_VERSION) : "";
		
		LOG(LogInfo) << "GL vendor:   " << vendor;
		LOG(LogInfo) << "GL renderer: " << renderer;
		LOG(LogInfo) << "GL version:  " << version;
		LOG(LogInfo) << "GL shading:  " << shaders;
		LOG(LogInfo) << "GL exts:     " << extensions;

		GLint maxTextureSize = 0;
		glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
		LOG(LogInfo) << " GL_MAX_TEXTURE_SIZE: " << maxTextureSize;

		setupDefaultShaders();
		setupVertexBuffer();

		GL_CHECK_ERROR(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));

		GL_CHECK_ERROR(glActiveTexture(GL_TEXTURE0));

		GL_CHECK_ERROR(glPixelStorei(GL_PACK_ALIGNMENT, 1));
		GL_CHECK_ERROR(glPixelStorei(GL_UNPACK_ALIGNMENT, 1));
		
	} // createContext

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::resetCache()
	{
		bindTexture(0);

		for (auto customShader : _customShaderBatch)
		{
			if (customShader.second != nullptr)
			{
				customShader.second->clear();
				delete customShader.second;
			}
		}

		_customShaderBatch.clear();

		for (auto customShader : _customShaders)
		{
			if (customShader.second != nullptr)
			{
				customShader.second->deleteProgram();
				delete customShader.second;
			}
		}

		_customShaders.clear();

		if (mFrameBuffer != -1)
		{
			GL_CHECK_ERROR(::glDeleteFramebuffers(1, &mFrameBuffer));
			mFrameBuffer = -1;
		}
	}

	void GLES30Renderer::destroyContext()
	{
		resetCache();

		SDL_GL_DeleteContext(sdlContext);
		sdlContext = nullptr;

	} // destroyContext

//////////////////////////////////////////////////////////////////////////

	unsigned int GLES30Renderer::createTexture(const Texture::Type _type, const bool _linear, const bool _repeat, const unsigned int _width, const unsigned int _height, void* _data)
	{
		const GLenum type = convertTextureType(_type);

		unsigned int texture = 0;  // It's safer to initialize texture to 0, as OpenGL requires non-zero texture IDs
		GL_CHECK_ERROR(glGenTextures(1, &texture));

		if (texture == 0)
		{
			LOG(LogError) << "CreateTexture error: glGenTextures failed ";
			return 0;
		}

		bindTexture(0);
		bindTexture(texture);

		// Texture wrapping
		GL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, _repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE));
		GL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, _repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE));

		// Texture filters
		GL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
		GL_CHECK_ERROR(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, _linear ? GL_LINEAR : GL_NEAREST));

		// Handle LUMINANCE_ALPHA textures
		glTexImage2D(GL_TEXTURE_2D, 0, type, _width, _height, 0, type, GL_UNSIGNED_BYTE, _data);
		GLenum err = glGetError();
		if (err != GL_NO_ERROR)
		{
			LOG(LogError) << "CreateTexture error: glTexImage2D failed, OpenGL error: " << err;
			destroyTexture(texture);
			return 0;
		}

		// Store texture information
		if (texture != 0)
		{
			auto it = _textures.find(texture);
			if (it != _textures.cend())
			{
				it->second->type = type;
				it->second->size = Vector2f(_width, _height);
			}
			else
			{
				auto info = std::make_unique<TextureInfo>();  // Use std::make_unique for creating a unique pointer
				info->type = type;
				info->size = Vector2f(_width, _height);
				_textures[texture] = std::move(info);  // Move the unique_ptr into the map
			}
		}

		return texture;
	} // createTexture

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::destroyTexture(const unsigned int _texture)
	{
		auto it = _textures.find(_texture);
		if (it != _textures.cend())
		{
			// No need to delete unique_ptr, it will be automatically cleaned up when removed from the map
			_textures.erase(it);  // Simply erase the texture info from the map
		}

		GL_CHECK_ERROR(glDeleteTextures(1, &_texture));
	} // destroyTexture

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::updateTexture(const unsigned int _texture, const Texture::Type _type, const unsigned int _x, const unsigned int _y, const unsigned int _width, const unsigned int _height, void* _data)
	{
		const GLenum type = convertTextureType(_type);

		bindTexture(_texture);

		// Regular GL_ALPHA textures are black + alpha in shaders
		// Create a GL_LUMINANCE_ALPHA texture instead so its white + alpha
		if (type == GL_LUMINANCE_ALPHA)
		{
			uint8_t* a_data  = (uint8_t*)_data;
			uint8_t* la_data = new uint8_t[_width * _height * 2];
			memset(la_data, 255, _width * _height * 2);
			if (a_data)
			{
				for(uint32_t i=0; i<(_width * _height); ++i)
					la_data[(i * 2) + 1] = a_data[i];                
			}

			GL_CHECK_ERROR(glTexSubImage2D(GL_TEXTURE_2D, 0, _x, _y, _width, _height, type, GL_UNSIGNED_BYTE, la_data));
			delete[] la_data;
		}
		else
			GL_CHECK_ERROR(glTexSubImage2D(GL_TEXTURE_2D, 0, _x, _y, _width, _height, type, GL_UNSIGNED_BYTE, _data));

		if (_texture != 0)
		{
			auto it = _textures.find(_texture);
			if (it != _textures.cend())
			{
				it->second->type = type;
				it->second->size = Vector2f(_width, _height);
			}
			else
			{
				auto info = std::make_unique<TextureInfo>();  // Use std::make_unique for creating a unique pointer
				info->type = type;
				info->size = Vector2f(_width, _height);
				_textures[_texture] = std::move(info);  // Move the unique_ptr into the map
			}
		}

		bindTexture(0);
	} // updateTexture

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::bindTexture(const unsigned int _texture)
	{
		if (boundTexture == _texture)
			return;

		boundTexture = _texture;

		if(_texture == 0)
		{
			GL_CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, 0));
			boundTexture = 0;
		}
		else
		{
			GL_CHECK_ERROR(glBindTexture(GL_TEXTURE_2D, _texture));
			boundTexture = _texture;
		}

	} // bindTexture

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::drawLines(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
	{
		GL_CHECK_ERROR(::glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * _numVertices, _vertices, GL_DYNAMIC_DRAW));

		useProgram(&shaderProgramColorNoTexture);

		const bool enableBlend = (_srcBlendFactor != Blend::ONE && _dstBlendFactor != Blend::ONE);

		GL_CHECK_ERROR(glBlendFunc(convertBlendFactor(_srcBlendFactor), convertBlendFactor(_dstBlendFactor)));
		if (enableBlend) GL_CHECK_ERROR(glEnable(GL_BLEND));

		GL_CHECK_ERROR(glDrawArrays(GL_LINES, 0, _numVertices));

		if (enableBlend) GL_CHECK_ERROR(glDisable(GL_BLEND));
	
	} // drawLines

//////////////////////////////////////////////////////////////////////////
	void GLES30Renderer::drawSolidRectangle(const float _x, const float _y, const float _w, const float _h, const unsigned int _fillColor, const unsigned int _borderColor, float borderWidth, float cornerRadius)
	{
		if (cornerRadius == 0.0f)
		{
			if (_fillColor != 0)
				drawRect(_x + borderWidth, _y + borderWidth, _w - borderWidth - borderWidth, _h - borderWidth - borderWidth, _fillColor);

			if (_borderColor != 0 && borderWidth > 0)
			{
				drawRect(_x, _y, _w, borderWidth, _borderColor);
				drawRect(_x + _w - borderWidth, _y + borderWidth, borderWidth, _h - borderWidth, _borderColor);
				drawRect(_x, _y + _h - borderWidth, _w - borderWidth, borderWidth, _borderColor);
				drawRect(_x, _y + borderWidth, borderWidth, _h - borderWidth - borderWidth, _borderColor);
			}
			return;
		}

		bindTexture(0);
		useProgram(&shaderProgramColorNoTexture);

		GL_CHECK_ERROR(glEnable(GL_BLEND));
		GL_CHECK_ERROR(glBlendFunc(convertBlendFactor(Blend::SRC_ALPHA), convertBlendFactor(Blend::ONE_MINUS_SRC_ALPHA)));

		auto inner = createRoundRect(_x + borderWidth, _y + borderWidth, _w - borderWidth - borderWidth, _h - borderWidth - borderWidth, cornerRadius, _fillColor);

		if ((_fillColor) & 0xFF)
		{
			GL_CHECK_ERROR(::glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * inner.size(), inner.data(), GL_DYNAMIC_DRAW));
			GL_CHECK_ERROR(glDrawArrays(GL_TRIANGLE_FAN, 0, inner.size()));
		}

		if ((_borderColor) & 0xFF && borderWidth > 0)
		{
			auto outer = createRoundRect(_x, _y, _w, _h, cornerRadius, _borderColor);

			setStencil(inner.data(), inner.size());
			GL_CHECK_ERROR(glStencilFunc(GL_NOTEQUAL, 1, ~0));

			GL_CHECK_ERROR(glEnable(GL_BLEND));
			GL_CHECK_ERROR(glBlendFunc(convertBlendFactor(Blend::SRC_ALPHA), convertBlendFactor(Blend::ONE_MINUS_SRC_ALPHA)));

			GL_CHECK_ERROR(::glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * outer.size(), outer.data(), GL_DYNAMIC_DRAW));
			GL_CHECK_ERROR(glDrawArrays(GL_TRIANGLE_FAN, 0, outer.size()));
			
			disableStencil();
		}

		GL_CHECK_ERROR(glDisable(GL_BLEND));
	}

	void GLES30Renderer::drawTriangleStrips(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor, bool verticesChanged)
	{
		if (verticesChanged)
			GL_CHECK_ERROR(::glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * _numVertices, _vertices, GL_STREAM_DRAW));

		// Setup shader
		if (boundTexture != 0)
		{
			auto it = _textures.find(boundTexture);
			if (it != _textures.cend() && it->second != nullptr && it->second->type == GL_ALPHA)
				useProgram(&shaderProgramAlpha);
			else
			{
				ShaderProgram* shader = &shaderProgramColorTexture;

				if (_vertices->customShader != nullptr && !_vertices->customShader->path.empty())
				{
					ShaderProgram* customShader = getShaderProgram(_vertices->customShader->path.c_str());
					if (customShader != nullptr)
						shader = customShader;
				}

				useProgram(shader);

				// Update Shader Uniforms				
				shader->setSaturation(_vertices->saturation);
				shader->setCornerRadius(_vertices->cornerRadius);
				shader->setResolution();
				shader->setFrameCount(Renderer::getCurrentFrame());

				if (shader->supportsTextureSize() && it != _textures.cend() && it->second != nullptr)
				{
					shader->setInputSize(it->second->size);
					shader->setTextureSize(it->second->size);
				}
				
				if (_numVertices > 0)
				{
					Vector2f vec = _vertices[_numVertices - 1].pos;
					if (_numVertices == 4)
					{
						vec.x() -= _vertices[0].pos.x();
						vec.y() -= _vertices[0].pos.y();
					}

					// Inverted rendering
					if (_vertices[_numVertices - 1].tex.y() == 1 && _vertices[0].tex.y() == 0)
						vec.y() = -vec.y();

					shader->setOutputSize(vec);						
					shader->setOutputOffset(_vertices[0].pos);
				}

				if (_vertices->customShader != nullptr && !_vertices->customShader->path.empty())
					shader->setCustomUniformsParameters(_vertices->customShader->parameters);
			}
		}
		else
			useProgram(&shaderProgramColorNoTexture);

		// Do rendering
		if (_srcBlendFactor != Blend::ONE && _dstBlendFactor != Blend::ONE)
		{
			GL_CHECK_ERROR(glEnable(GL_BLEND));
			GL_CHECK_ERROR(glBlendFunc(convertBlendFactor(_srcBlendFactor), convertBlendFactor(_dstBlendFactor)));
			GL_CHECK_ERROR(glDrawArrays(GL_TRIANGLE_STRIP, 0, _numVertices));
			GL_CHECK_ERROR(glDisable(GL_BLEND));
		}
		else
		{
			GL_CHECK_ERROR(glDisable(GL_BLEND));
			GL_CHECK_ERROR(glDrawArrays(GL_TRIANGLE_STRIP, 0, _numVertices));
		}

	} // drawTriangleStrips

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::setProjection(const Transform4x4f& _projection)
	{
		projectionMatrix = _projection;
		mvpMatrix = projectionMatrix * worldViewMatrix;
	} // setProjection

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::setMatrix(const Transform4x4f& _matrix)
	{
		worldViewMatrix = _matrix;
		mvpMatrix = projectionMatrix * worldViewMatrix;
	} // setMatrix

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::setViewport(const Rect& _viewport)
	{
		// glViewport starts at the bottom left of the window
		GL_CHECK_ERROR(glViewport( _viewport.x, getWindowHeight() - _viewport.y - _viewport.h, _viewport.w, _viewport.h));

	} // setViewport

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::setScissor(const Rect& _scissor)
	{
		if((_scissor.x == 0) && (_scissor.y == 0) && (_scissor.w == 0) && (_scissor.h == 0))
		{
			GL_CHECK_ERROR(glDisable(GL_SCISSOR_TEST));
		}
		else
		{
			// glScissor starts at the bottom left of the window
			GL_CHECK_ERROR(glScissor(_scissor.x, getWindowHeight() - _scissor.y - _scissor.h, _scissor.w, _scissor.h));
			GL_CHECK_ERROR(glEnable(GL_SCISSOR_TEST));
		}

	} // setScissor

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::setSwapInterval()
	{
		// vsync
		if(Settings::getInstance()->getBool("VSync"))
		{
			// SDL_GL_SetSwapInterval(0) for immediate updates (no vsync, default),
			// 1 for updates synchronized with the vertical retrace,
			// or -1 for late swap tearing.
			// SDL_GL_SetSwapInterval returns 0 on success, -1 on error.
			// if vsync is requested, try normal vsync; if that doesn't work, try late swap tearing
			// if that doesn't work, report an error
			if (SDL_GL_SetSwapInterval(1) != 0 && SDL_GL_SetSwapInterval(-1) != 0)
				LOG(LogWarning) << "Tried to enable vsync, but failed! (" << SDL_GetError() << ")";
		}
		else
			SDL_GL_SetSwapInterval(0);

	} // setSwapInterval

//////////////////////////////////////////////////////////////////////////

	void GLES30Renderer::swapBuffers()
	{
		useProgram(nullptr);

#ifdef WIN32		
		glFlush();
		Sleep(0);
#endif
		SDL_GL_SwapWindow(getSDLWindow());
		GL_CHECK_ERROR(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
	} // swapBuffers

//////////////////////////////////////////////////////////////////////////
	
    void GLES30Renderer::drawTriangleFan(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor)
    {
        // Pass buffer data
        GL_CHECK_ERROR(::glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * _numVertices, _vertices, GL_DYNAMIC_DRAW));

        // Setup shader
        if (boundTexture != 0)
        {
            auto it = _textures.find(boundTexture);
            if (it != _textures.cend() && it->second != nullptr && it->second->type == GL_ALPHA)
                useProgram(&shaderProgramAlpha);
            else
            {
                useProgram(&shaderProgramColorTexture);
                shaderProgramColorTexture.setSaturation(_vertices->saturation);
                shaderProgramColorTexture.setCornerRadius(0.0f);
            }
        }
        else
        {
            useProgram(&shaderProgramColorNoTexture);
        }

        // Blend settings and rendering
        if (_srcBlendFactor != Blend::ONE && _dstBlendFactor != Blend::ONE)
        {
            GL_CHECK_ERROR(glEnable(GL_BLEND));
            GL_CHECK_ERROR(glBlendFunc(convertBlendFactor(_srcBlendFactor), convertBlendFactor(_dstBlendFactor)));
        }
        else
        {
            GL_CHECK_ERROR(glDisable(GL_BLEND));
        }

        GL_CHECK_ERROR(glDrawArrays(GL_TRIANGLE_FAN, 0, _numVertices));
    }

	void GLES30Renderer::setStencil(const Vertex* _vertices, const unsigned int _numVertices)
	{
		useProgram(&shaderProgramColorNoTexture);

		glEnable(GL_STENCIL_TEST);

		glClearStencil(0);
		glClear(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);

		glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
		glDepthMask(GL_FALSE);

		glStencilFunc(GL_ALWAYS, 1, ~0);
		glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

		glEnable(GL_BLEND);
		glBlendFunc(convertBlendFactor(Blend::SRC_ALPHA), convertBlendFactor(Blend::ONE_MINUS_SRC_ALPHA));
		glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * _numVertices, _vertices, GL_DYNAMIC_DRAW);
		glDrawArrays(GL_TRIANGLE_FAN, 0, _numVertices);
		glDisable(GL_BLEND);

		glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
		glDepthMask(GL_TRUE);

		glStencilFunc(GL_EQUAL, 1, ~0);
		glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
	}

	void GLES30Renderer::disableStencil()
	{
		glDisable(GL_STENCIL_TEST);
	}

	size_t GLES30Renderer::getTotalMemUsage()
	{
		size_t total = 0;

		for (const auto& tex : _textures)
		{
			if (tex.first != 0 && tex.second)
			{
				size_t size = tex.second->size.x() * tex.second->size.y() * (tex.second->type == GL_ALPHA ? 1 : 4);
				total += size;
			}
		}

		return total;
	}

	bool GLES30Renderer::shaderSupportsCornerSize(const std::string& shader)
	{
		ShaderProgram* customShader = getShaderProgram(shader.c_str());
		if (customShader == nullptr)
			customShader = &shaderProgramColorTexture;
			
		return customShader->supportsCornerRadius();
	}

	void GLES30Renderer::postProcessShader(const std::string& path, const float _x, const float _y, const float _w, const float _h, const std::map<std::string, std::string>& parameters, unsigned int* data)
	{
	}
	
} // Renderer::

#endif // RENDERER_GLES_30
