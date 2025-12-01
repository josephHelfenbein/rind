#include <TextureManager.h>
#include <engine/io.h>

#include <iostream>

static uint16_t floatToHalf(float value) {
    union { float f; uint32_t i; } v;
    v.f = value;
    uint32_t i = v.i;
    uint32_t sign = (i >> 16) & 0x8000;
    int32_t exponent = ((i >> 23) & 0xff) - 127 + 15;
    uint32_t mantissa = i & 0x7fffff;
    
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<uint16_t>(sign);
        mantissa = (mantissa | 0x800000) >> (1 - exponent);
        return static_cast<uint16_t>(sign | (mantissa >> 13));
    } else if (exponent >= 0x1f) {
        return static_cast<uint16_t>(sign | 0x7c00);
    }
    return static_cast<uint16_t>(sign | (exponent << 10) | (mantissa >> 13));
}

engine::TextureManager::TextureManager(engine::Renderer* renderer, std::string textureDirectory) : renderer(renderer), textureDirectory(textureDirectory) {}

engine::TextureManager::~TextureManager() {
    for (auto& [name, texture] : textures) {
        if (texture.imageSampler != VK_NULL_HANDLE) {
            vkDestroySampler(renderer->getDevice(), texture.imageSampler, nullptr);
        }
        if (texture.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer->getDevice(), texture.imageView, nullptr);
        }
        if (texture.image != VK_NULL_HANDLE) {
            vkDestroyImage(renderer->getDevice(), texture.image, nullptr);
        }
        if (texture.imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(renderer->getDevice(), texture.imageMemory, nullptr);
        }
    }
}

void engine::TextureManager::init() {
    std::function<void(const std::string& directory, std::string parentPath)> scanAndLoadTextures = [&](const std::string& directory, std::string parentPath) {
        std::vector<std::string> textureFiles = engine::scanDirectory(directory);
        for (const auto& filePath : textureFiles) {
            if (std::filesystem::is_directory(filePath)) {
                scanAndLoadTextures(filePath, parentPath + std::filesystem::path(filePath).filename().string() + "_");
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
            stbi_set_flip_vertically_on_load(false);
            void* pixels = nullptr;
            int texWidth, texHeight, texChannels;
            bool isHDR = false;
            if (filePath.ends_with(".hdr")) {
                pixels = stbi_loadf(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
                isHDR = true;
            } else {
                pixels = stbi_load(filePath.c_str(), &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
                isHDR = false;
            }
            VkFormat format;
            VkDeviceSize pixelSize;
            if (isHDR) {
                format = VK_FORMAT_R16G16B16A16_SFLOAT;
                float* floatPixels = static_cast<float*>(pixels);
                size_t numPixels = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight);
                std::vector<uint16_t> float16Pixels(numPixels * 4);
                for (size_t i = 0; i < numPixels * 4; ++i) {
                    float16Pixels[i] = floatToHalf(floatPixels[i]);
                }
                pixelSize = numPixels * 4 * sizeof(uint16_t);
            } else {
                format = VK_FORMAT_R8G8B8A8_SRGB;
                pixelSize = static_cast<VkDeviceSize>(texWidth) * static_cast<VkDeviceSize>(texHeight) * 4 * sizeof(uint8_t);
            }
            VkImage textureImage;
            VkDeviceMemory textureImageMemory;
            std::tie(textureImage, textureImageMemory) = renderer->createImageFromPixels(
                pixels,
                pixelSize,
                texWidth,
                texHeight,
                1,
                VK_SAMPLE_COUNT_1_BIT,
                format,
                VK_IMAGE_TILING_OPTIMAL,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                1,
                0
            );
            stbi_image_free(pixels);
            VkImageView textureImageView = renderer->createImageView(textureImage, format);
            VkSampler textureSampler;
            textureSampler = renderer->createTextureSampler(
                VK_FILTER_LINEAR,
                VK_FILTER_LINEAR,
                VK_SAMPLER_MIPMAP_MODE_LINEAR,
                VK_SAMPLER_ADDRESS_MODE_REPEAT,
                VK_SAMPLER_ADDRESS_MODE_REPEAT,
                VK_SAMPLER_ADDRESS_MODE_REPEAT,
                0.0f,
                VK_TRUE,
                16.0f,
                VK_FALSE,
                VK_COMPARE_OP_ALWAYS,
                0.0f,
                0.0f,
                VK_BORDER_COLOR_INT_OPAQUE_BLACK,
                VK_FALSE
            );
            Texture texture = {
                .path = filePath,
                .image = textureImage,
                .imageView = textureImageView,
                .imageMemory = textureImageMemory,
                .imageSampler = textureSampler,
                .format = format,
                .width = texWidth,
                .height = texHeight
            };
            textures[textureName] = texture;
        }
    };
    scanAndLoadTextures(textureDirectory, "");
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
        if (it->second.imageSampler != VK_NULL_HANDLE) {
            vkDestroySampler(renderer->getDevice(), it->second.imageSampler, nullptr);
        }
        if (it->second.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(renderer->getDevice(), it->second.imageView, nullptr);
        }
        if (it->second.image != VK_NULL_HANDLE) {
            vkDestroyImage(renderer->getDevice(), it->second.image, nullptr);
        }
        if (it->second.imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(renderer->getDevice(), it->second.imageMemory, nullptr);
        }
    }
    textures[name] = texture;
}