#include <engine/ShaderManager.h>
#include <engine/TextureManager.h>
#include <engine/io.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>

#include <iostream>
#include <utility>
#include <unordered_set>

engine::ShaderManager::ShaderManager(engine::Renderer* renderer, std::string shaderDirectory)
    : renderer(renderer), shaderDirectory(std::move(shaderDirectory)) {
        renderer->registerShaderManager(this);
        std::vector<std::string> shaderFiles = engine::scanDirectory(this->shaderDirectory);
        for (const auto& filePath : shaderFiles) {
            if (!std::filesystem::is_regular_file(filePath)) {
                continue;
            }
            std::filesystem::path p(filePath);
            std::string baseName = p.stem().string(); // strip trailing .spv
            if (foundShaderFiles.find(baseName) != foundShaderFiles.end()) {
                std::cout << "Warning: Duplicate shader file name detected: " << baseName << ". Skipping " << filePath << "\n";
                continue;
            }
            foundShaderFiles[baseName] = filePath;
        }
    }

engine::ShaderManager::~ShaderManager() {
    std::unordered_set<PassInfo*> processedPasses;
    VkDevice device = renderer->getDevice();
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
                if (pass->images.has_value()) {
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
                }
            }
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
        auto resolveStage = [&](ShaderStageInfo& stage) {
            if (!std::filesystem::exists(stage.path)) {
                std::string mapped = getShaderFilePath(stage.path);
                if (!mapped.empty()) stage.path = mapped;
            }
        };
        resolveStage(shader->vertex);
        resolveStage(shader->fragment);
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
        if (!std::filesystem::exists(shader->compute.path)) {
            std::string mapped = getShaderFilePath(shader->compute.path);
            if (!mapped.empty()) shader->compute.path = mapped;
        }
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

void engine::ShaderManager::editGraphicsShader(const std::string& name, const ShaderStageInfo& newVertex, const ShaderStageInfo& newFragment) {
    auto shader = getGraphicsShader(name);
    if (shader) {
        shader->vertex = newVertex;
        shader->fragment = newFragment;
        loadGraphicsShader(name);
    }
}

void engine::ShaderManager::editComputeShader(const std::string& name, const ShaderStageInfo& newCompute) {
    auto shader = getComputeShader(name);
    if (shader) {
        shader->compute = newCompute;
        loadComputeShader(name);
    }
}

engine::GraphicsShader* engine::ShaderManager::getGraphicsShader(const std::string& name) {
    auto it = graphicsShaderMap.find(name);
    if (it != graphicsShaderMap.end()) {
        return it->second;
    }
    return nullptr;
}

engine::ComputeShader* engine::ShaderManager::getComputeShader(const std::string& name) {
    auto it = computeShaderMap.find(name);
    if (it != computeShaderMap.end()) {
        return it->second;
    }
    return nullptr;
}

std::string engine::ShaderManager::getShaderFilePath(const std::string& name) {
    auto it = foundShaderFiles.find(name);
    if (it != foundShaderFiles.end()) {
        return it->second;
    }
    return "";
}

std::vector<engine::GraphicsShader> engine::ShaderManager::getGraphicsShaders() {
    std::vector<GraphicsShader> shaders;
    for (const auto& shaderPtr : graphicsShaders) {
        shaders.push_back(*shaderPtr);
    }
    return shaders;
}

