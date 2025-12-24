#include <engine/ShaderManager.h>
#include <engine/TextureManager.h>
#include <engine/io.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>

#include <format>
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
                std::cout << std::format("Warning: Duplicate shader file name detected: {}. Skipping {}\n", baseName, filePath);
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
        std::cout << std::format("Warning: Graphics shader {} not found.\n", name);
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
        std::cout << std::format("Warning: Compute shader {} not found.\n", name);
    }
}

void engine::ShaderManager::addGraphicsShader(GraphicsShader shader) {
    const std::string name = shader.name;
    if (graphicsShaderMap.contains(name)) {
        std::cout << std::format("Warning: Graphics shader {} already added. Skipping duplicate.\n", name);
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
        std::cout << std::format("Warning: Compute shader {} already added. Skipping duplicate.\n", name);
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
        glm::vec3 tangent;
    };

    struct UIVertex {
        glm::vec2 pos;
        glm::vec2 texCoord;
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
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } }
        });
        // Normal (Target 1)
        images.push_back({
            .name = "Normal",
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } }
        });
        // Material (Target 2)
        images.push_back({
            .name = "Material",
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } }
        });
        // Depth
        images.push_back({
            .name = "Depth",
            .format = VK_FORMAT_D32_SFLOAT,
            .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .clearValue = { .depthStencil = { 1.0f, 0 } }
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
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } }
        });
        lightingPass->images = images;
    }

    // UI Pass
    auto uiPass = std::make_shared<PassInfo>();
    uiPass->name = "UIPass";
    uiPass->usesSwapchain = false;
    {
        std::vector<PassImage> images;
        images.push_back({
            .name = "UIColor",
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } }
        });
        uiPass->images = images;
    }

    // Shadow Pass
    auto shadowPass = std::make_shared<PassInfo>();
    shadowPass->name = "ShadowPass";
    shadowPass->usesSwapchain = false;
    shadowPass->hasDepthAttachment = true;
    shadowPass->depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    shadowPass->attachmentFormats.push_back(VK_FORMAT_R32_SFLOAT);

    // Shadow Shader
    {
        GraphicsShader shader = {
            .name = "shadow",
            .vertex = { shaderPath("shadow.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("shadow.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .passInfo = shadowPass,
                .colorAttachmentCount = 1,
                .depthWrite = true,
                .enableDepth = true,
                .depthCompare = VK_COMPARE_OP_LESS,
                .cullMode = VK_CULL_MODE_NONE,
                .vertexBitBindings = 0,
                .fragmentBitBindings = 0,
                .getVertexInputDescriptions = [](VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    binding.binding = 0;
                    binding.stride = sizeof(Vertex);
                    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    attributes.resize(1);
                    attributes[0].binding = 0;
                    attributes[0].location = 0;
                    attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
                    attributes[0].offset = offsetof(Vertex, pos);
                }
            }
        };
        shader.config.setPushConstant<ShadowPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
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
            .format = VK_FORMAT_R8G8B8A8_UNORM,
            .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            .clearValue = { .color = { {0.0f, 0.0f, 0.0f, 0.0f} } }
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
                .passInfo = gbufferPass,
                .colorAttachmentCount = 3,
                .depthWrite = true,
                .enableDepth = true,
                .cullMode = VK_CULL_MODE_BACK_BIT,
                .poolMultiplier = 512,
                .vertexBitBindings = 0,
                .fragmentBitBindings = 5,
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .getVertexInputDescriptions = [](VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    binding.binding = 0;
                    binding.stride = sizeof(Vertex);
                    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

                    attributes.resize(4);
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
                    attributes[3].format = VK_FORMAT_R32G32B32_SFLOAT;
                    attributes[3].offset = offsetof(Vertex, tangent);
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
            .vertex = { shaderPath("lighting.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("lighting.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .passInfo = lightingPass,
                .colorAttachmentCount = 1,
                .depthWrite = false,
                .enableDepth = false,
                .cullMode = VK_CULL_MODE_NONE,
                .vertexBitBindings = 1,
                .fragmentBitBindings = 6,
                .fragmentDescriptorCounts = {
                    1, 1, 1, 1, 64, 1
                },
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .inputBindings = {
                    { 1, "gbuffer", "Albedo" },
                    { 2, "gbuffer", "Normal" },
                    { 3, "gbuffer", "Material" },
                    { 4, "gbuffer", "Depth" }
                }
            }
        };
        shader.config.setPushConstant<LightingPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        shaders.push_back(shader);
    }

    // UI
    {
        GraphicsShader shader = {
            .name = "ui",
            .vertex = { shaderPath("ui.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("ui.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .passInfo = uiPass,
                .colorAttachmentCount = 1,
                .depthWrite = false,
                .enableDepth = false,
                .cullMode = VK_CULL_MODE_NONE,
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .poolMultiplier = 64,
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .getVertexInputDescriptions = [](VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    binding.binding = 0;
                    binding.stride = sizeof(UIVertex);
                    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

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
            .vertex = { shaderPath("text.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("text.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .passInfo = textPass,
                .colorAttachmentCount = 1,
                .depthWrite = false,
                .enableDepth = false,
                .cullMode = VK_CULL_MODE_NONE,
                .vertexBitBindings = 0,
                .fragmentBitBindings = 2,
                .poolMultiplier = 256,
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .getVertexInputDescriptions = [](VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes) {
                    binding.binding = 0;
                    binding.stride = sizeof(UIVertex);
                    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

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

    // Composite
    {
        GraphicsShader shader = {
            .name = "composite",
            .vertex = { shaderPath("composite.vert"), VK_SHADER_STAGE_VERTEX_BIT },
            .fragment = { shaderPath("composite.frag"), VK_SHADER_STAGE_FRAGMENT_BIT },
            .config = {
                .passInfo = mainPass,
                .colorAttachmentCount = 1,
                .depthWrite = false,
                .enableDepth = false,
                .cullMode = VK_CULL_MODE_NONE,
                .vertexBitBindings = 0,
                .fragmentBitBindings = 4,
                .fragmentDescriptorTypes = {
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                    VK_DESCRIPTOR_TYPE_SAMPLER
                },
                .inputBindings = {
                    { 0, "lighting", "SceneColor" },
                    { 1, "ui", "UIColor" },
                    { 2, "text", "TextColor" }
                }
            }
        };
        shaders.push_back(shader);
    }

    renderGraph.nodes.clear();
    renderGraph.nodes.reserve(5);

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
    pushNode(true, lightingPass.get(), { "lighting" });
    pushNode(true, uiPass.get(), { "ui" });
    pushNode(true, textPass.get(), { "text" });
    pushNode(true, mainPass.get(), { "composite" });

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
                std::cout << std::format("Warning: Render graph shader '{}' not found.\n", shaderName);
            }
        }
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
    const int vertexBindings = std::max(config.vertexBitBindings, 0);
    const int fragmentBindings = std::max(config.fragmentBitBindings, 0);
    const size_t expectedBuffers = static_cast<size_t>(vertexBindings) * MAX_FRAMES_IN_FLIGHT;
    if (buffers.size() < expectedBuffers && vertexBindings > 0) {
        throw std::runtime_error("Insufficient uniform buffers for descriptor set creation!");
    }

    auto getFragmentType = [&](int index) {
        if (!config.fragmentDescriptorTypes.empty() && static_cast<size_t>(index) < config.fragmentDescriptorTypes.size()) {
            return config.fragmentDescriptorTypes[static_cast<size_t>(index)];
        }
        return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    };
    auto getFragmentCount = [&](int index) {
        if (!config.fragmentDescriptorCounts.empty() && config.fragmentDescriptorCounts.size() == static_cast<size_t>(fragmentBindings)) {
            return std::max(config.fragmentDescriptorCounts[static_cast<size_t>(index)], 1u);
        }
        return 1u;
    };

    size_t requiredTextureBindings = 0;
    for (int i = 0; i < fragmentBindings; ++i) {
        const VkDescriptorType type = getFragmentType(i);
        if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
            requiredTextureBindings += getFragmentCount(i);
        }
    }
    if (textures.size() < requiredTextureBindings) {
        throw std::runtime_error("Insufficient textures for descriptor set creation!");
    }

    std::vector<VkDescriptorImageInfo> imageInfos;
    std::vector<VkDescriptorBufferInfo> bufferInfos;
    imageInfos.reserve(requiredTextureBindings * MAX_FRAMES_IN_FLIGHT + fragmentBindings * MAX_FRAMES_IN_FLIGHT);
    bufferInfos.reserve(expectedBuffers);
    std::vector<VkWriteDescriptorSet> descriptorWrites;
    descriptorWrites.reserve(static_cast<size_t>(vertexBindings + fragmentBindings) * MAX_FRAMES_IN_FLIGHT);

    for (size_t frame = 0; frame < MAX_FRAMES_IN_FLIGHT; ++frame) {
        for (int binding = 0; binding < vertexBindings; ++binding) {
            const size_t bufferIndex = frame * static_cast<size_t>(vertexBindings) + static_cast<size_t>(binding);
            VkBuffer bufferHandle = buffers[bufferIndex];
            if (bufferHandle == VK_NULL_HANDLE) {
                throw std::runtime_error("Invalid buffer handle provided for descriptor set creation!");
            }
            bufferInfos.push_back({
                .buffer = bufferHandle,
                .offset = 0,
                .range = VK_WHOLE_SIZE
            });
            descriptorWrites.push_back({
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[frame],
                .dstBinding = static_cast<uint32_t>(binding),
                .dstArrayElement = 0,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .descriptorCount = 1,
                .pBufferInfo = &bufferInfos.back()
            });
        }

        size_t textureIndex = 0;
        for (int frag = 0; frag < fragmentBindings; ++frag) {
            const VkDescriptorType type = getFragmentType(frag);
            const uint32_t descriptorCount = getFragmentCount(frag);
            const uint32_t bindingIndex = static_cast<uint32_t>(vertexBindings + frag);

            size_t startIndex = imageInfos.size();
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
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                        .descriptorCount = descriptorCount,
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
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            .imageView = texture->imageView,
                            .sampler = VK_NULL_HANDLE
                        });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = descriptorSets[frame],
                        .dstBinding = bindingIndex,
                        .dstArrayElement = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                        .descriptorCount = descriptorCount,
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
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                            .imageView = texture->imageView,
                            .sampler = texture->imageSampler
                        });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = descriptorSets[frame],
                        .dstBinding = bindingIndex,
                        .dstArrayElement = 0,
                        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        .descriptorCount = descriptorCount,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    break;
                }
            }
        }
    }

    if (!descriptorWrites.empty()) {
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    return descriptorSets;
}

void engine::GraphicsShader::createDescriptorSetLayout(engine::Renderer* renderer) {
    const int totalVertexBindings = std::max(config.vertexBitBindings, 0);
    const int totalFragmentBindings = std::max(config.fragmentBitBindings, 0);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(static_cast<size_t>(totalVertexBindings + totalFragmentBindings));
    VkShaderStageFlags uboStageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    for (int bindingIndex = 0; bindingIndex < totalVertexBindings; ++bindingIndex) {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {
            .binding = static_cast<uint32_t>(bindingIndex),
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = uboStageFlags,
            .pImmutableSamplers = nullptr
        };
        bindings.push_back(uboLayoutBinding);
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
    const int totalStorageBindings = std::max(config.storageImageCount, 0);
    const int totalComputeBindings = std::max(config.computeBitBindings, 0);
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(static_cast<size_t>(totalStorageBindings + totalComputeBindings));
    VkShaderStageFlags uboStageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    for (int bindingIndex = 0; bindingIndex < config.storageImageCount; ++bindingIndex) {
        VkDescriptorSetLayoutBinding uboLayoutBinding = {
            .binding = static_cast<uint32_t>(bindingIndex),
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .descriptorCount = 1,
            .stageFlags = uboStageFlags,
            .pImmutableSamplers = nullptr
        };
        bindings.push_back(uboLayoutBinding);
    }
    for (int offset = 0; offset < config.computeBitBindings; ++offset) {
        uint32_t descriptorCount = 1;
        VkDescriptorSetLayoutBinding computeLayoutBinding = {
            .binding = static_cast<uint32_t>(totalStorageBindings + offset),
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
    VkVertexInputBindingDescription bindingDescription{};
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    if (config.getVertexInputDescriptions) {
        config.getVertexInputDescriptions(bindingDescription, attributeDescriptions);
    }
    VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = bindingDescription.stride != 0u ? 1u : 0u,
        .pVertexBindingDescriptions = bindingDescription.stride != 0u ? &bindingDescription : nullptr,
        .vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size()),
        .pVertexAttributeDescriptions = attributeDescriptions.data()
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
        .lineWidth = 1.0f,
        .cullMode = config.cullMode,
        .frontFace = config.frontFace,
        .depthBiasEnable = VK_FALSE
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
        .blendEnable = VK_TRUE,
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
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 1.0f,
        .stencilTestEnable = VK_FALSE,
        .front = {},
        .back = {}
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
        .basePipelineIndex = -1,
        .pNext = &pipelineRenderingInfo
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
    if (config.vertexBitBindings > 0) {
        VkDescriptorPoolSize uboPoolSize = {
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = static_cast<uint32_t>(config.vertexBitBindings * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier)
        };
        poolSizes.push_back(uboPoolSize);
    }
    if (config.fragmentBitBindings > 0) {
        std::unordered_map<VkDescriptorType, uint32_t> typeCounts;
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
        for (const auto& [type, count] : typeCounts) {
            poolSizes.push_back({
                .type = type,
                .descriptorCount = count
            });
        }
    }
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
        .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * config.poolMultiplier)
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
    if (config.computeBitBindings > 0) {
        VkDescriptorPoolSize computePoolSize = {
            .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = static_cast<uint32_t>(config.computeBitBindings * MAX_FRAMES_IN_FLIGHT * config.poolMultiplier)
        };
        poolSizes.push_back(computePoolSize);
    }
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = static_cast<uint32_t>(poolSizes.size()),
        .pPoolSizes = poolSizes.data(),
        .maxSets = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT * config.poolMultiplier)
    };
    if (vkCreateDescriptorPool(renderer->getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute descriptor pool!");
    }
}