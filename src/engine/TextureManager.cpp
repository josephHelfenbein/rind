#include <TextureManager.h>
#include <engine/io.h>

#include <iostream>

engine::TextureManager::TextureManager(engine::Renderer* renderer, std::string textureDirectory) : renderer(renderer) {
    std::function<void(const std::string& directory, std::string parentPath)> scanAndLoadTextures = [&](const std::string& directory, std::string parentPath) {
        std::vector<std::string> textureFiles = engine::scanDirectory(directory);
        for (const auto& filePath : textureFiles) {
            if (std::filesystem::is_directory(filePath)) {
                scanAndLoadTextures(filePath, parentPath + std::filesystem::path(filePath).filename().string() + "-");
                continue;
            }
            if (!std::filesystem::is_regular_file(filePath)) {
                continue;
            }
            std::string fileName = std::filesystem::path(filePath).filename().string();
            std::string textureName = parentPath + fileName;
            if (textures.find(textureName) != textures.end()) {
                std::cout << std::format("Warning: Duplicate texture name detected: {}. Skipping {}\n", textureName, filePath);
                continue;
            }
            Texture texture = {
                .path = filePath,
                // load image data into Vulkan resources
            };
            textures[textureName] = texture;
        }
    };
    scanAndLoadTextures(textureDirectory, "");
}

engine::TextureManager::~TextureManager() {
    // Cleanup Vulkan resources for each texture
    for (auto& [name, texture] : textures) {
        // vkDestroyImageView, vkDestroyImage, vkFreeMemory, vkDestroySampler, etc.
    }
}

engine::Texture* engine::TextureManager::getTexture(const std::string& name) {
    auto it = textures.find(name);
    if (it != textures.end()) {
        return &it->second;
    }
    return nullptr;
}

void engine::TextureManager::registerTexture(const std::string& name, const Texture& texture) {
    auto it = textures.find(name);
    if (it != textures.end()) {
        // cleanup existing texture Vulkan resources
    }
    textures[name] = texture;
}