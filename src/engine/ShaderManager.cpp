#include <engine/ShaderManager.h>
#include <engine/io.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>

#include <format>
#include <iostream>
#include <utility>
#include <unordered_set>

engine::ShaderManager::ShaderManager(engine::Renderer* renderer, std::string shaderDirectory) : renderer(renderer), shaderDirectory(std::move(shaderDirectory)) {
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
    for (auto& shader : graphicsShaders) {
        if (shader->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(renderer->getDevice(), shader->pipeline, nullptr);
        }
        if (shader->pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(renderer->getDevice(), shader->pipelineLayout, nullptr);
        }
        if (shader->descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(renderer->getDevice(), shader->descriptorSetLayout, nullptr);
        }
        if (shader->descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(renderer->getDevice(), shader->descriptorPool, nullptr);
        }
        if (shader->config.passInfo) {
            PassInfo* pass = shader->config.passInfo.get();
            if (processedPasses.insert(pass).second) {
                if (pass->images.has_value()) {
                    for (auto& image : pass->images.value()) {
                        if (image.imageView != VK_NULL_HANDLE) {
                            vkDestroyImageView(renderer->getDevice(), image.imageView, nullptr);
                            image.imageView = VK_NULL_HANDLE;
                        }
                        if (image.image != VK_NULL_HANDLE) {
                            vkDestroyImage(renderer->getDevice(), image.image, nullptr);
                            image.image = VK_NULL_HANDLE;
                        }
                        if (image.memory != VK_NULL_HANDLE) {
                            vkFreeMemory(renderer->getDevice(), image.memory, nullptr);
                            image.memory = VK_NULL_HANDLE;
                        }
                    }
                }
            }
        }
    }
    for (auto& shader : computeShaders) {
        if (shader->pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(renderer->getDevice(), shader->pipeline, nullptr);
        }
        if (shader->pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(renderer->getDevice(), shader->pipelineLayout, nullptr);
        }
        if (shader->descriptorSetLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(renderer->getDevice(), shader->descriptorSetLayout, nullptr);
        }
        if (shader->descriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(renderer->getDevice(), shader->descriptorPool, nullptr);
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
        renderer->createGraphicsDescriptorSetLayout(*shader);
        renderer->createGraphicsPipeline(*shader);
        renderer->createGraphicsDescriptorPool(*shader);
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
        renderer->createComputeDescriptorSetLayout(*shader);
        renderer->createComputePipeline(*shader);
        renderer->createComputeDescriptorPool(*shader);
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
            .format = VK_FORMAT_R8G8B8A8_UNORM,
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