#include <engine/ShaderManager.h>
#include <engine/Renderer.h>
#include <engine/Camera.h>
#include <engine/LightManager.h>
#include <engine/IrradianceManager.h>
#include <engine/SettingsManager.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <engine/TextureManager.h>
#include <engine/EmbeddedAssets.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>
#include <shader/shader_registry.h>

#include <iostream>
#include <utility>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

engine::ShaderManager::ShaderManager(
    engine::Renderer* renderer
) : renderer(renderer) {
        renderer->registerShaderManager(this);
    }

engine::ShaderManager::~ShaderManager() {
    std::unordered_set<PassInfo*> processedPasses;
    VkDevice device = renderer->getDevice();
    auto destroyPassImages = [&](PassInfo* pass) {
        if (!pass || !pass->images.has_value()) {
            return;
        }
        for (auto& image : pass->images.value()) {
            if (image.imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(device, image.imageView, nullptr);
                image.imageView = VK_NULL_HANDLE;
            }
            if (image.image != VK_NULL_HANDLE) {
                vkDestroyImage(device, image.image, nullptr);
                image.image = VK_NULL_HANDLE;
            }
            if (image.memory != VK_NULL_HANDLE) {
                vkFreeMemory(device, image.memory, nullptr);
                image.memory = VK_NULL_HANDLE;
            }
        }
    };
    for (auto& shader : graphicsShaders) {
        if (shader->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, shader->pipeline, nullptr);
        }
        if (shader->pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, shader->pipelineLayout, nullptr);
        }
        if (shader->descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, shader->descriptorSetLayout, nullptr);
        }
        if (shader->descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, shader->descriptorPool, nullptr);
        }
        if (shader->config.passInfo) {
            PassInfo* pass = shader->config.passInfo.get();
            if (processedPasses.insert(pass).second) {
                destroyPassImages(pass);
            }
        }
    }
    for (auto& passPtr : renderPasses) {
        PassInfo* pass = passPtr.get();
        if (processedPasses.insert(pass).second) {
            destroyPassImages(pass);
        }
    }
    for (auto& shader : computeShaders) {
        if (shader->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, shader->pipeline, nullptr);
        }
        if (shader->pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, shader->pipelineLayout, nullptr);
        }
        if (shader->descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, shader->descriptorSetLayout, nullptr);
        }
        if (shader->descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, shader->descriptorPool, nullptr);
        }
    }
}

void engine::ShaderManager::loadAllShaders() {
    for (const auto& [name, shader] : graphicsShaderMap) {
        loadGraphicsShader(name);
    }
    for (const auto& [name, shader] : computeShaderMap) {
        loadComputeShader(name);
    }
}

void engine::ShaderManager::loadGraphicsShader(const std::string& name) {
    auto it = graphicsShaderMap.find(name);
    if (it != graphicsShaderMap.end()) {
        GraphicsShader* shader = it->second;
        shader->createDescriptorSetLayout(renderer);
        shader->createPipeline(renderer);
        shader->createDescriptorPool(renderer);
    } else {
        std::cout << "Warning: Graphics shader " << name << " not found.\n";
    }
}

void engine::ShaderManager::loadComputeShader(const std::string& name) {
    auto it = computeShaderMap.find(name);
    if (it != computeShaderMap.end()) {
        ComputeShader* shader = it->second;
        shader->createDescriptorSetLayout(renderer);
        shader->createPipeline(renderer);
        shader->createDescriptorPool(renderer);
    } else {
        std::cout << "Warning: Compute shader " << name << " not found.\n";
    }
}

void engine::ShaderManager::addGraphicsShader(GraphicsShader shader) {
    const std::string name = shader.name;
    if (graphicsShaderMap.contains(name)) {
        std::cout << "Warning: Graphics shader " << name << " already added. Skipping duplicate.\n";
        return;
    }
    auto shaderPtr = std::make_unique<GraphicsShader>(std::move(shader));
    GraphicsShader* rawPtr = shaderPtr.get();
    graphicsShaders.push_back(std::move(shaderPtr));
    graphicsShaderMap[name] = rawPtr;
}

void engine::ShaderManager::addComputeShader(ComputeShader shader) {
    const std::string name = shader.name;
    if (computeShaderMap.contains(name)) {
        std::cout << "Warning: Compute shader " << name << " already added. Skipping duplicate.\n";
        return;
    }
    auto shaderPtr = std::make_unique<ComputeShader>(std::move(shader));
    ComputeShader* rawPtr = shaderPtr.get();
    computeShaders.push_back(std::move(shaderPtr));
    computeShaderMap[name] = rawPtr;
}


engine::GraphicsShader* engine::ShaderManager::getGraphicsShader(const std::string& name) const {
    auto it = graphicsShaderMap.find(name);
    if (it != graphicsShaderMap.end()) {
        return it->second;
    }
    return nullptr;
}

