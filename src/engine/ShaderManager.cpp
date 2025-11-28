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
    // Cleanup Vulkan resources for each shader
    for (auto& shader : graphicsShaders) {
        // vkDestroyPipeline, vkDestroyPipelineLayout, vkDestroyDescriptorSetLayout, vkDestroyDescriptorPool, etc.
    }
    for (auto& shader : computeShaders) {
        // vkDestroyPipeline, vkDestroyPipelineLayout, vkDestroyDescriptorSetLayout, vkDestroyDescriptorPool, etc.
    }
}

void engine::ShaderManager::addGraphicsShader(const std::string& name, const ShaderStageInfo& vertex, const ShaderStageInfo& fragment, const GraphicsShader::Config& config) {
    auto shader = std::make_unique<GraphicsShader>(new GraphicsShader{
        .name = name,
        .vertex = vertex,
        .fragment = fragment,
        .config = config
    });

    // update shader pipeline

    graphicsShaderMap[name] = shader.get();
    graphicsShaders.push_back(std::move(shader));
}

void engine::ShaderManager::addComputeShader(const std::string& name, const ShaderStageInfo& compute, const ComputeShader::Config& config) {
    auto shader = std::make_unique<ComputeShader>(new ComputeShader{
        .name = name,
        .compute = compute,
        .config = config
    });

    // update shader pipeline

    computeShaderMap[name] = shader.get();
    computeShaders.push_back(std::move(shader));
}

void engine::ShaderManager::editGraphicsShader(const std::string& name, const ShaderStageInfo& newVertex, const ShaderStageInfo& newFragment) {
    auto shader = getGraphicsShader(name);
    if (shader) {
        shader->vertex = newVertex;
        shader->fragment = newFragment;

        // update shader pipeline
    }
}

void engine::ShaderManager::editComputeShader(const std::string& name, const ShaderStageInfo& newCompute) {
    auto shader = getComputeShader(name);
    if (shader) {
        shader->compute = newCompute;

        // update shader pipeline
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