#include "Renderer_GLES30.h"

#ifdef RENDERER_GLES_30

#include <GLES3/gl3.h>
#include <vector>
#include <map>
#include <string>
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/type_ptr.hpp"

namespace Renderer
{

struct TextureInfo
{
    GLenum type;
    glm::vec2 size;
};

static GLuint VAO;
static GLuint VBO;
static GLuint EBO;

static glm::mat4 projectionMatrix = glm::mat4(1.0f);
static glm::mat4 worldViewMatrix = glm::mat4(1.0f);
static glm::mat4 mvpMatrix = glm::mat4(1.0f);

static ShaderProgram shaderProgramColorTexture;
static ShaderProgram shaderProgramColorNoTexture;
static ShaderProgram shaderProgramAlpha;

static std::map<unsigned int, TextureInfo*> textures;

static unsigned int boundTexture = 0;

// Vertex Array Object for common geometry
static void setupVAO()
{
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);

    // Setup vertex attributes
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, pos));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tex));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), (void*)offsetof(Vertex, col));
    glEnableVertexAttribArray(2);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);

    glBindVertexArray(0);
}

// Use Vertex Array Objects and glMapBuffer for efficient data transfer
void GLES30Renderer::drawTriangleStrips(const Vertex* _vertices, const unsigned int _numVertices, const Blend::Factor _srcBlendFactor, const Blend::Factor _dstBlendFactor, bool verticesChanged)
{
    glBindVertexArray(VAO);

    if (verticesChanged)
    {
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        void* buffer = glMapBufferRange(GL_ARRAY_BUFFER, 0, sizeof(Vertex) * _numVertices, GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_BUFFER_BIT);
        memcpy(buffer, _vertices, sizeof(Vertex) * _numVertices);
        glUnmapBuffer(GL_ARRAY_BUFFER);
    }

    // Setup shader
    ShaderProgram* shader = nullptr;
    if (boundTexture != 0)
    {
        auto it = textures.find(boundTexture);
        if (it != textures.end() && it->second != nullptr && it->second->type == GL_ALPHA)
            shader = &shaderProgramAlpha;
        else
        {
            shader = &shaderProgramColorTexture;
            shader->use();
            shader->setUniform("uSaturation", _vertices->saturation);
            shader->setUniform("uCornerRadius", _vertices->cornerRadius);
            // Set other uniforms...
        }
    }
    else
    {
        shader = &shaderProgramColorNoTexture;
    }

    shader->use();
    shader->setUniform("uMVPMatrix", mvpMatrix);

    // Set blend mode
    if (_srcBlendFactor != Blend::ONE || _dstBlendFactor != Blend::ONE)
    {
        glEnable(GL_BLEND);
        glBlendFunc(convertBlendFactor(_srcBlendFactor), convertBlendFactor(_dstBlendFactor));
    }
    else
    {
        glDisable(GL_BLEND);
    }

    // Draw
    glDrawArrays(GL_TRIANGLE_STRIP, 0, _numVertices);

    glBindVertexArray(0);
}

// Use texture arrays for more efficient texture handling
unsigned int GLES30Renderer::createTexture(const Texture::Type _type, const bool _linear, const bool _repeat, const unsigned int _width, const unsigned int _height, void* _data)
{
    GLenum type = convertTextureType(_type);
    
    GLuint textureArray;
    glGenTextures(1, &textureArray);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray);
    
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, _width, _height, 1);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, 0, _width, _height, 1, type, GL_UNSIGNED_BYTE, _data);
    
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, _linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, _linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, _repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, _repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    
    // Store texture info
    TextureInfo* info = new TextureInfo{type, glm::vec2(_width, _height)};
    textures[textureArray] = info;
    
    return textureArray;
}

// Use Uniform Buffer Objects for shared uniforms
void GLES30Renderer::setupUniformBuffers()
{
    GLuint uboMatrices;
    glGenBuffers(1, &uboMatrices);
    glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
    glBufferData(GL_UNIFORM_BUFFER, 2 * sizeof(glm::mat4), NULL, GL_STATIC_DRAW);
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, uboMatrices);
}

void GLES30Renderer::updateMatrices()
{
    mvpMatrix = projectionMatrix * worldViewMatrix;
    
    glBindBuffer(GL_UNIFORM_BUFFER, uboMatrices);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(glm::mat4), glm::value_ptr(projectionMatrix));
    glBufferSubData(GL_UNIFORM_BUFFER, sizeof(glm::mat4), sizeof(glm::mat4), glm::value_ptr(worldViewMatrix));
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

// Use instanced rendering for repeated geometry
void GLES30Renderer::drawRepeatedGeometry(const Vertex* _vertices, const unsigned int _numVertices, const unsigned int _instances)
{
    // Setup instanced attributes...
    
    glBindVertexArray(VAO);
    glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, _numVertices, _instances);
    glBindVertexArray(0);
}

// Use compute shaders for complex operations
void GLES30Renderer::setupComputeShaders()
{
    // Compile and setup compute shaders...
}

void GLES30Renderer::runComputeShader()
{
    // Dispatch compute shader...
}

} // namespace Renderer

#endif // RENDERER_GLES_30