engine::ComputeShader* engine::ShaderManager::getComputeShader(const std::string& name) const {
    auto it = computeShaderMap.find(name);
    if (it != computeShaderMap.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<char> engine::ShaderManager::getShaderBytes(const std::string& name) const {
    const auto& shaders = getEmbedded_shader();
    auto it = shaders.find(name);
    if (it != shaders.end()) {
        const auto& asset = it->second;
        return std::vector<char>(reinterpret_cast<const char*>(asset.data),
                                reinterpret_cast<const char*>(asset.data) + asset.size);
    }
    std::cerr << "Warning: Embedded shader not found: " << name << "\n";
    return {};
}

std::vector<engine::GraphicsShader> engine::ShaderManager::getGraphicsShaders() const {
    std::vector<GraphicsShader> shaders;
    for (const auto& shaderPtr : graphicsShaders) {
        shaders.push_back(*shaderPtr);
    }
    return shaders;
}

std::vector<engine::ComputeShader> engine::ShaderManager::getComputeShaders() const {
    std::vector<ComputeShader> shaders;
    for (const auto& shaderPtr : computeShaders) {
        shaders.push_back(*shaderPtr);
    }
    return shaders;
}

namespace {
    struct Vertex {
        glm::vec3 pos;
        glm::vec3 normal;
        glm::vec2 texCoord;
        glm::vec4 tangent; // xyz = tangent, w = bitangent handedness (+1 or -1)
    };

    struct UIVertex {
        glm::vec2 pos;
        glm::vec2 texCoord;
    };

    struct SkinnedVertex {
        glm::vec4 joints;  // 4 joint indices as floats
        glm::vec4 weights; // 4 joint weights
    };
}

void engine::ShaderManager::createDefaultShaders() {
    renderPasses.clear();
    auto shaderPath = [&](const std::string& baseName) -> std::string {
        return baseName;
    };

    // Define Render Passes
    auto gbufferPass = std::make_shared<PassInfo>();
    gbufferPass->name = "GBuffer";
    gbufferPass->usesSwapchain = false;
    renderPasses.push_back(gbufferPass);
    
    // GBuffer Images
    {
        std::vector<PassImage> images;
        // Albedo (Target 0)
        images.push_back({
            .name = "Albedo",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        // Normal (Target 1)
        images.push_back({
            .name = "Normal",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        // Material (Target 2)
        images.push_back({
            .name = "Material",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        // Depth
        images.push_back({
            .name = "Depth",
            .clearValue = { .depthStencil = { 1.0f, 0 } },
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        gbufferPass->images = images;
    }

    // Shadow Pass
    auto shadowPass = std::make_shared<PassInfo>();
    shadowPass->name = "ShadowPass";
    shadowPass->usesSwapchain = false;
    shadowPass->hasDepthAttachment = true;
    shadowPass->depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    renderPasses.push_back(shadowPass);

    // Shadow Image Pass
    auto shadowImagePass = std::make_shared<PassInfo>();
    shadowImagePass->name = "ShadowImagePass";
    shadowImagePass->usesSwapchain = false;
    renderPasses.push_back(shadowImagePass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "ShadowImage",
            .clearValue = { .color = { {1.0f} } },
            .format = VK_FORMAT_R8_UNORM,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .arrayLayers = 64 // max shadow-casting lights
        });
        shadowImagePass->images = images;
    }

    // Shadow Image Pass Horizontal
    auto shadowImageBlurPassH = std::make_shared<PassInfo>();
    shadowImageBlurPassH->name = "ShadowImageBlurPassH";
    shadowImageBlurPassH->usesSwapchain = false;
    renderPasses.push_back(shadowImageBlurPassH);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "ShadowImageBlurHColor",
            .clearValue = { .color = { {1.0f} } },
            .format = VK_FORMAT_R8_UNORM,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .arrayLayers = 64
        });
        shadowImageBlurPassH->images = images;
    }

    // Shadow Image Pass Vertical
    auto shadowImageBlurPassV = std::make_shared<PassInfo>();
    shadowImageBlurPassV->name = "ShadowImageBlurPassV";
    shadowImageBlurPassV->usesSwapchain = false;
    renderPasses.push_back(shadowImageBlurPassV);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "ShadowImageBlurVColor",
            .clearValue = { .color = { {1.0f} } },
            .format = VK_FORMAT_R8_UNORM,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .arrayLayers = 64
        });
        shadowImageBlurPassV->images = images;
    }

    // Particle Pass
    auto particlePass = std::make_shared<PassInfo>();
    particlePass->name = "ParticlePass";
    particlePass->usesSwapchain = false;
    renderPasses.push_back(particlePass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "ParticleColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        images.push_back({
            .name = "ParticleDepth",
            .clearValue = { .color = { {1.0f, 1.0f, 1.0f, 1.0f} } },
            .format = VK_FORMAT_R32_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        particlePass->images = images;
    }

    // Simple Particle Pass (for irradiance maps)
    auto simpleParticlePass = std::make_shared<PassInfo>();
    simpleParticlePass->name = "SimpleParticlePass";
    simpleParticlePass->usesSwapchain = false;
    simpleParticlePass->hasDepthAttachment = false;
    simpleParticlePass->attachmentFormats = { VK_FORMAT_R16G16B16A16_SFLOAT };
    renderPasses.push_back(simpleParticlePass);

    // Irradiance Pass
    auto irradiancePass = std::make_shared<PassInfo>();
    irradiancePass->name = "IrradiancePass";
    irradiancePass->usesSwapchain = false;
    irradiancePass->hasDepthAttachment = false;
    irradiancePass->attachmentFormats = { VK_FORMAT_R16G16B16A16_SFLOAT };
    renderPasses.push_back(irradiancePass);

    // Volumetric Pass
    auto volumetricPass = std::make_shared<PassInfo>();
    volumetricPass->name = "VolumetricPass";
    volumetricPass->usesSwapchain = false;
    renderPasses.push_back(volumetricPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "VolumetricColor",
            .resolutionDivider = 2, // half
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        images.push_back({
            .name = "VolumetricDepth",
            .resolutionDivider = 2,
            .clearValue = { .depthStencil = { 1.0f, 0 } },
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        volumetricPass->images = images;
    }

    // AO Pass
    auto aoPass = std::make_shared<PassInfo>();
    aoPass->name = "AOPass";
    aoPass->usesSwapchain = false;
    renderPasses.push_back(aoPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "AOColor",
            .resolutionDivider = 2, // half
            .clearValue = { .color = { {1.0f, 1.0f, 1.0f, 1.0f} } },
            .format = VK_FORMAT_R8_UNORM,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        aoPass->images = images;
    }

    // Lighting Pass
    auto lightingPass = std::make_shared<PassInfo>();
    lightingPass->name = "LightingPass";
    lightingPass->usesSwapchain = false;
    renderPasses.push_back(lightingPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "SceneColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        lightingPass->images = images;
    }

    // SSR Pass
    auto ssrPass = std::make_shared<PassInfo>();
    ssrPass->name = "SSRPass";
    ssrPass->usesSwapchain = false;
    renderPasses.push_back(ssrPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "SceneColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        ssrPass->images = images;
    }

    // Bloom Pass
    auto bloomPass = std::make_shared<PassInfo>();
    bloomPass->name = "BloomPass";
    bloomPass->usesSwapchain = false;
    renderPasses.push_back(bloomPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "BloomColor",
            .resolutionDivider = 4, // quarter
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        bloomPass->images = images;
    }

    // Bloom Blur Pass Horizontal
    auto bloomBlurPassH = std::make_shared<PassInfo>();
    bloomBlurPassH->name = "BloomBlurPassH";
    bloomBlurPassH->usesSwapchain = false;
    renderPasses.push_back(bloomBlurPassH);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "BloomBlurHColor",
            .resolutionDivider = 4, // quarter
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        bloomBlurPassH->images = images;
    }

    // Bloom Blur Pass Vertical
    auto bloomBlurPassV = std::make_shared<PassInfo>();
    bloomBlurPassV->name = "BloomBlurPassV";
    bloomBlurPassV->usesSwapchain = false;
    renderPasses.push_back(bloomBlurPassV);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "BloomBlurVColor",
            .resolutionDivider = 4, // quarter
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        bloomBlurPassV->images = images;
    }

    // Combine Pass
    auto combinePass = std::make_shared<PassInfo>();
    combinePass->name = "CombinePass";
    combinePass->usesSwapchain = false;
    renderPasses.push_back(combinePass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "CombinedColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        combinePass->images = images;
    }

    // SMAA Edge Detection Pass
    auto smaaEdgePass = std::make_shared<PassInfo>();
    smaaEdgePass->name = "SMAAEdgePass";
    smaaEdgePass->usesSwapchain = false;
    renderPasses.push_back(smaaEdgePass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "SMAAEdgesColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R8G8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        smaaEdgePass->images = images;
    }

    // SMAA Blending Weight Pass
    auto smaaWeightPass = std::make_shared<PassInfo>();
    smaaWeightPass->name = "SMAAWeightPass";
    smaaWeightPass->usesSwapchain = false;
    renderPasses.push_back(smaaWeightPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "SMAAWeightsColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        smaaWeightPass->images = images;
    }

    // SMAA Neighborhood Blending Pass
    auto smaaBlendPass = std::make_shared<PassInfo>();
    smaaBlendPass->name = "SMAABlendPass";
    smaaBlendPass->usesSwapchain = false;
    renderPasses.push_back(smaaBlendPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "SMAABlendedColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        smaaBlendPass->images = images;
    }

    // UI Pass
    auto uiPass = std::make_shared<PassInfo>();
    uiPass->name = "UIPass";
    uiPass->usesSwapchain = false;
    renderPasses.push_back(uiPass);
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "UIColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        uiPass->images = images;
    }


    auto mainPass = std::make_shared<PassInfo>();
    mainPass->name = "Main";
    mainPass->usesSwapchain = true;
    renderPasses.push_back(mainPass);

    // GBuffer
    {
        GraphicsShader shader = {
            .name = "gbuffer",
            .vertex = { shaderPath("gbuffer.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("gbuffer.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .poolMultiplier = 512,
                .vertexBitBindings = 1,
                .fragmentBitBindings = 5,
                .vertexDescriptorCounts = { 1 },
                .vertexDescriptorTypes = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .depthWrite = true,
                .enableDepth = true,
                .passInfo = gbufferPass,
                .colorAttachmentCount = 3,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(2);
                    bindings = {
                        { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
                        { .binding = 1, .stride = sizeof(SkinnedVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
                    };
                    attributes.resize(6);
                    attributes = {
                        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
                        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
                        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, texCoord) },
                        { .location = 3, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Vertex, tangent) },
                        { .location = 4, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(SkinnedVertex, joints) },
                        { .location = 5, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(SkinnedVertex, weights) }
                    };
                }
            }
        };
        shader.config.setPushConstant<GBufferPC>(VK_SHADER_STAGE_VERTEX_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Shadow Shader
    {
        GraphicsShader shader = {
            .name = "shadow",
            .vertex = { shaderPath("shadow.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .config = {
                .poolMultiplier = 512,
                .vertexBitBindings = 2,
                .fragmentBitBindings = 0,
                .vertexDescriptorCounts = { 1, 1 },
                .vertexDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = true,
                .depthCompare = VK_COMPARE_OP_LESS,
                .enableDepth = true,
                .passInfo = shadowPass,
                .colorAttachmentCount = 0,
                .viewMask = 0x3Fu,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(2);
                    bindings = {
                        { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX },
                        { .binding = 1, .stride = sizeof(SkinnedVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
                    };
                    attributes.resize(3);
                    attributes = {
                        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
                        { .location = 1, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(SkinnedVertex, joints) },
                        { .location = 2, .binding = 1, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(SkinnedVertex, weights) }
                    };
                }
            }
        };
        shader.config.setPushConstant<ShadowPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    auto getActiveShadowLayers = [](Renderer* renderer) -> uint32_t {
        uint32_t activeShadowLayers = 0;
        if (engine::LightManager* lightManager = renderer->getLightManager()) {
            auto& lights = lightManager->getLights();
            for (uint32_t i = 0; i < static_cast<uint32_t>(lights.size()) && i < 64u; ++i) {
                engine::Light& light = lights[i];
                if (light.getShadowImageView(0) == VK_NULL_HANDLE) {
                    continue;
                }
                ++activeShadowLayers;
                if (activeShadowLayers >= 64u) {
                    break;
                }
            }
        }
        return activeShadowLayers;
    };

    // Shadow Image Compute
    {
        ComputeShader shader = {
            .name = "shadowimage",
            .compute = { shaderPath("shadowimage.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .poolMultiplier = 1,
                .computeBitBindings = 6,
                .computeDescriptorCounts = { 1, 1, 1, 64, 1, 1 },
                .computeDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .workgroupSizeX = 16,
                .workgroupSizeY = 16,
                .workgroupSizeZ = 1,
                .fillPushConstants = [](Renderer* renderer, ComputeShader* shader, VkCommandBuffer cmd) {
                    engine::Camera* camera = renderer->getEntityManager()->getCamera();
                    if (camera) {
                        // 1, 2, 4, 8
                        uint32_t shadowSamples = pow(2, static_cast<int>(renderer->getSettingsManager()->getSettings()->shadowQuality));
                        ShadowImagePC pc = {
                            .invView = camera->getInvViewMatrix(),
                            .invProj = camera->getInvProjectionMatrix(),
                            .camPos = camera->getWorldPosition(),
                            .shadowSamples = shadowSamples
                        };
                        vkCmdPushConstants(
                            cmd,
                            shader->pipelineLayout,
                            shader->config.pushConstantRange.stageFlags,
                            0,
                            sizeof(ShadowImagePC),
                            &pc
                        );
                    }
                },
                .getDispatchLayerCount = [getActiveShadowLayers](Renderer* renderer, ComputeShader*) {
                    return getActiveShadowLayers(renderer);
                },
                .inputBindings = {
                    {
                        .binding = 0,
                        .bufferProvider = [](Renderer* renderer, size_t i) -> VkDescriptorBufferInfo {
                            LightManager* lightManager = renderer->getLightManager();
                            auto& lightsBuffers = lightManager->getLightsBuffers();
                            if (lightsBuffers.size() < renderer->getMaxFramesInFlight()) {
                                lightManager->createLightsUBO();
                            }
                            if (i >= lightsBuffers.size() || lightsBuffers[i] == VK_NULL_HANDLE) {
                                std::cout << "Warning: Lights UBO buffer missing for frame " << i << " after ensure. Skipping descriptor write.\n";
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{lightsBuffers[i], 0, sizeof(LightsUBO)};
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    },
                    { 1, "gbuffer", "Depth" },
                    { 2, "gbuffer", "Normal" },
                    {
                        .binding = 3,
                        .imageArrayProvider = [](Renderer* renderer, size_t frameIndex, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) {
                            auto* textureManager = renderer->getTextureManager();
                            Texture* fallbackTex = textureManager ? textureManager->getTexture("fallback_shadow_cube") : nullptr;
                            VkImageView fallbackView = (fallbackTex && fallbackTex->imageView != VK_NULL_HANDLE) ? fallbackTex->imageView : VK_NULL_HANDLE;
                            auto& lights = renderer->getLightManager()->getLights();

                            const size_t startIdx = imageInfos.size();
                            imageInfos.resize(startIdx + count, {
                                .sampler = VK_NULL_HANDLE,
                                .imageView = fallbackView,
                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            });

                            const uint32_t lightCount = static_cast<uint32_t>(std::min<size_t>(lights.size(), count));
                            uint32_t shadowLayer = 0;
                            for (uint32_t i = 0; i < lightCount && shadowLayer < count; ++i) {
                                Light& light = lights[i];
                                VkImageView shadowView = light.getShadowImageView(frameIndex);
                                if (shadowView != VK_NULL_HANDLE) {
                                    imageInfos[startIdx + shadowLayer].imageView = shadowView;
                                    ++shadowLayer;
                                }
                            }
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 4,
                        .sourceShaderName = "shadowimage",
                        .attachmentName = "ShadowImage",
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                    }
                }
            }
        };
        shader.config.setPushConstant<ShadowImagePC>(VK_SHADER_STAGE_COMPUTE_BIT);
        addComputeShader(std::move(shader));
    }

    // Shadow Image Blur Horizontal
    {
        ComputeShader shader = {
            .name = "shadowimageblurh",
            .compute = { shaderPath("blurarray.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .poolMultiplier = 1,
                .computeBitBindings = 6,
                .computeDescriptorCounts = {
                    1, 1, 1, 1, 1, 1
                },
                .computeDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .workgroupSizeX = 16,
                .workgroupSizeY = 16,
                .workgroupSizeZ = 1,
                .fillPushConstants = [getActiveShadowLayers](Renderer* renderer, ComputeShader* shader, VkCommandBuffer cmd) {
                    uint32_t activeShadowLayers = getActiveShadowLayers(renderer);
                    engine::BlurArrayPC pc = {
                        .invProj = glm::mat4(1.0f),
                        .blurDirection = 0,
                        .taps = 4,
                        .layerCount = activeShadowLayers
                    };
                    if (engine::Camera* camera = renderer->getEntityManager()->getCamera()) {
                        pc.invProj = camera->getInvProjectionMatrix();
                    }
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(engine::BlurArrayPC), &pc);
                },
                .getDispatchLayerCount = [getActiveShadowLayers](Renderer* renderer, ComputeShader*) {
                    return getActiveShadowLayers(renderer);
                },
                .inputBindings = {
                    {
                        .binding = 0,
                        .sourceShaderName = "shadowimage",
                        .attachmentName = "ShadowImage",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 1,
                        .sourceShaderName = "gbuffer",
                        .attachmentName = "Depth",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 2,
                        .sourceShaderName = "gbuffer",
                        .attachmentName = "Normal",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 3,
                        .sourceShaderName = "shadowimageblurh",
                        .attachmentName = "ShadowImageBlurHColor",
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                    },
                    {
                        .binding = 4,
                        .bufferProvider = [](Renderer* renderer, size_t i) -> VkDescriptorBufferInfo {
                            LightManager* lightManager = renderer->getLightManager();
                            auto& lightsBuffers = lightManager->getLightsBuffers();
                            if (lightsBuffers.size() < renderer->getMaxFramesInFlight()) {
                                lightManager->createLightsUBO();
                            }
                            if (i >= lightsBuffers.size() || lightsBuffers[i] == VK_NULL_HANDLE) {
                                std::cout << "Warning: Lights UBO buffer missing for frame " << i << " after ensure. Skipping descriptor write.\n";
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{lightsBuffers[i], 0, sizeof(LightsUBO)};
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    }
                }
            }
        };
        shader.config.setPushConstant<engine::BlurArrayPC>(VK_SHADER_STAGE_COMPUTE_BIT);
        addComputeShader(std::move(shader));
    }

    // Shadow Image Blur Vertical
    {
        ComputeShader shader = {
            .name = "shadowimageblurv",
            .compute = { shaderPath("blurarray.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .poolMultiplier = 1,
                .computeBitBindings = 6,
                .computeDescriptorCounts = {
                    1, 1, 1, 1, 1, 1
                },
                .computeDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .workgroupSizeX = 16,
                .workgroupSizeY = 16,
                .workgroupSizeZ = 1,
                .fillPushConstants = [getActiveShadowLayers](Renderer* renderer, ComputeShader* shader, VkCommandBuffer cmd) {
                    uint32_t activeShadowLayers = getActiveShadowLayers(renderer);
                    engine::BlurArrayPC pc = {
                        .invProj = glm::mat4(1.0f),
                        .blurDirection = 1,
                        .taps = 4,
                        .layerCount = activeShadowLayers
                    };
                    if (engine::Camera* camera = renderer->getEntityManager()->getCamera()) {
                        pc.invProj = camera->getInvProjectionMatrix();
                    }
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(engine::BlurArrayPC), &pc);
                },
                .getDispatchLayerCount = [getActiveShadowLayers](Renderer* renderer, ComputeShader*) {
                    return getActiveShadowLayers(renderer);
                },
                .inputBindings = {
                    {
                        .binding = 0,
                        .sourceShaderName = "shadowimageblurh",
                        .attachmentName = "ShadowImageBlurHColor",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 1,
                        .sourceShaderName = "gbuffer",
                        .attachmentName = "Depth",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 2,
                        .sourceShaderName = "gbuffer",
                        .attachmentName = "Normal",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 3,
                        .sourceShaderName = "shadowimageblurv",
                        .attachmentName = "ShadowImageBlurVColor",
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                    },
                    {
                        .binding = 4,
                        .bufferProvider = [](Renderer* renderer, size_t i) -> VkDescriptorBufferInfo {
                            LightManager* lightManager = renderer->getLightManager();
                            auto& lightsBuffers = lightManager->getLightsBuffers();
                            if (lightsBuffers.size() < renderer->getMaxFramesInFlight()) {
                                lightManager->createLightsUBO();
                            }
                            if (i >= lightsBuffers.size() || lightsBuffers[i] == VK_NULL_HANDLE) {
                                std::cout << "Warning: Lights UBO buffer missing for frame " << i << " after ensure. Skipping descriptor write.\n";
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{lightsBuffers[i], 0, sizeof(LightsUBO)};
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    }
                }
            }
        };
        shader.config.setPushConstant<engine::BlurArrayPC>(VK_SHADER_STAGE_COMPUTE_BIT);
        addComputeShader(std::move(shader));
    }

    // AO
    {
        ComputeShader shader = {
            .name = "ao",
            .compute = { shaderPath("ao.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .computeBitBindings = 4,
                .computeDescriptorCounts = {
                    1, 1, 1, 1
                },
                .computeDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .workgroupSizeX = 16,
                .workgroupSizeY = 16,
                .workgroupSizeZ = 1,
                .fillPushConstants = [](Renderer* renderer, ComputeShader* shader, VkCommandBuffer cmd) {
                    engine::Camera* camera = renderer->getEntityManager()->getCamera();
                    if (camera) {
                        glm::mat4 invProj = glm::inverse(camera->getProjectionMatrix());
                        glm::mat4 proj = camera->getProjectionMatrix();
                        glm::mat4 view = camera->getViewMatrix();
                        AOPC pc = {
                            .invProj = invProj,
                            .proj = proj,
                            .view = view,
                            .flags = renderer->getSettingsManager()->getSettings()->aoMode
                        };
                        vkCmdPushConstants(
                            cmd,
                            shader->pipelineLayout,
                            shader->config.pushConstantRange.stageFlags,
                            0,
                            sizeof(AOPC),
                            &pc
                        );
                    }
                },
                .inputBindings = {
                    {
                        .binding = 0,
                        .sourceShaderName = "gbuffer",
                        .attachmentName = "Depth",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 1,
                        .sourceShaderName = "gbuffer",
                        .attachmentName = "Normal",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 2,
                        .sourceShaderName = "ao",
                        .attachmentName = "AOColor",
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                    }
                }
            }
        };
        shader.config.setPushConstant<AOPC>(VK_SHADER_STAGE_COMPUTE_BIT);
        addComputeShader(std::move(shader));
    }

    // Particle
    {
        GraphicsShader shader = {
            .name = "particle",
            .vertex = { shaderPath("particle.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("particle.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .poolMultiplier = 1,
                .vertexBitBindings = 1,
                .fragmentBitBindings = 2,
                .vertexDescriptorCounts = {
                    1
                },
                .vertexDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                },
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = particlePass,
                .blendEnable = true,
                .blendAdditive = true,
                .colorAttachmentCount = 2,
                .colorBlendOverrides = {
                    VkPipelineColorBlendAttachmentState{
                        .blendEnable = VK_TRUE,
                        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
                        .colorBlendOp = VK_BLEND_OP_ADD,
                        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                        .alphaBlendOp = VK_BLEND_OP_ADD,
                        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
                    },
                    VkPipelineColorBlendAttachmentState{
                        .blendEnable = VK_TRUE,
                        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
                        .colorBlendOp = VK_BLEND_OP_MIN,
                        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
                        .alphaBlendOp = VK_BLEND_OP_MIN,
                        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT
                    }
                },
                .getVertexInputDescriptions = nullptr
            }
        };
        shader.config.setPushConstant<ParticlePC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Simple Particle (for irradiance)
    {
        ComputeShader shader = {
            .name = "particlesimple",
            .compute = { shaderPath("particlesimple.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .poolMultiplier = 1,
                .computeBitBindings = 6,
                .computeDescriptorCounts = { 1, kMaxIrradianceProbes, 1, 1, kMaxIrradianceProbes, 1 },
                .computeDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .workgroupSizeX = 8,
                .workgroupSizeY = 8,
                .workgroupSizeZ = 1,
                .fillPushConstants = [](Renderer* renderer, ComputeShader* shader, VkCommandBuffer cmd) {
                    if (!renderer || !shader) {
                        return;
                    }
                    const uint32_t frameIndex = renderer->getCurrentFrameIndex();
                    IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                    uint32_t activeComputeProbeCount = 0u;
                    if (irradianceManager) {
                        activeComputeProbeCount = irradianceManager->getDynamicComputeProbeCount(frameIndex);
                    }
                    uint32_t particleCount = 0u;
                    if (ParticleManager* particleManager = renderer->getParticleManager()) {
                        particleCount = particleManager->getParticleCount();
                    }
                    SimpleParticlePC pc = {
                        .probePosition = glm::vec4(0.0f),
                        .particleSize = 0.1f,
                        .particleCount = particleCount,
                        .cubemapSize = 16u,
                        .activeProbeCount = activeComputeProbeCount,
                        .layerBase = 0u,
                        .mappingOffset = 0u,
                        .pad = 0u
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(SimpleParticlePC), &pc);
                },
                .getDispatchLayerCount = [](Renderer* renderer, ComputeShader*) {
                    if (!renderer) {
                        return 0u;
                    }
                    IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                    if (!irradianceManager) {
                        return 0u;
                    }
                    const uint32_t frameIndex = renderer->getCurrentFrameIndex();
                    const uint32_t activeProbeCount = irradianceManager->getDynamicComputeProbeCount(frameIndex);
                    return activeProbeCount * 6u;
                },
                .getDispatchWidth = [](Renderer*, ComputeShader*) {
                    return 16u;
                },
                .getDispatchHeight = [](Renderer*, ComputeShader*) {
                    return 16u;
                },
                .inputBindings = {
                    {
                        .binding = 0,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            ParticleManager* particleManager = renderer->getParticleManager();
                            if (!particleManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            const auto& particleBuffers = particleManager->getParticleBuffers();
                            if (frameIndex >= particleBuffers.size() || particleBuffers[frameIndex] == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ particleBuffers[frameIndex], 0, VK_WHOLE_SIZE };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    },
                    {
                        .binding = 1,
                        .imageArrayProvider = [](Renderer* renderer, size_t frameIndex, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return;
                            }
                            irradianceManager->fillDynamicProbeStorageImageInfos(static_cast<uint32_t>(frameIndex), count, imageInfos);
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                    },
                    {
                        .binding = 2,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            irradianceManager->createActiveProbeIndexBuffers();
                            VkBuffer indexBuffer = irradianceManager->getActiveProbeIndexBuffer(static_cast<uint32_t>(frameIndex));
                            if (indexBuffer == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ indexBuffer, 0, sizeof(uint32_t) * kMaxIrradianceProbes };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    },
                    {
                        .binding = 3,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            auto& irradianceBuffers = irradianceManager->getIrradianceProbesBuffers();
                            if (irradianceBuffers.size() < renderer->getMaxFramesInFlight()) {
                                irradianceManager->createIrradianceProbesUBO();
                            }
                            if (frameIndex >= irradianceBuffers.size() || irradianceBuffers[frameIndex] == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ irradianceBuffers[frameIndex], 0, sizeof(IrradianceProbesUBO) };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    },
                    {
                        .binding = 4,
                        .imageArrayProvider = [](Renderer* renderer, size_t, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return;
                            }
                            irradianceManager->fillBakedProbeCubemapImageInfos(count, imageInfos);
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    }
                }
            }
        };
        shader.config.setPushConstant<SimpleParticlePC>(VK_SHADER_STAGE_COMPUTE_BIT);
        addComputeShader(std::move(shader));
    }

    // Spherical Harmonics Projection
    {
        ComputeShader shader = {
            .name = "sh",
            .compute = { shaderPath("sh.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .poolMultiplier = 1,
                .computeBitBindings = 4,
                .computeDescriptorCounts = { 1, kMaxIrradianceProbes, 1, 1 },
                .computeDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                },
                .workgroupSizeX = 8,
                .workgroupSizeY = 8,
                .workgroupSizeZ = 1,
                .fillPushConstants = [](Renderer* renderer, ComputeShader* shader, VkCommandBuffer cmd) {
                    if (!renderer || !shader) {
                        return;
                    }
                    uint32_t activeProbeCount = 0u;
                    if (IrradianceManager* irradianceManager = renderer->getIrradianceManager()) {
                        const uint32_t frameIndex = renderer->getCurrentFrameIndex();
                        activeProbeCount = irradianceManager->getDynamicComputeProbeCount(frameIndex);
                    }
                    SHPC pc = {
                        .cubemapSize = 16u,
                        .activeProbeCount = activeProbeCount,
                        .pad0 = 0u,
                        .pad1 = 0u
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(SHPC), &pc);
                },
                .getDispatchLayerCount = [](Renderer* renderer, ComputeShader*) {
                    if (!renderer) {
                        return 0u;
                    }
                    IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                    if (!irradianceManager) {
                        return 0u;
                    }
                    const uint32_t frameIndex = renderer->getCurrentFrameIndex();
                    return irradianceManager->getDynamicComputeProbeCount(frameIndex) * 6u;
                },
                .getDispatchWidth = [](Renderer*, ComputeShader*) {
                    return 16u;
                },
                .getDispatchHeight = [](Renderer*, ComputeShader*) {
                    return 16u;
                },
                .inputBindings = {
                    {
                        .binding = 0,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            irradianceManager->createDynamicSHPartialBuffers();
                            VkBuffer partialBuffer = irradianceManager->getDynamicSHPartialBuffer(static_cast<uint32_t>(frameIndex));
                            if (partialBuffer == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ partialBuffer, 0, VK_WHOLE_SIZE };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    },
                    {
                        .binding = 1,
                        .imageArrayProvider = [](Renderer* renderer, size_t frameIndex, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return;
                            }
                            irradianceManager->fillDynamicProbeCubemapImageInfos(static_cast<uint32_t>(frameIndex), count, imageInfos);
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 3,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            irradianceManager->createActiveProbeIndexBuffers();
                            VkBuffer indexBuffer = irradianceManager->getActiveProbeIndexBuffer(static_cast<uint32_t>(frameIndex));
                            if (indexBuffer == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ indexBuffer, 0, sizeof(uint32_t) * kMaxIrradianceProbes };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    }
                }
            }
        };
        shader.config.setPushConstant<SHPC>(VK_SHADER_STAGE_COMPUTE_BIT);
        addComputeShader(std::move(shader));
    }

    // Spherical Harmonics Reduction
    {
        ComputeShader shader = {
            .name = "shreduce",
            .compute = { shaderPath("shreduce.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .poolMultiplier = 1,
                .computeBitBindings = 3,
                .computeDescriptorCounts = { 1, 1, 1 },
                .computeDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                },
                .workgroupSizeX = 64,
                .workgroupSizeY = 1,
                .workgroupSizeZ = 1,
                .fillPushConstants = [](Renderer* renderer, ComputeShader* shader, VkCommandBuffer cmd) {
                    if (!renderer || !shader) {
                        return;
                    }
                    uint32_t activeProbeCount = 0u;
                    if (IrradianceManager* irradianceManager = renderer->getIrradianceManager()) {
                        const uint32_t frameIndex = renderer->getCurrentFrameIndex();
                        activeProbeCount = irradianceManager->getDynamicComputeProbeCount(frameIndex);
                    }
                    SHPC pc = {
                        .cubemapSize = 16u,
                        .activeProbeCount = activeProbeCount,
                        .pad0 = 0u,
                        .pad1 = 0u
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(SHPC), &pc);
                },
                .getDispatchWidth = [](Renderer* renderer, ComputeShader*) {
                    if (!renderer) {
                        return 0u;
                    }
                    IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                    if (!irradianceManager) {
                        return 0u;
                    }
                    const uint32_t frameIndex = renderer->getCurrentFrameIndex();
                    return irradianceManager->getDynamicComputeProbeCount(frameIndex);
                },
                .getDispatchHeight = [](Renderer*, ComputeShader*) {
                    return 1u;
                },
                .inputBindings = {
                    {
                        .binding = 0,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            irradianceManager->createDynamicSHOutputBuffers();
                            VkBuffer outputBuffer = irradianceManager->getDynamicSHOutputBuffer(static_cast<uint32_t>(frameIndex));
                            if (outputBuffer == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ outputBuffer, 0, VK_WHOLE_SIZE };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    },
                    {
                        .binding = 1,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            irradianceManager->createDynamicSHPartialBuffers();
                            VkBuffer partialBuffer = irradianceManager->getDynamicSHPartialBuffer(static_cast<uint32_t>(frameIndex));
                            if (partialBuffer == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ partialBuffer, 0, VK_WHOLE_SIZE };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    },
                    {
                        .binding = 2,
                        .bufferProvider = [](Renderer* renderer, size_t frameIndex) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            irradianceManager->createActiveProbeIndexBuffers();
                            VkBuffer indexBuffer = irradianceManager->getActiveProbeIndexBuffer(static_cast<uint32_t>(frameIndex));
                            if (indexBuffer == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{ indexBuffer, 0, sizeof(uint32_t) * kMaxIrradianceProbes };
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    }
                }
            }
        };
        shader.config.setPushConstant<SHPC>(VK_SHADER_STAGE_COMPUTE_BIT);
        addComputeShader(std::move(shader));
    }

    // Irradiance Shader
    {
        GraphicsShader shader = {
            .name = "irradiance",
            .vertex = { shaderPath("irradiance.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("irradiance.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .poolMultiplier = 512,
                .vertexBitBindings = 1,
                .fragmentBitBindings = 5,
                .vertexDescriptorCounts = { 1 },
                .vertexDescriptorTypes = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .depthCompare = VK_COMPARE_OP_ALWAYS,
                .enableDepth = false,
                .passInfo = irradiancePass,
                .colorAttachmentCount = 1,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(1);
                    bindings = {
                        { .binding = 0, .stride = sizeof(Vertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
                    };
                    attributes.resize(3);
                    attributes = {
                        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, pos) },
                        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, texCoord) },
                        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, normal) },
                    };
                }
            }
        };
        shader.config.setPushConstant<IrradianceBakePC>(VK_SHADER_STAGE_VERTEX_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Volumetric
    {
        GraphicsShader shader = {
            .name = "volumetric",
            .vertex = { shaderPath("volumetric.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("volumetric.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .poolMultiplier = 1,
                .vertexBitBindings = 1,
                .fragmentBitBindings = 2,
                .vertexDescriptorCounts = {
                    1
                },
                .vertexDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                },
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .depthWrite = true,
                .depthCompare = VK_COMPARE_OP_ALWAYS,
                .enableDepth = true,
                .passInfo = volumetricPass,
                .blendEnable = true,
                .blendAdditive = true,
                .colorAttachmentCount = 1,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(1);
                    bindings = {
                        { .binding = 0, .stride = sizeof(glm::vec3), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
                    };
                    attributes.resize(1);
                    attributes = {
                        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = 0 }
                    };
                }
            }
        };
        shader.config.setPushConstant<VolumetricPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Lighting
    {
        GraphicsShader shader = {
            .name = "lighting",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("lighting.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 2,
                .fragmentBitBindings = 9,
                .vertexDescriptorCounts = { 1, 1 },
                .vertexDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                },
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1, 1, 1, 1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER,
                    VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = lightingPass,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    engine::Camera* camera = renderer->getEntityManager()->getCamera();
                    if (camera) {
                        LightingPC pc = {
                            .invView = camera->getInvViewMatrix(),
                            .invProj = camera->getInvProjectionMatrix(),
                            .camPos = camera->getWorldPosition()
                        };
                        vkCmdPushConstants(
                            cmd,
                            shader->pipelineLayout,
                            shader->config.pushConstantRange.stageFlags,
                            0,
                            sizeof(LightingPC),
                            &pc
                        );
                    }
                },
                .inputBindings = {
                    {
                        .binding = 10,
                        .bufferProvider = [](Renderer* renderer, size_t i) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            if (!irradianceManager) {
                                return VkDescriptorBufferInfo{};
                            }
                            irradianceManager->createDynamicSHOutputBuffers();
                            VkBuffer shBuffer = irradianceManager->getDynamicSHOutputBuffer(static_cast<uint32_t>(i));
                            if (shBuffer == VK_NULL_HANDLE) {
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{shBuffer, 0, sizeof(ProbeSHData) * kMaxIrradianceProbes};
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER
                    },
                    {
                        .binding = 0,
                        .bufferProvider = [](Renderer* renderer, size_t i) -> VkDescriptorBufferInfo {
                            LightManager* lightManager = renderer->getLightManager();
                            auto& lightsBuffers = lightManager->getLightsBuffers();
                            if (lightsBuffers.size() < renderer->getMaxFramesInFlight()) {
                                lightManager->createLightsUBO();
                            }
                            if (i >= lightsBuffers.size() || lightsBuffers[i] == VK_NULL_HANDLE) {
                                std::cout << "Warning: Lights UBO buffer missing for frame " << i << " after ensure. Skipping descriptor write.\n";
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{lightsBuffers[i], 0, sizeof(LightsUBO)};
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    },
                    {
                        .binding = 1,
                        .bufferProvider = [](Renderer* renderer, size_t i) -> VkDescriptorBufferInfo {
                            IrradianceManager* irradianceManager = renderer->getIrradianceManager();
                            auto& irradianceProbesBuffers = irradianceManager->getIrradianceProbesBuffers();
                            if (irradianceProbesBuffers.size() < renderer->getMaxFramesInFlight()) {
                                irradianceManager->createIrradianceProbesUBO();
                            }
                            if (i >= irradianceProbesBuffers.size() || irradianceProbesBuffers[i] == VK_NULL_HANDLE) {
                                std::cout << "Warning: Irradiance UBO buffer missing for frame " << i << " after ensure. Skipping descriptor write.\n";
                                return VkDescriptorBufferInfo{};
                            }
                            return VkDescriptorBufferInfo{irradianceProbesBuffers[i], 0, sizeof(IrradianceProbesUBO)};
                        },
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                    },
                    { 2, "gbuffer", "Albedo" },
                    { 3, "gbuffer", "Normal" },
                    { 4, "gbuffer", "Material" },
                    { 5, "gbuffer", "Depth" },
                    { 6, "particle", "ParticleColor" },
                    { 7, "volumetric", "VolumetricColor" },
                    { 8, "shadowimageblurv", "ShadowImageBlurVColor" }
                }
            }
        };
        shader.config.setPushConstant<LightingPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // SSR
    {
        GraphicsShader shader = {
            .name = "ssr",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("ssr.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 7,
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1, 1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = ssrPass,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    engine::Camera* camera = renderer->getEntityManager()->getCamera();
                    if (camera) {
                        SSRPC pc = {
                            .view = camera->getViewMatrix(),
                            .proj = camera->getProjectionMatrix(),
                            .invView = camera->getInvViewMatrix(),
                            .invProj = camera->getInvProjectionMatrix()
                        };
                        vkCmdPushConstants(
                            cmd,
                            shader->pipelineLayout,
                            shader->config.pushConstantRange.stageFlags,
                            0,
                            sizeof(SSRPC),
                            &pc
                        );
                    }
                },
                .inputBindings = {
                    { 0, "lighting", "SceneColor" },
                    { 1, "gbuffer", "Depth" },
                    { 2, "gbuffer", "Normal" },
                    { 3, "gbuffer", "Material" },
                    { 4, "particle", "ParticleDepth" },
                    { 5, "volumetric", "VolumetricDepth" }
                }
            }
        };
        shader.config.setPushConstant<SSRPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Bloom
    {
        GraphicsShader shader = {
            .name = "bloom",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("bloom.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = bloomPass,
                .colorAttachmentCount = 1,
                .inputBindings = {
                    { 0, "lighting", "SceneColor" }
                }
            }
        };
        addGraphicsShader(std::move(shader));
    }

    // Bloom Blur Horizontal
    {
        GraphicsShader shader = {
            .name = "bloomblurh",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("blur.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = bloomBlurPassH,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    engine::BlurPC pc = {
                        .blurDirection = 0,
                        .taps = 2
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(engine::BlurPC), &pc);
                },
                .inputBindings = {
                    { 0, "bloom", "BloomColor" }
                }
            }
        };
        shader.config.setPushConstant<engine::BlurPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Bloom Blur Vertical
    {
        GraphicsShader shader = {
            .name = "bloomblurv",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("blur.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = bloomBlurPassV,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    engine::BlurPC pc = {
                        .blurDirection = 1,
                        .taps = 1
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(engine::BlurPC), &pc);
                },
                .inputBindings = {
                    { 0, "bloomblurh", "BloomBlurHColor" }
                }
            }
        };
        shader.config.setPushConstant<engine::BlurPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // UI
    {
        GraphicsShader shader = {
            .name = "ui",
            .vertex = { shaderPath("ui.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("ui.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .poolMultiplier = 64,
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = uiPass,
                .colorAttachmentCount = 1,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(1);
                    bindings = {
                        { .binding = 0, .stride = sizeof(UIVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
                    };
                    attributes.resize(2);
                    attributes = {
                        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(UIVertex, pos) },
                        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(UIVertex, texCoord) }
                    };
                }
            }
        };
        shader.config.setPushConstant<UIPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Text
    {
        GraphicsShader shader = {
            .name = "text",
            .vertex = { shaderPath("ui.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("text.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .poolMultiplier = 1024,
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = uiPass,
                .colorAttachmentCount = 1,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(1);
                    bindings = {
                        { .binding = 0, .stride = sizeof(UIVertex), .inputRate = VK_VERTEX_INPUT_RATE_VERTEX }
                    };
                    attributes.resize(2);
                    attributes = {
                        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(UIVertex, pos) },
                        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(UIVertex, texCoord) }
                    };
                }
            }
        };
        shader.config.setPushConstant<UIPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Combine
    {
        GraphicsShader shader = {
            .name = "combine",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("combine.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 5,
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = combinePass,
                .colorAttachmentCount = 1,
                .inputBindings = {
                    { 0, "lighting", "SceneColor" },
                    { 1, "ssr", "SceneColor" },
                    { 2, "ao", "AOColor" },
                    { 3, "bloomblurv", "BloomBlurVColor" }
                }
            }
        };
        addGraphicsShader(std::move(shader));
    }

    // SMAA Edge Detection
    {
        GraphicsShader shader = {
            .name = "smaaEdge",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("smaaEdge.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .fragmentDescriptorCounts = {
                    1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = smaaEdgePass,
                .blendEnable = false,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    VkExtent2D swapChainExtent = renderer->getSwapChainExtent();
                    CompositePC pc = {
                        .inverseScreenSize = glm::vec2(1.0f / static_cast<float>(swapChainExtent.width), 1.0f / static_cast<float>(swapChainExtent.height)),
                        .flags = renderer->getSettingsManager()->getSettings()->aaMode
                    };
                    vkCmdPushConstants(
                        cmd,
                        shader->pipelineLayout,
                        shader->config.pushConstantRange.stageFlags,
                        0,
                        sizeof(CompositePC),
                        &pc
                    );
                },
                .inputBindings = {
                    { 0, "combine", "CombinedColor" }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // SMAA Blending Weight Calculation
    {
        GraphicsShader shader = {
            .name = "smaaWeight",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("smaaWeight.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 4,
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = smaaWeightPass,
                .blendEnable = false,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    VkExtent2D swapChainExtent = renderer->getSwapChainExtent();
                    CompositePC pc = {
                        .inverseScreenSize = glm::vec2(1.0f / static_cast<float>(swapChainExtent.width), 1.0f / static_cast<float>(swapChainExtent.height)),
                        .flags = renderer->getSettingsManager()->getSettings()->aaMode
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(CompositePC), &pc);
                },
                .inputBindings = {
                    { 0, "smaaEdge", "SMAAEdgesColor" },
                    {
                        .binding = 1,
                        .textureName = "smaa_area",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    },
                    {
                        .binding = 2,
                        .textureName = "smaa_search",
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                    }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // SMAA Neighborhood Blending
    {
        GraphicsShader shader = {
            .name = "smaaBlend",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("smaaBlend.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 3,
                .fragmentDescriptorCounts = {
                    1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = smaaBlendPass,
                .blendEnable = false,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    VkExtent2D swapChainExtent = renderer->getSwapChainExtent();
                    CompositePC pc = {
                        .inverseScreenSize = glm::vec2(1.0f / static_cast<float>(swapChainExtent.width), 1.0f / static_cast<float>(swapChainExtent.height)),
                        .flags = renderer->getSettingsManager()->getSettings()->aaMode
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(CompositePC), &pc);
                },
                .inputBindings = {
                    { 0, "combine", "CombinedColor" },
                    { 1, "smaaWeight", "SMAAWeightsColor" }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    // Composite
    {
        GraphicsShader shader = {
            .name = "composite",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("composite.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 0,
                .fragmentBitBindings = 4,
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = mainPass,
                .colorAttachmentCount = 1,
                .fillPushConstants = [](Renderer* renderer, GraphicsShader* shader, VkCommandBuffer cmd) {
                    VkExtent2D swapChainExtent = renderer->getSwapChainExtent();
                    CompositePC pc = {
                        .inverseScreenSize = glm::vec2(1.0f / static_cast<float>(swapChainExtent.width), 1.0f / static_cast<float>(swapChainExtent.height)),
                        .flags = renderer->getSettingsManager()->getSettings()->aaMode
                    };
                    vkCmdPushConstants(cmd, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(CompositePC), &pc);
                },
                .inputBindings = {
                    { 0, "combine", "CombinedColor" },
                    { 1, "ui", "UIColor" },
                    { 2, "smaaBlend", "SMAABlendedColor" }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        addGraphicsShader(std::move(shader));
    }

    auto generalGraphicsLane = std::make_shared<RenderLane>(RenderLane{
        .name = "GeneralGraphics",
        .allowGraphics = true,
        .allowCompute = false,
        .preferAsync = false,
        .mustPreserveOrder = false
    });
    auto volumetricLane = std::make_shared<RenderLane>(RenderLane{
        .name = "Volumetric",
        .allowGraphics = true,
        .allowCompute = false,
        .preferAsync = true,
        .mustPreserveOrder = false
    });
    auto shadowLane = std::make_shared<RenderLane>(RenderLane{
        .name = "Shadow",
        .allowGraphics = true,
        .allowCompute = true,
        .preferAsync = true,
        .mustPreserveOrder = true
    });
    auto irradianceSHLane = std::make_shared<RenderLane>(RenderLane{
        .name = "IrradianceSH",
        .allowGraphics = true,
        .allowCompute = true,
        .preferAsync = true,
        .mustPreserveOrder = true
    });
    auto irradianceRenderLane = std::make_shared<RenderLane>(RenderLane{
        .name = "IrradianceRender",
        .allowGraphics = true,
        .allowCompute = true,
        .preferAsync = true,
        .mustPreserveOrder = false
    });

    renderGraph.nodes = {
        {
            .name = "gbuffer",
            .is2D = false,
            .passInfo = gbufferPass.get(),
            .shaderNames = { "gbuffer" },
            .lane = generalGraphicsLane,
            .customRenderFunc = [](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                renderer->getEntityManager()->renderEntities(cmd, frame);
            },
            .skipCondition = [](Renderer* renderer) {
                auto hasRenderable3D = [&](auto& self, const std::vector<Entity*>& nodes) -> bool {
                    for (const Entity* e : nodes) {
                        const std::string& shaderName = e->getShader();
                        const bool isGBufferShader = shaderName.empty() || shaderName == "gbuffer";
                        if (e->getModel() && isGBufferShader && e->isVisible()) return true;
                        if (self(self, e->getChildren())) return true;
                    }
                    return false;
                };
                return !hasRenderable3D(hasRenderable3D, renderer->getEntityManager()->getRootEntities());
            }
        },
        {
            .name = "particle",
            .is2D = true,
            .passInfo = particlePass.get(),
            .shaderNames = { "particle" },
            .dependsOnNodeNames = { "gbuffer" },
            .lane = generalGraphicsLane,
            .customRenderFunc = [](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                renderer->getParticleManager()->renderParticles(cmd, frame);
            }
        },
        {
            .name = "volumetric",
            .is2D = true,
            .passInfo = volumetricPass.get(),
            .shaderNames = { "volumetric" },
            .dependsOnNodeNames = { "gbuffer" },
            .lane = volumetricLane,
            .customRenderFunc = [](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                renderer->getVolumetricManager()->renderVolumetrics(cmd, frame);
            }
        },
        {
            .name = "shadow_prep",
            .is2D = false,
            .passInfo = shadowPass.get(),
            .dependsOnNodeNames = { "gbuffer" },
            .lane = shadowLane,
            .usesRendering = false,
            .usePassManagedTransitions = false,
            .customRenderFunc = [](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                renderer->getLightManager()->renderShadows(cmd, frame);
                renderer->getLightManager()->updateLightsUBO(frame);
            }
        },
        {
            .name = "shadow_image",
            .is2D = false,
            .passInfo = shadowImagePass.get(),
            .shaderNames = { "shadowimage" },
            .dependsOnNodeNames = { "shadow_prep", "gbuffer", "irradiance_dynamic_render" },
            .lane = shadowLane,
            .usesRendering = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .skipCondition = [](Renderer* renderer) {
                auto hasRenderable3D = [&](auto& self, const std::vector<Entity*>& nodes) -> bool {
                    for (const Entity* e : nodes) {
                        const std::string& shaderName = e->getShader();
                        const bool isGBufferShader = shaderName.empty() || shaderName == "gbuffer";
                        if (e->getModel() && isGBufferShader && e->isVisible()) return true;
                        if (self(self, e->getChildren())) return true;
                    }
                    return false;
                };
                return !hasRenderable3D(hasRenderable3D, renderer->getEntityManager()->getRootEntities());
            }
        },
        {
            .name = "shadow_blur_h",
            .is2D = true,
            .passInfo = shadowImageBlurPassH.get(),
            .shaderNames = { "shadowimageblurh" },
            .dependsOnNodeNames = { "shadow_image" },
            .lane = shadowLane,
            .usesRendering = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        },
        {
            .name = "shadow_blur_v",
            .is2D = true,
            .passInfo = shadowImageBlurPassV.get(),
            .shaderNames = { "shadowimageblurv" },
            .dependsOnNodeNames = { "shadow_blur_h" },
            .lane = shadowLane,
            .usesRendering = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT
        },
        {
            .name = "irradiance_dynamic_prepare",
            .is2D = false,
            .passInfo = irradiancePass.get(),
            .dependsOnNodeNames = { "gbuffer", "volumetric" },
            .lane = irradianceRenderLane,
            .usesRendering = false,
            .canRunCustomOnComputeQueue = true,
            .usePassManagedTransitions = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .customRenderFunc = [](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                renderer->getIrradianceManager()->prepareDynamicIrradianceCompute(cmd, frame);
            }
        },
        {
            .name = "irradiance_dynamic_render",
            .is2D = false,
            .passInfo = irradiancePass.get(),
            .shaderNames = { "particlesimple" },
            .dependsOnNodeNames = { "irradiance_dynamic_prepare" },
            .lane = irradianceRenderLane,
            .usesRendering = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        },
        {
            .name = "irradiance_dynamic_finalize",
            .is2D = false,
            .passInfo = irradiancePass.get(),
            .dependsOnNodeNames = { "irradiance_dynamic_render" },
            .lane = irradianceRenderLane,
            .usesRendering = false,
            .canRunCustomOnComputeQueue = true,
            .usePassManagedTransitions = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .customRenderFunc = [](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                renderer->getIrradianceManager()->finalizeDynamicIrradianceCompute(cmd, frame);
                renderer->getIrradianceManager()->updateIrradianceProbesUBO(frame);
            }
        },
        {
            .name = "irradiance_dynamic_sh",
            .is2D = false,
            .passInfo = irradiancePass.get(),
            .shaderNames = { "sh" },
            .dependsOnNodeNames = { "irradiance_dynamic_finalize" },
            .lane = irradianceSHLane,
            .usesRendering = false,
            .usePassManagedTransitions = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        },
        {
            .name = "irradiance_dynamic_sh_reduce",
            .is2D = false,
            .passInfo = irradiancePass.get(),
            .dependsOnNodeNames = { "irradiance_dynamic_sh" },
            .lane = irradianceSHLane,
            .usesRendering = false,
            .canRunCustomOnComputeQueue = true,
            .usePassManagedTransitions = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .customRenderFunc = [](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                renderer->getIrradianceManager()->dispatchDynamicIrradianceSHReduce(cmd, frame);
            }
        },
        {
            .name = "lighting",
            .is2D = true,
            .passInfo = lightingPass.get(),
            .shaderNames = { "lighting" },
            .dependsOnNodeNames = { "irradiance_dynamic_sh_reduce", "shadow_blur_v", "volumetric", "particle" },
            .lane = generalGraphicsLane,
            .skipCondition = [](Renderer* renderer) {
                auto hasRenderable3D = [&](auto& self, const std::vector<Entity*>& nodes) -> bool {
                    for (const Entity* e : nodes) {
                        const std::string& shaderName = e->getShader();
                        const bool isGBufferShader = shaderName.empty() || shaderName == "gbuffer";
                        if (e->getModel() && isGBufferShader && e->isVisible()) return true;
                        if (self(self, e->getChildren())) return true;
                    }
                    return false;
                };
                return !hasRenderable3D(hasRenderable3D, renderer->getEntityManager()->getRootEntities());
            }
        },
        {
            .name = "ssr",
            .is2D = true,
            .passInfo = ssrPass.get(),
            .shaderNames = { "ssr" },
            .dependsOnNodeNames = { "lighting" },
            .lane = generalGraphicsLane,
            .skipCondition = [](Renderer* renderer) {
                return !renderer->getSettingsManager()->getSettings()->ssrEnabled;
            }
        },
        {
            .name = "ao",
            .is2D = true,
            .passInfo = aoPass.get(),
            .shaderNames = { "ao" },
            .dependsOnNodeNames = { "gbuffer" },
            .lane = generalGraphicsLane,
            .usesRendering = false,
            .storageWriteStage = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        },
        {
            .name = "bloom",
            .is2D = true,
            .passInfo = bloomPass.get(),
            .shaderNames = { "bloom" },
            .dependsOnNodeNames = { "lighting" },
            .lane = generalGraphicsLane,
        },
        {
            .name = "bloom_blur_h",
            .is2D = true,
            .passInfo = bloomBlurPassH.get(),
            .shaderNames = { "bloomblurh" },
            .dependsOnNodeNames = { "bloom" },
            .lane = generalGraphicsLane,
        },
        {
            .name = "bloom_blur_v",
            .is2D = true,
            .passInfo = bloomBlurPassV.get(),
            .shaderNames = { "bloomblurv" },
            .dependsOnNodeNames = { "bloom_blur_h" },
            .lane = generalGraphicsLane,
        },
        {
            .name = "combine",
            .is2D = true,
            .passInfo = combinePass.get(),
            .shaderNames = { "combine" },
            .dependsOnNodeNames = { "lighting", "ssr", "ao", "bloom_blur_v" },
            .lane = generalGraphicsLane,
        },
        {
            .name = "smaa_edge",
            .is2D = true,
            .passInfo = smaaEdgePass.get(),
            .shaderNames = { "smaaEdge" },
            .dependsOnNodeNames = { "combine" },
            .lane = generalGraphicsLane,
            .skipCondition = [](Renderer* renderer) {
                return renderer->getSettingsManager()->getSettings()->aaMode != 2;
            }
        },
        {
            .name = "smaa_weight",
            .is2D = true,
            .passInfo = smaaWeightPass.get(),
            .shaderNames = { "smaaWeight" },
            .dependsOnNodeNames = { "smaa_edge" },
            .lane = generalGraphicsLane,
            .skipCondition = [](Renderer* renderer) {
                return renderer->getSettingsManager()->getSettings()->aaMode != 2;
            }
        },
        {
            .name = "smaa_blend",
            .is2D = true,
            .passInfo = smaaBlendPass.get(),
            .shaderNames = { "smaaBlend" },
            .dependsOnNodeNames = { "smaa_weight" },
            .lane = generalGraphicsLane,
            .skipCondition = [](Renderer* renderer) {
                return renderer->getSettingsManager()->getSettings()->aaMode != 2;
            }
        },
        {
            .name = "ui",
            .is2D = true,
            .passInfo = uiPass.get(),
            .shaderNames = { "ui", "text" },
            .lane = generalGraphicsLane,
            .customRenderFunc = [this](Renderer* renderer, VkCommandBuffer cmd, uint32_t frame) {
                if (renderer->getSettingsManager()->getSettings()->showFPS) {
                    if (!renderer->getFPSCounter()) {
                        renderer->setFPSCounter(new TextObject(
                            renderer->getUIManager(),
                            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 0.4f, 1.0f)), glm::vec3(15.0f, -15.0f, 0.0f)),
                            "fpsCounter",
                            glm::vec4(1.0f),
                            "0 FPS",
                            "Lato",
                            Corner::TopLeft
                        ));
                    }
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration<float>(now - renderer->getLastFPSUpdateTime()).count();
                    if (elapsed >= 0.5f) {
                        uint32_t averageFPS = static_cast<uint32_t>(static_cast<float>(renderer->getFPSFrameCount()) / elapsed);
                        std::string fps = std::to_string(averageFPS) + " FPS";
                        renderer->getFPSCounter()->setText(fps);
                        renderer->setLastFPSUpdateTime(now);
                        renderer->setFPSFrameCount(0);
                    }
                } else {
                    if (renderer->getFPSCounter()) {
                        renderer->getUIManager()->removeObject(renderer->getFPSCounter()->getName());
                        renderer->setFPSCounter(nullptr);
                    }
                }
                renderer->getUIManager()->renderUI(cmd, frame);
            }
        },
        {
            .name = "composite",
            .is2D = true,
            .passInfo = mainPass.get(),
            .shaderNames = { "composite" },
            .dependsOnNodeNames = { "combine", "ui", "smaa_blend" },
            .lane = generalGraphicsLane,
        }
    };
}

void engine::ShaderManager::loadSMAATextures() {
    auto createSMAATexture = [&](const std::string& name, const unsigned char* data, int width, int height, VkDeviceSize pixelSize, VkFormat format) {
        unsigned char* pixels = const_cast<unsigned char*>(data);
        VkImage image;
        VkDeviceMemory memory;
        std::tie(image, memory) = renderer->createImageFromPixels(
            (void*) pixels,
            pixelSize,
            width, height,
            1, VK_SAMPLE_COUNT_1_BIT, format,
            VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1, 0
        );
        renderer->transitionImageLayout(
            image,
            format,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            1, 1
        );
        VkImageView imageView = renderer->createImageView(image, format);
        Texture texture = {
            .image = image,
            .imageView = imageView,
            .imageMemory = memory,
            .format = format,
            .width = width,
            .height = height
        };
        renderer->getTextureManager()->registerTexture(name, texture);
    };
    createSMAATexture("smaa_area", areaTexBytes, AREATEX_WIDTH, AREATEX_HEIGHT, AREATEX_SIZE, VK_FORMAT_R8G8_UNORM);
    createSMAATexture("smaa_search", searchTexBytes, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, SEARCHTEX_SIZE, VK_FORMAT_R8_UNORM);
}

std::vector<engine::RenderNode>& engine::ShaderManager::getRenderGraph() {
    return renderGraph.nodes;
}

const std::vector<engine::RenderNode>& engine::ShaderManager::getRenderGraph() const {
    return renderGraph.nodes;
}

const std::vector<size_t>& engine::ShaderManager::getScheduledNodeOrder() const {
    return renderGraph.scheduledNodeOrder;
}

const std::vector<std::shared_ptr<engine::PassInfo>>& engine::ShaderManager::getRenderPasses() const {
    return renderPasses;
}

void engine::ShaderManager::resolveRenderGraphShaders() {
    auto& nodes = renderGraph.nodes;
    renderGraph.scheduledNodeOrder.clear();

    for (auto& node : nodes) {
        node.shaders.clear();
        node.computeShaders.clear();
        node.resolvedDependencies.clear();
        for (const auto& shaderName : node.shaderNames) {
            bool found = false;
            auto it = graphicsShaderMap.find(shaderName);
            if (it != graphicsShaderMap.end()) {
                node.shaders.insert(it->second);
                if (!node.passInfo && it->second->config.passInfo) {
                    node.passInfo = it->second->config.passInfo.get();
                }
                found = true;
            }
            auto computeIt = computeShaderMap.find(shaderName);
            if (computeIt != computeShaderMap.end()) {
                node.computeShaders.insert(computeIt->second);
                found = true;
            }
            if (!found) {
                std::cout << "Warning: Render graph shader '" << shaderName << "' not found.\n";
            }
        }
    }

    const size_t nodeCount = nodes.size();
    if (nodeCount == 0) {
        return;
    }

    std::unordered_map<std::string, size_t> nodeNameToIndex;
    for (size_t idx = 0; idx < nodeCount; ++idx) {
        const RenderNode& node = nodes[idx];
        if (!node.name.empty()) {
            auto inserted = nodeNameToIndex.emplace(node.name, idx);
            if (!inserted.second) {
                std::cout << "Warning: Duplicate render node name '" << node.name << "'. Using first occurrence for dependency resolution.\n";
            }
        }
    }

    std::vector<std::unordered_set<size_t>> dependencies(nodeCount);
    auto addDependency = [&](size_t consumerIdx, size_t producerIdx) {
        if (consumerIdx == producerIdx) {
            return;
        }
        dependencies[consumerIdx].insert(producerIdx);
    };

    for (size_t idx = 0; idx < nodeCount; ++idx) {
        const RenderNode& node = nodes[idx];
        for (const auto& depName : node.dependsOnNodeNames) {
            auto depIt = nodeNameToIndex.find(depName);
            if (depIt == nodeNameToIndex.end()) {
                std::cout << "Warning: Render node '" << node.name << "' depends on unknown node '" << depName << "'.\n";
                continue;
            }
            addDependency(idx, depIt->second);
        }
    }

    std::vector<std::vector<size_t>> dependents(nodeCount);
    std::vector<size_t> indegree(nodeCount, 0);
    for (size_t consumerIdx = 0; consumerIdx < nodeCount; ++consumerIdx) {
        indegree[consumerIdx] = dependencies[consumerIdx].size();
        for (size_t producerIdx : dependencies[consumerIdx]) {
            dependents[producerIdx].push_back(consumerIdx);
        }
        nodes[consumerIdx].resolvedDependencies.assign(dependencies[consumerIdx].begin(), dependencies[consumerIdx].end());
        std::sort(nodes[consumerIdx].resolvedDependencies.begin(), nodes[consumerIdx].resolvedDependencies.end());
    }

    std::vector<size_t> ready;
    ready.reserve(nodeCount);
    for (size_t idx = 0; idx < nodeCount; ++idx) {
        if (indegree[idx] == 0) {
            ready.push_back(idx);
        }
    }

    std::vector<size_t> scheduledOrder;
    scheduledOrder.reserve(nodeCount);
    auto pickNextReady = [&](const std::vector<size_t>& candidates) {
        return *std::min_element(
            candidates.begin(),
            candidates.end(),
            [&](size_t lhs, size_t rhs) {
                const bool lhsPreferAsync = nodes[lhs].lane && nodes[lhs].lane->preferAsync;
                const bool rhsPreferAsync = nodes[rhs].lane && nodes[rhs].lane->preferAsync;
                if (lhsPreferAsync != rhsPreferAsync) {
                    return lhsPreferAsync && !rhsPreferAsync;
                }
                return lhs < rhs;
            }
        );
    };

    while (!ready.empty()) {
        size_t nextNode = pickNextReady(ready);
        ready.erase(std::find(ready.begin(), ready.end(), nextNode));
        scheduledOrder.push_back(nextNode);
        for (size_t dependentIdx : dependents[nextNode]) {
            if (indegree[dependentIdx] == 0) {
                continue;
            }
            indegree[dependentIdx]--;
            if (indegree[dependentIdx] == 0) {
                ready.push_back(dependentIdx);
            }
        }
    }

    if (scheduledOrder.size() != nodeCount) {
        std::cout << "Warning: Render graph dependency cycle detected. Falling back to declaration order for scheduling.\n";
        renderGraph.scheduledNodeOrder.resize(nodeCount);
        for (size_t idx = 0; idx < nodeCount; ++idx) {
            renderGraph.scheduledNodeOrder[idx] = idx;
        }
        return;
    }

    renderGraph.scheduledNodeOrder = std::move(scheduledOrder);
}

void engine::GraphicsShader::updateDescriptorSets(Renderer* renderer, std::vector<VkDescriptorSet>& descriptorSets, std::vector<Texture*>& textures, std::vector<VkBuffer>& buffers, int frameIndex) {
    int MAX_FRAMES_IN_FLIGHT = renderer->getMaxFramesInFlight();
    VkDevice device = renderer->getDevice();
    const size_t vertexBindings = static_cast<size_t>(std::max(config.vertexBitBindings, 0));
    const size_t fragmentBindings = static_cast<size_t>(std::max(config.fragmentBitBindings, 0));
    auto getVertexType = [&](size_t index) {
        if (!config.vertexDescriptorTypes.empty() && index < config.vertexDescriptorTypes.size()) {
            return config.vertexDescriptorTypes[index];
        }
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    };
    auto getVertexCount = [&](size_t index) {
        if (!config.vertexDescriptorCounts.empty() && config.vertexDescriptorCounts.size() == vertexBindings) {
            return std::max(config.vertexDescriptorCounts[index], 1u);
        }
        return 1u;
    };
    auto getFragmentType = [&](int index) {
        if (!config.fragmentDescriptorTypes.empty() && static_cast<size_t>(index) < config.fragmentDescriptorTypes.size()) {
            return config.fragmentDescriptorTypes[static_cast<size_t>(index)];
        }
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    };
    auto getFragmentCount = [&](size_t index) {
        if (!config.fragmentDescriptorCounts.empty() && config.fragmentDescriptorCounts.size() == fragmentBindings) {
            return std::max(config.fragmentDescriptorCounts[index], 1u);
        }
        return 1u;
    };
    auto isInputBinding = [&](uint32_t bindingIndex) {
        for (const auto& ib : config.inputBindings) {
            if (ib.binding == bindingIndex) return true;
        }
        return false;
    };
    int startFrame = (frameIndex >= 0) ? frameIndex : 0;
    int endFrame = (frameIndex >= 0) ? frameIndex + 1 : MAX_FRAMES_IN_FLIGHT;
    for (int frame = startFrame; frame < endFrame; ++frame) {
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        imageInfos.reserve(fragmentBindings + config.inputBindings.size());
        bufferInfos.reserve(vertexBindings);
        descriptorWrites.reserve(vertexBindings + fragmentBindings);
        
        size_t bufferIndex = 0;
        for (size_t binding = 0; binding < vertexBindings; ++binding) {
            const VkDescriptorType type = getVertexType(binding);
            const uint32_t descriptorCount = getVertexCount(binding);
            if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                for (uint32_t c = 0; c < descriptorCount; ++c) {
                    const size_t idx = static_cast<size_t>(frame) * (buffers.size() / static_cast<size_t>(MAX_FRAMES_IN_FLIGHT)) + bufferIndex++;
                    VkBuffer bufferHandle = buffers[idx];
                    if (bufferHandle == VK_NULL_HANDLE) {
                        throw std::runtime_error("Invalid buffer handle provided for descriptor set update!");
                    }
                    bufferInfos.push_back({
                        .buffer = bufferHandle,
                        .offset = 0,
                        .range = VK_WHOLE_SIZE
                    });
                }
                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[frame],
                    .dstBinding = static_cast<uint32_t>(binding),
                    .dstArrayElement = 0,
                    .descriptorCount = descriptorCount,
                    .descriptorType = type,
                    .pBufferInfo = &bufferInfos[bufferInfos.size() - descriptorCount]
                });
            }
        }

        size_t textureIndex = 0;
        for (size_t binding = 0; binding < fragmentBindings; ++binding) {
            const uint32_t actualBinding = static_cast<uint32_t>(vertexBindings + binding);
            if (isInputBinding(actualBinding)) continue;
            const VkDescriptorType type = getFragmentType(binding);
            const uint32_t descriptorCount = getFragmentCount(binding);
            if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                for (uint32_t c = 0; c < descriptorCount; ++c) {
                    Texture* texture = textures[textureIndex++];
                    if (!texture) {
                        throw std::runtime_error("Invalid texture provided for descriptor set update!");
                    }
                    imageInfos.push_back({
                        .sampler = (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? renderer->getMainTextureSampler() : VK_NULL_HANDLE,
                        .imageView = texture->imageView,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    });
                }
                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[frame],
                    .dstBinding = actualBinding,
                    .dstArrayElement = 0,
                    .descriptorCount = descriptorCount,
                    .descriptorType = type,
                    .pImageInfo = &imageInfos[imageInfos.size() - descriptorCount]
                });
            }
        }
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

std::vector<VkDescriptorSet> engine::GraphicsShader::createDescriptorSets(Renderer* renderer, std::vector<Texture*>& textures, std::vector<VkBuffer>& buffers) {
    int MAX_FRAMES_IN_FLIGHT = renderer->getMaxFramesInFlight();
    VkDevice device = renderer->getDevice();
    VkSampler mainTextureSampler = renderer->getMainTextureSampler();
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
        .pSetLayouts = layouts.data()
    };
    std::vector<VkDescriptorSet> descriptorSets(MAX_FRAMES_IN_FLIGHT);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor sets!");
    }
    const size_t vertexBindings = static_cast<size_t>(std::max(config.vertexBitBindings, 0));
    const size_t fragmentBindings = static_cast<size_t>(std::max(config.fragmentBitBindings, 0));
    auto getVertexType = [&](size_t index) {
        if (!config.vertexDescriptorTypes.empty() && index < config.vertexDescriptorTypes.size()) {
            return config.vertexDescriptorTypes[index];
        }
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    };
    auto getVertexCount = [&](size_t index) {
        if (!config.vertexDescriptorCounts.empty() && config.vertexDescriptorCounts.size() == vertexBindings) {
            return std::max(config.vertexDescriptorCounts[index], 1u);
        }
        return 1u;
    };
    size_t expectedBuffers = 0;
    for (size_t i = 0; i < vertexBindings; ++i) {
        VkDescriptorType type = getVertexType(i);
        if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
            expectedBuffers += getVertexCount(i);
        }
    }
    expectedBuffers *= MAX_FRAMES_IN_FLIGHT;
    if (buffers.size() < expectedBuffers && expectedBuffers > 0) {
        throw std::runtime_error("Insufficient buffers for descriptor set creation!");
    }

    auto getFragmentType = [&](int index) {
        if (!config.fragmentDescriptorTypes.empty() && static_cast<size_t>(index) < config.fragmentDescriptorTypes.size()) {
            return config.fragmentDescriptorTypes[static_cast<size_t>(index)];
        }
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    };
    auto getFragmentCount = [&](size_t index) {
        if (!config.fragmentDescriptorCounts.empty() && config.fragmentDescriptorCounts.size() == fragmentBindings) {
            return std::max(config.fragmentDescriptorCounts[index], 1u);
        }
        return 1u;
    };
    auto isInputBinding = [&](uint32_t bindingIndex) {
        for (const auto& ib : config.inputBindings) {
            if (ib.binding == bindingIndex) return true;
        }
        return false;
    };

    size_t requiredTextureBindings = 0;
    for (size_t i = 0; i < fragmentBindings; ++i) {
        const uint32_t actualBinding = static_cast<uint32_t>(vertexBindings + i);
        if (isInputBinding(actualBinding)) continue;
        const VkDescriptorType type = getFragmentType(i);
        if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
            requiredTextureBindings += getFragmentCount(i);
        }
    }
    if (textures.size() < requiredTextureBindings) {
        throw std::runtime_error("Insufficient textures for descriptor set creation!");
    }

    size_t inputBindingCount = config.inputBindings.size();    
    for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        std::vector<VkDescriptorImageInfo> imageInfos;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        std::vector<VkWriteDescriptorSet> descriptorWrites;
        imageInfos.reserve(requiredTextureBindings + fragmentBindings + inputBindingCount);
        bufferInfos.reserve(expectedBuffers / MAX_FRAMES_IN_FLIGHT);
        descriptorWrites.reserve(static_cast<size_t>(vertexBindings + fragmentBindings));
        
        size_t bufferIndex = 0;
        for (size_t binding = 0; binding < vertexBindings; ++binding) {
            const VkDescriptorType type = getVertexType(binding);
            const uint32_t descriptorCount = getVertexCount(binding);
            if (type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                for (uint32_t c = 0; c < descriptorCount; ++c) {
                    const size_t idx = static_cast<size_t>(frame) * (expectedBuffers / static_cast<size_t>(MAX_FRAMES_IN_FLIGHT)) + bufferIndex++;
                    VkBuffer bufferHandle = buffers[idx];
                    if (bufferHandle == VK_NULL_HANDLE) {
                        throw std::runtime_error("Invalid buffer handle provided for descriptor set creation!");
                    }
                    bufferInfos.push_back({
                        .buffer = bufferHandle,
                        .offset = 0,
                        .range = VK_WHOLE_SIZE
                    });
                }
                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[frame],
                    .dstBinding = static_cast<uint32_t>(binding),
                    .dstArrayElement = 0,
                    .descriptorCount = descriptorCount,
                    .descriptorType = type,
                    .pBufferInfo = &bufferInfos[bufferInfos.size() - descriptorCount]
                });
            }
        }

        size_t textureIndex = 0;
        for (size_t frag = 0; frag < fragmentBindings; ++frag) {
            const VkDescriptorType type = getFragmentType(frag);
            const uint32_t descriptorCount = getFragmentCount(frag);
            const uint32_t bindingIndex = static_cast<uint32_t>(vertexBindings + frag);
            const GraphicsShader::Config::InputBinding* inputBinding = nullptr;
            for (const auto& ib : config.inputBindings) {
                if (ib.binding == bindingIndex) {
                    inputBinding = &ib;
                    break;
                }
            }
            size_t startIndex = imageInfos.size();
            if (inputBinding) {
                VkImageView imageView = renderer->getPassImageView(inputBinding->sourceShaderName, inputBinding->attachmentName);
                if (imageView == VK_NULL_HANDLE) {
                    throw std::runtime_error("Failed to resolve inputBinding: shader='" + inputBinding->sourceShaderName + 
                                           "' attachment='" + inputBinding->attachmentName + "' for binding " + std::to_string(bindingIndex));
                }
                
                imageInfos.push_back({
                    .sampler = VK_NULL_HANDLE,
                    .imageView = imageView,
                    .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                });
                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = descriptorSets[frame],
                    .dstBinding = bindingIndex,
                    .dstArrayElement = 0,
                    .descriptorCount = 1,
                    .descriptorType = type,
                    .pImageInfo = &imageInfos[startIndex]
                });
                continue;
            }
            
            switch (type) {
                case VK_DESCRIPTOR_TYPE_SAMPLER: {
                    VkSampler sampler = mainTextureSampler;
                    if (!textures.empty() && textures.front() && textures.front()->imageSampler != VK_NULL_HANDLE) {
                        sampler = textures.front()->imageSampler;
                    }
                    for (uint32_t c = 0; c < descriptorCount; ++c) {
                        imageInfos.push_back({
                            .sampler = sampler
                        });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = descriptorSets[frame],
                        .dstBinding = bindingIndex,
                        .dstArrayElement = 0,
                        .descriptorCount = descriptorCount,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    break;
                }
                case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
                    for (uint32_t c = 0; c < descriptorCount; ++c) {
                        Texture* texture = textures.at(textureIndex++);
                        if (!texture || !texture->imageView) {
                            throw std::runtime_error("Invalid texture provided for sampled image descriptor!");
                        }
                        imageInfos.push_back({
                            .sampler = VK_NULL_HANDLE,
                            .imageView = texture->imageView,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = descriptorSets[frame],
                        .dstBinding = bindingIndex,
                        .dstArrayElement = 0,
                        .descriptorCount = descriptorCount,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    break;
                }
                default: {
                    for (uint32_t c = 0; c < descriptorCount; ++c) {
                        Texture* texture = textures.at(textureIndex++);
                        if (!texture || !texture->imageView) {
                            throw std::runtime_error("Invalid texture provided for combined image sampler descriptor!");
                        }
                        imageInfos.push_back({
                            .sampler = texture->imageSampler,
                            .imageView = texture->imageView,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = descriptorSets[frame],
                        .dstBinding = bindingIndex,
                        .dstArrayElement = 0,
                        .descriptorCount = descriptorCount,
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    break;
                }
            }
        }
        if (!descriptorWrites.empty()) {
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
    }

    return descriptorSets;
}

void engine::GraphicsShader::createDescriptorSetLayout(engine::Renderer* renderer) {
    const int totalVertexBindings = std::max(config.vertexBitBindings, 0);
    const int totalFragmentBindings = std::max(config.fragmentBitBindings, 0);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(static_cast<size_t>(totalVertexBindings + totalFragmentBindings));
    VkShaderStageFlags uboStageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    auto getVertexType = [&](int index) {
        if (!config.vertexDescriptorTypes.empty() && static_cast<size_t>(index) < config.vertexDescriptorTypes.size()) {
            return config.vertexDescriptorTypes[static_cast<size_t>(index)];
        }
        return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    };
    auto getVertexCount = [&](int index) {
        if (!config.vertexDescriptorCounts.empty() && config.vertexDescriptorCounts.size() == static_cast<size_t>(totalVertexBindings)) {
            return std::max(config.vertexDescriptorCounts[static_cast<size_t>(index)], 1u);
        }
        return 1u;
    };
    for (int bindingIndex = 0; bindingIndex < totalVertexBindings; ++bindingIndex) {
        const VkDescriptorType descriptorType = getVertexType(bindingIndex);
        const uint32_t descriptorCount = getVertexCount(bindingIndex);
        VkDescriptorSetLayoutBinding vertexLayoutBinding = {
            .binding = static_cast<uint32_t>(bindingIndex),
            .descriptorType = descriptorType,
            .descriptorCount = descriptorCount,
            .stageFlags = uboStageFlags,
            .pImmutableSamplers = nullptr
        };
        bindings.push_back(vertexLayoutBinding);
    }
    auto getFragmentType = [&](int index) {
        if (!config.fragmentDescriptorTypes.empty() && static_cast<size_t>(index) < config.fragmentDescriptorTypes.size()) {
            return config.fragmentDescriptorTypes[static_cast<size_t>(index)];
        }
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    };
    auto getFragmentCount = [&](int index) {
        if (!config.fragmentDescriptorCounts.empty() && config.fragmentDescriptorCounts.size() == static_cast<size_t>(totalFragmentBindings)) {
            return std::max(config.fragmentDescriptorCounts[static_cast<size_t>(index)], 1u);
        }
        return 1u;
    };
    for (int offset = 0; offset < totalFragmentBindings; ++offset) {
        const VkDescriptorType descriptorType = getFragmentType(offset);
        const uint32_t descriptorCount = getFragmentCount(offset);
        VkDescriptorSetLayoutBinding fragmentLayoutBinding = {
            .binding = static_cast<uint32_t>(totalVertexBindings + offset),
            .descriptorType = descriptorType,
            .descriptorCount = descriptorCount,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
            .pImmutableSamplers = nullptr
        };
        bindings.push_back(fragmentLayoutBinding);
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };
    if (vkCreateDescriptorSetLayout(renderer->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void engine::ComputeShader::createDescriptorSetLayout(engine::Renderer* renderer) {
    const int totalStorageImageBindings = std::max(config.storageImageCount, 0);
    const int totalStorageBufferBindings = std::max(config.storageBufferCount, 0);
    const int totalComputeBindings = std::max(config.computeBitBindings, 0);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    if (!config.computeDescriptorTypes.empty()) {
        bindings.reserve(static_cast<size_t>(totalComputeBindings));
    } else {
        bindings.reserve(static_cast<size_t>(totalStorageImageBindings + totalStorageBufferBindings + totalComputeBindings));
    }
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t currentBinding = 0;
    if (!config.computeDescriptorTypes.empty()) {
        auto getComputeType = [&](int index) {
            if (static_cast<size_t>(index) < config.computeDescriptorTypes.size()) {
                return config.computeDescriptorTypes[static_cast<size_t>(index)];
            }
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        };
        auto getComputeCount = [&](int index) {
            if (!config.computeDescriptorCounts.empty() && config.computeDescriptorCounts.size() == static_cast<size_t>(totalComputeBindings)) {
                return std::max(config.computeDescriptorCounts[static_cast<size_t>(index)], 1u);
            }
            return 1u;
        };
        for (int i = 0; i < totalComputeBindings; ++i) {
            VkDescriptorSetLayoutBinding computeLayoutBinding = {
                .binding = currentBinding++,
                .descriptorType = getComputeType(i),
                .descriptorCount = getComputeCount(i),
                .stageFlags = stageFlags,
                .pImmutableSamplers = nullptr
            };
            bindings.push_back(computeLayoutBinding);
        }
    } else {
        for (int i = 0; i < config.storageImageCount; ++i) {
            VkDescriptorSetLayoutBinding storageImageBinding = {
                .binding = currentBinding++,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = 1,
                .stageFlags = stageFlags,
                .pImmutableSamplers = nullptr
            };
            bindings.push_back(storageImageBinding);
        }
        for (int i = 0; i < config.storageBufferCount; ++i) {
            VkDescriptorSetLayoutBinding storageBufferBinding = {
                .binding = currentBinding++,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = 1,
                .stageFlags = stageFlags,
                .pImmutableSamplers = nullptr
            };
            bindings.push_back(storageBufferBinding);
        }
        for (int offset = 0; offset < config.computeBitBindings; ++offset) {
            uint32_t descriptorCount = 1;
            VkDescriptorSetLayoutBinding computeLayoutBinding = {
                .binding = currentBinding++,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = descriptorCount,
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .pImmutableSamplers = nullptr
            };
            bindings.push_back(computeLayoutBinding);
        }
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = static_cast<uint32_t>(bindings.size()),
        .pBindings = bindings.data()
    };
    if (vkCreateDescriptorSetLayout(renderer->getDevice(), &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }
}

void engine::GraphicsShader::createPipeline(engine::Renderer* renderer) {
    VkDevice device = renderer->getDevice();
    std::vector<char> vertShaderCode = renderer->getShaderManager()->getShaderBytes(vertex.path);
    VkShaderModule vertShaderModule = ShaderManager::createShaderModule(vertShaderCode, renderer);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vertShaderModule,
        .pName = "main"
    };

    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    shaderStages.push_back(vertShaderStageInfo);

    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    if (!fragment.path.empty()) {
        std::vector<char> fragShaderCode = renderer->getShaderManager()->getShaderBytes(fragment.path);
        fragShaderModule = ShaderManager::createShaderModule(fragShaderCode, renderer);
        VkPipelineShaderStageCreateInfo fragShaderStageInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragShaderModule,
            .pName = "main"
        };
        shaderStages.push_back(fragShaderStageInfo);
    }

    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data()
    };
    std::vector<VkVertexInputBindingDescription> bindingDescriptions;
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    if (config.getVertexInputDescriptions) {
        config.getVertexInputDescriptions(bindingDescriptions, attributeDescriptions);
    }
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = static_cast<uint32_t>(bindingDescriptions.size()),
        .pVertexBindingDescriptions = bindingDescriptions.empty() ? nullptr : bindingDescriptions.data(),
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.empty() ? nullptr : attributeDescriptions.data()
    };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE
    };
    VkExtent2D swapChainExtent = renderer->getSwapChainExtent();
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(swapChainExtent.width),
        .height = static_cast<float>(swapChainExtent.height),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    VkRect2D scissor = {
        .offset = {0, 0},
        .extent = swapChainExtent
    };
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1u,
        .pViewports = &viewport,
        .scissorCount = 1u,
        .pScissors = &scissor
    };
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = config.cullMode,
        .frontFace = config.frontFace,
        .depthBiasEnable = VK_FALSE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = config.sampleCount,
        .sampleShadingEnable = (config.sampleCount != VK_SAMPLE_COUNT_1_BIT) ? VK_TRUE : VK_FALSE,
        .minSampleShading = 0.2f,
        .pSampleMask = nullptr,
        .alphaToCoverageEnable = VK_FALSE,
        .alphaToOneEnable = VK_FALSE
    };
    VkPipelineColorBlendAttachmentState colorBlendAttachment = {
        .blendEnable = config.blendEnable ? VK_TRUE : VK_FALSE,
        .srcColorBlendFactor = config.blendAdditive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = config.blendAdditive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = config.blendAdditive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
    };
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
    if (!config.colorBlendOverrides.empty()) {
        if (static_cast<int>(config.colorBlendOverrides.size()) != config.colorAttachmentCount) {
            throw std::runtime_error("colorBlendOverrides size must equal colorAttachmentCount");
        }
        colorBlendAttachments = config.colorBlendOverrides;
    } else {
        colorBlendAttachments.assign(config.colorAttachmentCount, colorBlendAttachment);
    }
    VkPipelineColorBlendStateCreateInfo colorBlending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .logicOp = VK_LOGIC_OP_COPY,
        .attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size()),
        .pAttachments = colorBlendAttachments.data(),
        .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = config.enableDepth ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = (config.enableDepth && config.depthWrite) ? VK_TRUE : VK_FALSE,
        .depthCompareOp = config.depthCompare,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {},
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f
    };
    const bool hasPushConstant = config.pushConstantRange.stageFlags != 0;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &descriptorSetLayout,
        .pushConstantRangeCount = hasPushConstant ? 1u : 0u,
        .pPushConstantRanges = hasPushConstant ? &config.pushConstantRange : nullptr
    };
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout!");
    }
    VkPipelineRenderingCreateInfo pipelineRenderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .viewMask = config.viewMask,
        .colorAttachmentCount = static_cast<uint32_t>(config.colorAttachmentCount),
        .pColorAttachmentFormats = config.colorAttachmentCount > 0 ? config.passInfo->attachmentFormats.data() : nullptr,
        .depthAttachmentFormat = config.enableDepth ? config.passInfo->depthAttachmentFormat : VK_FORMAT_UNDEFINED,
        .stencilAttachmentFormat = VK_FORMAT_UNDEFINED
    };
    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &pipelineRenderingInfo,
        .stageCount = static_cast<uint32_t>(shaderStages.size()),
        .pStages = shaderStages.data(),
        .pVertexInputState = &vertexInputInfo,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlending,
        .pDynamicState = &dynamicState,
        .layout = pipelineLayout,
        .subpass = 0u,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline!");
    }
    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);
}

void engine::ComputeShader::createPipeline(Renderer* renderer) {
    VkDevice device = renderer->getDevice();
    std::vector<char> compShaderCode = renderer->getShaderManager()->getShaderBytes(compute.path);
    VkShaderModule compShaderModule = ShaderManager::createShaderModule(compShaderCode, renderer);
    VkPipelineShaderStageCreateInfo compShaderStageInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = compShaderModule,
        .pName = "main"
    };
    const bool hasPushConstant = config.pushConstantRange.stageFlags != 0;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1u,
        .pSetLayouts = &descriptorSetLayout,
        .pushConstantRangeCount = hasPushConstant ? 1u : 0u,
        .pPushConstantRanges = hasPushConstant ? &config.pushConstantRange : nullptr
    };
    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline layout!");
    }
    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = compShaderStageInfo,
        .layout = pipelineLayout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1
    };
    if (vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute pipeline!");
    }
    vkDestroyShaderModule(device, compShaderModule, nullptr);
}

VkShaderModule engine::ShaderManager::createShaderModule(const std::vector<char>& code, Renderer* renderer) {
    VkShaderModuleCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code.size(),
        .pCode = reinterpret_cast<const uint32_t*>(code.data())
    };
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(renderer->getDevice(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create shader module!");
    }
    return shaderModule;
}

void engine::GraphicsShader::createDescriptorPool(Renderer* renderer) {
    int MAX_FRAMES_IN_FLIGHT = renderer->getMaxFramesInFlight();
    std::vector<VkDescriptorPoolSize> poolSizes;
    std::unordered_map<VkDescriptorType, uint32_t> typeCounts;
    if (config.vertexBitBindings > 0) {
        auto getVertexType = [&](size_t idx) {
            if (!config.vertexDescriptorTypes.empty() && idx < config.vertexDescriptorTypes.size()) {
                return config.vertexDescriptorTypes[idx];
            }
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        };
        auto getVertexCount = [&](size_t idx) {
            if (!config.vertexDescriptorCounts.empty() && config.vertexDescriptorCounts.size() == static_cast<size_t>(config.vertexBitBindings)) {
                return std::max(config.vertexDescriptorCounts[idx], 1u);
            }
            return 1u;
        };
        for (size_t i = 0; i < static_cast<size_t>(config.vertexBitBindings); ++i) {
            const VkDescriptorType type = getVertexType(i);
            const uint32_t count = getVertexCount(i) * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier;
            typeCounts[type] += count;
        }
    }
    if (config.fragmentBitBindings > 0) {
        auto getFragmentType = [&](size_t idx) {
            if (!config.fragmentDescriptorTypes.empty() && idx < config.fragmentDescriptorTypes.size()) {
                return config.fragmentDescriptorTypes[idx];
            }
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        };
        auto getFragmentCount = [&](size_t idx) {
            if (!config.fragmentDescriptorCounts.empty() && config.fragmentDescriptorCounts.size() == static_cast<size_t>(config.fragmentBitBindings)) {
                return std::max(config.fragmentDescriptorCounts[idx], 1u);
            }
            return 1u;
        };
        for (size_t i = 0; i < static_cast<size_t>(config.fragmentBitBindings); ++i) {
            const VkDescriptorType type = getFragmentType(i);
            const uint32_t count = getFragmentCount(i) * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier;
            typeCounts[type] += count;
        }
    }
    for (const auto& [type, count] : typeCounts) {
        poolSizes.push_back({
            .type = type,
            .descriptorCount = count
        });
    }
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * config.poolMultiplier),
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    if (vkCreateDescriptorPool(renderer->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

void engine::ComputeShader::createDescriptorPool(Renderer* renderer) {
    int MAX_FRAMES_IN_FLIGHT = renderer->getMaxFramesInFlight();
    std::vector<VkDescriptorPoolSize> poolSizes;
    if (!config.computeDescriptorTypes.empty()) {
        std::unordered_map<VkDescriptorType, uint32_t> typeCounts;
        auto getComputeCount = [&](size_t index) {
            if (!config.computeDescriptorCounts.empty() && config.computeDescriptorCounts.size() == static_cast<size_t>(config.computeBitBindings)) {
                return std::max(config.computeDescriptorCounts[index], 1u);
            }
            return 1u;
        };
        for (size_t i = 0; i < static_cast<size_t>(config.computeBitBindings); ++i) {
            VkDescriptorType type = config.computeDescriptorTypes[i];
            uint32_t count = getComputeCount(i) * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier;
            typeCounts[type] += count;
        }
        for (const auto& [type, count] : typeCounts) {
            poolSizes.push_back({
                .type = type,
                .descriptorCount = count
            });
        }
    } else {
        if (config.storageImageCount > 0) {
            VkDescriptorPoolSize storageImagePoolSize = {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                .descriptorCount = static_cast<uint32_t>(config.storageImageCount * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier)
            };
            poolSizes.push_back(storageImagePoolSize);
        }
        if (config.storageBufferCount > 0) {
            VkDescriptorPoolSize storageBufferPoolSize = {
                .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .descriptorCount = static_cast<uint32_t>(config.storageBufferCount * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier)
            };
            poolSizes.push_back(storageBufferPoolSize);
        }
        if (config.computeBitBindings > 0) {
            VkDescriptorPoolSize computePoolSize = {
                .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .descriptorCount = static_cast<uint32_t>(config.computeBitBindings * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier)
            };
            poolSizes.push_back(computePoolSize);
        }
    }
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * config.poolMultiplier),
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data()
    };
    if (vkCreateDescriptorPool(renderer->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute descriptor pool!");
    }
}
