#include <engine/TextureManager.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>
#include <engine/EmbeddedAssets.h>
#include <texture/texture_registry.h>

#include <iostream>
#include <functional>
#include <cstring>
#include <cmath>
#include <algorithm>

static inline uint16_t floatToHalf(float value) {
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

engine::TextureManager::TextureManager(
    engine::Renderer* renderer
) : renderer(renderer) {
        renderer->registerTextureManager(this);
    }

engine::TextureManager::~TextureManager() {
    VkDevice device = renderer->getDevice();
    for (auto& [name, texture] : textures) {
        if (texture.imageSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, texture.imageSampler, nullptr);
        }
        if (texture.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, texture.imageView, nullptr);
        }
        if (texture.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, texture.image, nullptr);
        }
        if (texture.imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, texture.imageMemory, nullptr);
        }
    }
}

void engine::TextureManager::init() {
    const auto& embeddedTextures = getEmbedded_texture();
    for (const auto& [textureName, asset] : embeddedTextures) {
        if (textures.find(textureName) != textures.end()) {
            std::cout << "Warning: Duplicate texture name detected: " << textureName << ". Skipping.\n";
            continue;
        }
        stbi_set_flip_vertically_on_load(false);
        void* pixels = nullptr;
        int texWidth = 0, texHeight = 0, texChannels = 0;
        bool isHDR = false;
        std::vector<uint16_t> float16Pixels;

        bool isHDRFile = (std::strcmp(asset.ext, ".hdr") == 0);
        if (isHDRFile) {
            float* floatPixels = stbi_loadf_from_memory(asset.data, static_cast<int>(asset.size),
                &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            if (!floatPixels) {
                std::cerr << "Failed to load HDR texture: " << textureName << std::endl;
                continue;
            }
            size_t numPixels = static_cast<size_t>(texWidth) * static_cast<size_t>(texHeight);
            float16Pixels.resize(numPixels * 4);
            for (size_t i = 0; i < numPixels * 4; ++i) {
                float16Pixels[i] = floatToHalf(floatPixels[i]);
            }
            stbi_image_free(floatPixels);
            pixels = float16Pixels.data();
            isHDR = true;
        } else {
            pixels = stbi_load_from_memory(asset.data, static_cast<int>(asset.size),
                &texWidth, &texHeight, &texChannels, STBI_rgb_alpha);
            if (!pixels) {
                std::cerr << "Failed to load texture: " << textureName << std::endl;
                continue;
            }
            isHDR = false;
        }
        VkFormat format;
        VkDeviceSize pixelSize;
        if (isHDR) {
            format = VK_FORMAT_R16G16B16A16_SFLOAT;
            pixelSize = static_cast<VkDeviceSize>(texWidth) * static_cast<VkDeviceSize>(texHeight) * 4 * sizeof(uint16_t);
        } else {
            bool isNoncolorMap = textureName.find("metallic") != std::string::npos
            || textureName.find("roughness") != std::string::npos
            || textureName.find("normal") != std::string::npos
            || textureName.find("smaa_") != std::string::npos;
            format = isNoncolorMap ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_SRGB;
            pixelSize = static_cast<VkDeviceSize>(texWidth) * static_cast<VkDeviceSize>(texHeight) * 4 * sizeof(uint8_t);
        }
        const bool canMipmap = renderer->formatSupportsLinearBlit(format);
        const uint32_t mipLevels = canMipmap
            ? static_cast<uint32_t>(std::floor(std::log2(std::max(texWidth, texHeight)))) + 1
            : 1;
        VkImageUsageFlags imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (mipLevels > 1) {
            imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }
        VkImage textureImage;
        VkDeviceMemory textureImageMemory;
        std::tie(textureImage, textureImageMemory) = renderer->createImageFromPixels(
            pixels,
            pixelSize,
            texWidth,
            texHeight,
            mipLevels,
            VK_SAMPLE_COUNT_1_BIT,
            format,
            VK_IMAGE_TILING_OPTIMAL,
            imageUsage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            1,
            0
        );
        if (!isHDR) {
            stbi_image_free(pixels);
        }
        if (mipLevels > 1) {
            renderer->generateMipmaps(textureImage, format, texWidth, texHeight, mipLevels, 1);
        } else {
            renderer->transitionImageLayout(
                textureImage,
                format,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                1,
                1
            );
        }
        VkImageView textureImageView = renderer->createImageView(
            textureImage,
            format,
            VK_IMAGE_ASPECT_COLOR_BIT,
            mipLevels
        );
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
            static_cast<float>(mipLevels),
            VK_BORDER_COLOR_INT_OPAQUE_BLACK,
            VK_FALSE
        );
        Texture texture = {
            .name = textureName,
            .image = textureImage,
            .imageView = textureImageView,
            .imageMemory = textureImageMemory,
            .imageSampler = textureSampler,
            .format = format,
            .width = texWidth,
            .height = texHeight
        };
        textures[textureName] = std::move(texture);
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
        VkDevice device = renderer->getDevice();
        if (it->second.imageSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, it->second.imageSampler, nullptr);
        }
        if (it->second.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, it->second.imageView, nullptr);
        }
        if (it->second.image != VK_NULL_HANDLE) {
            vkDestroyImage(device, it->second.image, nullptr);
        }
        if (it->second.imageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, it->second.imageMemory, nullptr);
        }
    }
    textures[name] = texture;
}
