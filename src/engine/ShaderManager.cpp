#include <engine/ShaderManager.h>
#include <engine/io.h>
#include <glm/glm.hpp>

#include <format>
#include <iostream>

engine::ShaderManager::ShaderManager(engine::Renderer* renderer, std::string shaderDirectory) : renderer(renderer) {
    std::vector<std::string> shaderFiles = engine::scanDirectory(shaderDirectory);
    for (const auto& filePath : shaderFiles) {
        if (!std::filesystem::is_regular_file(filePath)) {
            continue;
        }
        std::string fileName = std::filesystem::path(filePath).filename().string();
        if (foundShaderFiles.find(fileName) != foundShaderFiles.end()) {
            std::cout << std::format("Warning: Duplicate shader file name detected: {}. Skipping {}\n", fileName, filePath);
            continue;
        }
        foundShaderFiles[fileName] = filePath;
    }
}

engine::ShaderManager::~ShaderManager() {
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
        renderer->createComputeDescriptorSetLayout(*shader);
        renderer->createComputePipeline(*shader);
        renderer->createComputeDescriptorPool(*shader);
    } else {
        std::cout << std::format("Warning: Compute shader {} not found.\n", name);
    }
}

void engine::ShaderManager::addGraphicsShader(const std::string& name, const ShaderStageInfo& vertex, const ShaderStageInfo& fragment, const GraphicsShader::Config& config) {
    auto shader = std::make_unique<GraphicsShader>(new GraphicsShader{
        .name = name,
        .vertex = vertex,
        .fragment = fragment,
        .config = config
    });

    graphicsShaderMap[name] = shader.get();
    graphicsShaders.push_back(std::move(shader));
}

void engine::ShaderManager::addComputeShader(const std::string& name, const ShaderStageInfo& compute, const ComputeShader::Config& config) {
    auto shader = std::make_unique<ComputeShader>(new ComputeShader{
        .name = name,
        .compute = compute,
        .config = config
    });

    computeShaderMap[name] = shader.get();
    computeShaders.push_back(std::move(shader));
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

    struct GBufferPC {
        glm::mat4 model;
        glm::mat4 view;
        glm::mat4 projection;
        glm::vec3 camPos;
    };

    struct LightingPC {
        glm::mat4 invView;
        glm::mat4 invProj;
        glm::vec3 camPos;
    };

    struct UIPC {
        glm::vec3 tint;
        float _pad;
        glm::mat4 model;
    };
}

std::vector<engine::GraphicsShader> engine::ShaderManager::createDefaultShaders() {
    std::vector<GraphicsShader> shaders;

    // GBuffer
    {
        GraphicsShader shader;
        shader.name = "gbuffer";
        shader.vertex = { "gbuffer.vert", VK_SHADER_STAGE_VERTEX_BIT };
        shader.fragment = { "gbuffer.frag", VK_SHADER_STAGE_FRAGMENT_BIT };

        shader.config.setPushConstant<GBufferPC>(VK_SHADER_STAGE_VERTEX_BIT);

        shader.config.colorAttachmentCount = 3;
        shader.config.depthWrite = true;
        shader.config.enableDepth = true;
        shader.config.cullMode = VK_CULL_MODE_BACK_BIT;
        shader.config.vertexBitBindings = 1;
        shader.config.getVertexInputDescriptions = [](VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes) {
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
        };
        shaders.push_back(shader);
    }

    // Lighting
    {
        GraphicsShader shader;
        shader.name = "lighting";
        shader.vertex = { "lighting.vert", VK_SHADER_STAGE_VERTEX_BIT };
        shader.fragment = { "lighting.frag", VK_SHADER_STAGE_FRAGMENT_BIT };

        shader.config.setPushConstant<LightingPC>(VK_SHADER_STAGE_FRAGMENT_BIT);
        
        shader.config.colorAttachmentCount = 1;
        shader.config.depthWrite = false;
        shader.config.enableDepth = false;
        shader.config.cullMode = VK_CULL_MODE_NONE;
        shader.config.vertexBitBindings = 0;
        
        shaders.push_back(shader);
    }

    // UI
    {
        GraphicsShader shader;
        shader.name = "ui";
        shader.vertex = { "ui.vert", VK_SHADER_STAGE_VERTEX_BIT };
        shader.fragment = { "ui.frag", VK_SHADER_STAGE_FRAGMENT_BIT };

        shader.config.setPushConstant<UIPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        
        shader.config.colorAttachmentCount = 1;
        shader.config.depthWrite = false;
        shader.config.enableDepth = false;
        shader.config.cullMode = VK_CULL_MODE_NONE;
        shader.config.vertexBitBindings = 1;
        shader.config.getVertexInputDescriptions = [](VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes) {
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
        };
        shaders.push_back(shader);
    }

    // Text
    {
        GraphicsShader shader;
        shader.name = "text";
        shader.vertex = { "text.vert", VK_SHADER_STAGE_VERTEX_BIT };
        shader.fragment = { "text.frag", VK_SHADER_STAGE_FRAGMENT_BIT };

        shader.config.setPushConstant<UIPC>(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
        
        shader.config.colorAttachmentCount = 1;
        shader.config.depthWrite = false;
        shader.config.enableDepth = false;
        shader.config.cullMode = VK_CULL_MODE_NONE;
        shader.config.vertexBitBindings = 1;
        shader.config.getVertexInputDescriptions = [](VkVertexInputBindingDescription& binding, std::vector<VkVertexInputAttributeDescription>& attributes) {
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
        };
        shaders.push_back(shader);
    }

    // Composite
    {
        GraphicsShader shader;
        shader.name = "composite";
        shader.vertex = { "composite.vert", VK_SHADER_STAGE_VERTEX_BIT };
        shader.fragment = { "composite.frag", VK_SHADER_STAGE_FRAGMENT_BIT };

        shader.config.colorAttachmentCount = 1;
        shader.config.depthWrite = false;
        shader.config.enableDepth = false;
        shader.config.cullMode = VK_CULL_MODE_NONE;
        shader.config.vertexBitBindings = 0;
        
        shaders.push_back(shader);
    }

    return shaders;
}