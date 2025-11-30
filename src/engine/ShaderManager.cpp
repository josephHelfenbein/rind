#include <engine/ShaderManager.h>
#include <engine/io.h>

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