std::vector<engine::ComputeShader> engine::ShaderManager::getComputeShaders() {
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

std::vector<engine::GraphicsShader> engine::ShaderManager::createDefaultShaders() {
    std::vector<GraphicsShader> shaders;

    auto shaderPath = [&](const std::string& baseName) {
        auto mapped = getShaderFilePath(baseName);
        if (!mapped.empty()) return mapped;
        return (std::filesystem::path(shaderDirectory) / baseName).string();
    };

    // Define Render Passes
    auto gbufferPass = std::make_shared<PassInfo>();
    gbufferPass->name = "GBuffer";
    gbufferPass->usesSwapchain = false;
    
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

    // Lighting Pass
    auto lightingPass = std::make_shared<PassInfo>();
    lightingPass->name = "LightingPass";
    lightingPass->usesSwapchain = false;
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

    // Particle Pass
    auto particlePass = std::make_shared<PassInfo>();
    particlePass->name = "ParticlePass";
    particlePass->usesSwapchain = false;
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "ParticleColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        particlePass->images = images;
    }

    // AO Pass
    auto aoPass = std::make_shared<PassInfo>();
    aoPass->name = "AOPass";
    aoPass->usesSwapchain = false;
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "AOColor",
            .clearValue = { .color = { {1.0f, 1.0f, 1.0f, 1.0f} } },
            .format = VK_FORMAT_R16_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        aoPass->images = images;
    }

    // Bloom Pass
    auto bloomPass = std::make_shared<PassInfo>();
    bloomPass->name = "BloomPass";
    bloomPass->usesSwapchain = false;
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "BloomColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        bloomPass->images = images;
    }

    // Bloom Blur Pass
    auto bloomBlurPassH = std::make_shared<PassInfo>();
    bloomBlurPassH->name = "BloomBlurPassH";
    bloomBlurPassH->usesSwapchain = false;
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "BloomBlurHColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        bloomBlurPassH->images = images;
    }

    // Combine Pass
    auto combinePass = std::make_shared<PassInfo>();
    combinePass->name = "CombinePass";
    combinePass->usesSwapchain = false;
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

    auto bloomBlurPassV = std::make_shared<PassInfo>();
    bloomBlurPassV->name = "BloomBlurPassV";
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "BloomBlurVColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        bloomBlurPassV->images = images;
    }

    // UI Pass
    auto uiPass = std::make_shared<PassInfo>();
    uiPass->name = "UIPass";
    uiPass->usesSwapchain = false;
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

    // Shadow Pass
    auto shadowPass = std::make_shared<PassInfo>();
    shadowPass->name = "ShadowPass";
    shadowPass->usesSwapchain = false;
    shadowPass->hasDepthAttachment = true;
    shadowPass->depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    // Shadow Shader
    {
        GraphicsShader shader = {
            .name = "shadow",
            .vertex = { shaderPath("shadow.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .config = {
                .poolMultiplier = 512,
                .vertexBitBindings = 1,
                .fragmentBitBindings = 0,
                .vertexDescriptorCounts = { 1 },
                .vertexDescriptorTypes = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = true,
                .depthCompare = VK_COMPARE_OP_LESS,
                .enableDepth = true,
                .passInfo = shadowPass,
                .colorAttachmentCount = 0,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(2);
                    bindings[0].binding = 0;
                    bindings[0].stride = sizeof(Vertex);
                    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    bindings[1].binding = 1;
                    bindings[1].stride = sizeof(SkinnedVertex);
                    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    attributes.resize(3);
                    attributes[0].binding = 0;
                    attributes[0].location = 0;
                    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                    attributes[0].offset = offsetof(Vertex, pos);

                    attributes[1].binding = 1;
                    attributes[1].location = 1;
                    attributes[1].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    attributes[1].offset = offsetof(SkinnedVertex, joints);

                    attributes[2].binding = 1;
                    attributes[2].location = 2;
                    attributes[2].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    attributes[2].offset = offsetof(SkinnedVertex, weights);
                }
            }
        };
        shader.config.setPushConstant<ShadowPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    // Irradiance Pass
    auto irradiancePass = std::make_shared<PassInfo>();
    irradiancePass->name = "IrradiancePass";
    irradiancePass->usesSwapchain = false;
    irradiancePass->hasDepthAttachment = false;
    irradiancePass->attachmentFormats = { VK_FORMAT_R16G16B16A16_SFLOAT };

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
                    bindings[0].binding = 0;
                    bindings[0].stride = sizeof(Vertex);
                    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    attributes.resize(3);
                    attributes[0].binding = 0;
                    attributes[0].location = 0;
                    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                    attributes[0].offset = offsetof(Vertex, pos);

                    attributes[1].binding = 0;
                    attributes[1].location = 1;
                    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
                    attributes[1].offset = offsetof(Vertex, normal);

                    attributes[2].binding = 0;
                    attributes[2].location = 2;
                    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
                    attributes[2].offset = offsetof(Vertex, texCoord);
                }
            }
        };
        shader.config.setPushConstant<IrradianceBakePC>(VK_SHADER_STAGE_VERTEX_BIT);
        shaders.push_back(shader);
    }

    // Text Pass
    auto textPass = std::make_shared<PassInfo>();
    textPass->name = "TextPass";
    textPass->usesSwapchain = false;
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "TextColor",
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } },
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT
        });
        textPass->images = images;
    }

    auto mainPass = std::make_shared<PassInfo>();
    mainPass->name = "Main";
    mainPass->usesSwapchain = true;

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
                    bindings[0].binding = 0;
                    bindings[0].stride = sizeof(Vertex);
                    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    bindings[1].binding = 1;
                    bindings[1].stride = sizeof(SkinnedVertex);
                    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    attributes.resize(6);
                    attributes[0].binding = 0;
                    attributes[0].location = 0;
                    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                    attributes[0].offset = offsetof(Vertex, pos);

                    attributes[1].binding = 0;
                    attributes[1].location = 1;
                    attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
                    attributes[1].offset = offsetof(Vertex, normal);

                    attributes[2].binding = 0;
                    attributes[2].location = 2;
                    attributes[2].format = VK_FORMAT_R32G32_SFLOAT;
                    attributes[2].offset = offsetof(Vertex, texCoord);

                    attributes[3].binding = 0;
                    attributes[3].location = 3;
                    attributes[3].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    attributes[3].offset = offsetof(Vertex, tangent);

                    attributes[4].binding = 1;
                    attributes[4].location = 4;
                    attributes[4].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    attributes[4].offset = offsetof(SkinnedVertex, joints);

                    attributes[5].binding = 1;
                    attributes[5].location = 5;
                    attributes[5].format = VK_FORMAT_R32G32B32A32_SFLOAT;
                    attributes[5].offset = offsetof(SkinnedVertex, weights);
                }
            }
        };
        shader.config.setPushConstant<GBufferPC>(VK_SHADER_STAGE_VERTEX_BIT);
        shaders.push_back(shader);
    }

    // Lighting
    {
        GraphicsShader shader = {
            .name = "lighting",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("lighting.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .vertexBitBindings = 2,
                .fragmentBitBindings = 7,
                .vertexDescriptorCounts = { 1, 1 },
                .vertexDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                    VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER
                },
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1, 1, 64, 1
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
                .passInfo = lightingPass,
                .colorAttachmentCount = 1,
                .inputBindings = {
                    { 2, "gbuffer", "Albedo" },
                    { 3, "gbuffer", "Normal" },
                    { 4, "gbuffer", "Material" },
                    { 5, "gbuffer", "Depth" },
                    { 6, "particle", "ParticleColor" }
                }
            }
        };
        shader.config.setPushConstant<LightingPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    // SSR
    {
        GraphicsShader shader = {
            .name = "ssr",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("ssr.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
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
                .passInfo = ssrPass,
                .colorAttachmentCount = 1,
                .inputBindings = {
                    { 0, "lighting", "SceneColor" },
                    { 1, "gbuffer", "Depth" },
                    { 2, "gbuffer", "Normal" }
                }
            }
        };
        shader.config.setPushConstant<SSRPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    // AO
    {
        GraphicsShader shader = {
            .name = "ao",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("ao.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
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
                .passInfo = aoPass,
                .colorAttachmentCount = 1,
                .inputBindings = {
                    { 0, "gbuffer", "Depth" },
                    { 1, "gbuffer", "Normal" }
                }
            }
        };
        shader.config.setPushConstant<AOPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
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
                .colorAttachmentCount = 1,
                .getVertexInputDescriptions = nullptr
            }
        };
        shader.config.setPushConstant<ParticlePC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
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
        shaders.push_back(shader);
    }

    // Bloom Blur Horizontal
    {
        GraphicsShader shader = {
            .name = "hblur",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("hblur.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
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
                .inputBindings = {
                    { 0, "bloom", "BloomColor" }
                }
            }
        };
        shaders.push_back(shader);
    }

    // Bloom Blur Vertical
    {
        GraphicsShader shader = {
            .name = "vblur",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("vblur.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
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
                .inputBindings = {
                    { 0, "hblur", "BloomBlurHColor" }
                }
            }
        };
        shaders.push_back(shader);
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
                    bindings[0].binding = 0;
                    bindings[0].stride = sizeof(UIVertex);
                    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    attributes.resize(2);
                    attributes[0].binding = 0;
                    attributes[0].location = 0;
                    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
                    attributes[0].offset = offsetof(UIVertex, pos);

                    attributes[1].binding = 0;
                    attributes[1].location = 1;
                    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
                    attributes[1].offset = offsetof(UIVertex, texCoord);
                }
            }
        };
        shader.config.setPushConstant<UIPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    // Text
    {
        GraphicsShader shader = {
            .name = "text",
            .vertex = { shaderPath("ui.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("text.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .poolMultiplier = 256,
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
                .passInfo = textPass,
                .colorAttachmentCount = 1,
                .getVertexInputDescriptions = [](std::vector<VkVertexInputBindingDescription>& bindings, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    bindings.resize(1);
                    bindings[0].binding = 0;
                    bindings[0].stride = sizeof(UIVertex);
                    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    attributes.resize(2);
                    attributes[0].binding = 0;
                    attributes[0].location = 0;
                    attributes[0].format = VK_FORMAT_R32G32_SFLOAT;
                    attributes[0].offset = offsetof(UIVertex, pos);

                    attributes[1].binding = 0;
                    attributes[1].location = 1;
                    attributes[1].format = VK_FORMAT_R32G32_SFLOAT;
                    attributes[1].offset = offsetof(UIVertex, texCoord);
                }
            }
        };
        shader.config.setPushConstant<UIPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
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
                    { 3, "vblur", "BloomBlurVColor" }
                }
            }
        };
        shaders.push_back(shader);
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
                .inputBindings = {
                    { 0, "combine", "CombinedColor" }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    // SMAA Blending Weight Calculation
    {
        GraphicsShader shader = {
            .name = "smaaWeight",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("smaaWeight.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
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
                    VK_DESCRIPTOR_TYPE_SAMPLER,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .cullMode = VK_CULL_MODE_NONE,
                .depthWrite = false,
                .enableDepth = false,
                .passInfo = smaaWeightPass,
                .sampler = renderer->getNearestSampler(),
                .blendEnable = false,
                .colorAttachmentCount = 1,
                .inputBindings = {
                    { 0, "smaaEdge", "SMAAEdgesColor" }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
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
                .inputBindings = {
                    { 0, "combine", "CombinedColor" },
                    { 1, "smaaWeight", "SMAAWeightsColor" }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    // Composite
    {
        GraphicsShader shader = {
            .name = "composite",
            .vertex = { shaderPath("rect.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("composite.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
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
                .passInfo = mainPass,
                .colorAttachmentCount = 1,
                .inputBindings = {
                    { 0, "combine", "CombinedColor" },
                    { 1, "ui", "UIColor" },
                    { 2, "text", "TextColor" },
                    { 3, "smaaBlend", "SMAABlendedColor" }
                }
            }
        };
        shader.config.setPushConstant<CompositePC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    renderGraph.nodes.clear();
    renderGraph.nodes.reserve(6);

    auto pushNode = [&](bool is2D, PassInfo* pass, std::initializer_list<const char*> shaderList) {
        RenderNode node;
        node.is2D = is2D;
        node.passInfo = pass;
        for (auto name : shaderList) {
            node.shaderNames.emplace_back(name);
        }
        renderGraph.nodes.push_back(std::move(node));
    };

    pushNode(false, gbufferPass.get(), { "gbuffer" });
    pushNode(true, particlePass.get(), { "particle" });
    pushNode(true, lightingPass.get(), { "lighting" });
    pushNode(true, ssrPass.get(), { "ssr" });
    pushNode(true, aoPass.get(), { "ao" });
    pushNode(true, bloomPass.get(), { "bloom" });
    pushNode(true, bloomBlurPassH.get(), { "hblur" });
    pushNode(true, bloomBlurPassV.get(), { "vblur" });
    pushNode(true, combinePass.get(), { "combine" });
    pushNode(true, smaaEdgePass.get(), { "smaaEdge" });
    pushNode(true, smaaWeightPass.get(), { "smaaWeight" });
    pushNode(true, smaaBlendPass.get(), { "smaaBlend" });
    pushNode(true, uiPass.get(), { "ui" });
    pushNode(true, textPass.get(), { "text" });
    pushNode(true, mainPass.get(), { "composite" });

    return shaders;
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

std::vector<engine::ComputeShader> engine::ShaderManager::createDefaultComputeShaders() {
    std::vector<ComputeShader> shaders;

    auto shaderPath = [&](const std::string& baseName) {
        auto mapped = getShaderFilePath(baseName);
        if (!mapped.empty()) return mapped;
        return (std::filesystem::path(shaderDirectory) / baseName).string();
    };

    // Spherical Harmonics Projection
    {
        ComputeShader shader = {
            .name = "sh",
            .compute = { shaderPath("sh.comp"), VK_SHADER_STAGE_COMPUTE_BIT },
            .config = {
                .poolMultiplier = 64,
                .computeBitBindings = 1,
                .storageImageCount = 0,
                .storageBufferCount = 1
            }
        };
        shader.config.setPushConstant<SHPC>(VK_SHADER_STAGE_COMPUTE_BIT);
        shaders.push_back(shader);
    }

    return shaders;
}

std::vector<engine::RenderNode>& engine::ShaderManager::getRenderGraph() {
    return renderGraph.nodes;
}

const std::vector<engine::RenderNode>& engine::ShaderManager::getRenderGraph() const {
    return renderGraph.nodes;
}

void engine::ShaderManager::resolveRenderGraphShaders() {
    for (auto& node : renderGraph.nodes) {
        node.shaders.clear();
        for (const auto& shaderName : node.shaderNames) {
            auto it = graphicsShaderMap.find(shaderName);
            if (it != graphicsShaderMap.end()) {
                node.shaders.insert(it->second);
                if (!node.passInfo && it->second->config.passInfo) {
                    node.passInfo = it->second->config.passInfo.get();
                }
            } else {
                std::cout << "Warning: Render graph shader '" << shaderName << "' not found.\n";
            }
        }
    }
}

void engine::GraphicsShader::updateDescriptorSets(Renderer* renderer, std::vector<VkDescriptorSet>& descriptorSets, std::vector<Texture*>& textures, std::vector<VkBuffer>& buffers) {
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
    for (int frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
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
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .imageView = texture->imageView,
                        .sampler = (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? renderer->getMainTextureSampler() : VK_NULL_HANDLE
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
                ShaderManager* sm = renderer->getShaderManager();
                GraphicsShader* sourceShader = sm ? sm->getGraphicsShader(inputBinding->sourceShaderName) : nullptr;
                VkImageView imageView = VK_NULL_HANDLE;
                
                if (sourceShader && sourceShader->config.passInfo && sourceShader->config.passInfo->images.has_value()) {
                    for (auto& img : sourceShader->config.passInfo->images.value()) {
                        if (img.name == inputBinding->attachmentName) {
                            imageView = img.imageView;
                            break;
                        }
                    }
                }
                
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
    bindings.reserve(static_cast<size_t>(totalStorageImageBindings + totalStorageBufferBindings + totalComputeBindings));
    VkShaderStageFlags stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    uint32_t currentBinding = 0;
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
    std::vector<char> vertShaderCode = readFile(vertex.path);
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
        std::vector<char> fragShaderCode = readFile(fragment.path);
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
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp = VK_BLEND_OP_ADD,
        .colorWriteMask = static_cast<VkColorComponentFlags>(VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT)
    };
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(config.colorAttachmentCount, colorBlendAttachment);
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
    std::vector<char> compShaderCode = readFile(compute.path);
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
