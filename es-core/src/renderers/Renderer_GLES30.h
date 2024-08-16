#pragma once

#ifndef RENDERER_GLES30_H
#define RENDERER_GLES30_H

#include <vector>
#include <map>
#include <string>
#include <GLES3/gl3.h>
#include "renderers/Renderer.h"
#include "math/Transform4x4f.h"
#include "Shader.h"
#include <thread>

namespace Renderer
{

class GLES30Renderer : public IRenderer
{
public:
    GLES30Renderer();
    virtual ~GLES30Renderer();

    unsigned int getWindowFlags() override;
    void         setupWindow() override;
    void         createContext() override;
    void         destroyContext() override;
    void         resetCache() override;
    void         setMatrix(const Transform4x4f& matrix) override;
    void         setProjection(const Transform4x4f& projection) override;
    void         setViewport(const Rect& viewport) override;
    void         setScissor(const Rect& scissor) override;
    void         setSwapInterval() override;
    void         swapBuffers() override;

    void         drawLines(const Vertex* vertices, const unsigned int numVertices, const Blend::Factor srcBlendFactor = Blend::SRC_ALPHA, const Blend::Factor dstBlendFactor = Blend::ONE_MINUS_SRC_ALPHA) override;
    void         drawTriangleStrips(const Vertex* vertices, const unsigned int numVertices, const Blend::Factor srcBlendFactor = Blend::SRC_ALPHA, const Blend::Factor dstBlendFactor = Blend::ONE_MINUS_SRC_ALPHA, bool verticesChanged = true) override;
    void         drawTriangleFan(const Vertex* vertices, const unsigned int numVertices, const Blend::Factor srcBlendFactor = Blend::SRC_ALPHA, const Blend::Factor dstBlendFactor = Blend::ONE_MINUS_SRC_ALPHA) override;

    void         setStencil(const Vertex* vertices, const unsigned int numVertices) override;
    void         disableStencil() override;

    unsigned int createTexture(const Texture::Type type, const bool linear, const bool repeat, const unsigned int width, const unsigned int height, void* data) override;
    void         destroyTexture(const unsigned int texture) override;
    void         updateTexture(const unsigned int texture, const Texture::Type type, const unsigned int x, const unsigned int y, const unsigned int width, const unsigned int height, void* data) override;
    void         bindTexture(const unsigned int texture) override;

    size_t       getTotalMemUsage() override;

    void         drawSolidRectangle(float x, float y, float w, float h, unsigned int fillColor, unsigned int borderColor, float borderWidth, float cornerRadius) override;

    void         postProcessShader(const std::string& path, const float x, const float y, const float w, const float h, const std::map<std::string, std::string>& parameters, unsigned int* data) override;

    bool         shaderSupportsCornerSize(const std::string& shader) override;

    std::vector<std::pair<std::string, std::string>> getDriverInformation() override;
    std::string  getDriverName() override;

    // New methods for OpenGL ES 3.0 optimizations
    void setupVAO();
    void setupUniformBuffers();
    void updateMatrices();
    void drawRepeatedGeometry(const Vertex* vertices, const unsigned int numVertices, const unsigned int instances);
    void setupComputeShaders();
    void runComputeShader();

private:
    void bindAttributePointers();
    void setupShaders();
    
    // Vertex Array Object
    GLuint mVAO;
    
    // Vertex Buffer Object and Element Buffer Object
    GLuint mVBO;
    GLuint mEBO;
    
    // Uniform Buffer Objects
    GLuint mUBOMatrices;
    GLuint mUBOLights;
    
    // Shader Programs
    ShaderProgram mShaderProgramColorTexture;
    ShaderProgram mShaderProgramColorNoTexture;
    ShaderProgram mShaderProgramAlpha;
    ShaderProgram mComputeShader;
    
    // Texture handling
    std::map<unsigned int, TextureInfo*> mTextures;
    unsigned int mBoundTexture;
    
    // Framebuffer for post-processing
    GLuint mFramebuffer;
    
    // State caching
    bool mBlendEnabled;
    GLenum mSrcBlendFactor;
    GLenum mDstBlendFactor;
    
    // Instanced rendering data
    GLuint mInstanceVBO;
    
    // Occlusion query objects
    std::vector<GLuint> mOcclusionQueries;
    
    // Multithreading
    std::vector<std::thread> mWorkerThreads;
    
    // Frustum culling
    Frustum mFrustum;
    
    // Level of Detail system
    LODSystem mLODSystem;
    
    // Batching system
    BatchingSystem mBatchingSystem;
};

} // namespace Renderer

#endif // RENDERER_GLES30_H
