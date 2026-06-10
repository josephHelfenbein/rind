#include <engine/TextureManager.h>
#include <engine/Renderer.h>
#include <engine/ThreadPool.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include <iostream>
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

void engine::TextureManager::registerEmbeddedTextures(const std::unordered_map<std::string, EmbeddedAsset>& assets) {
    embeddedAssets.insert(assets.begin(), assets.end());
}

void engine::TextureManager::init() {
    stbi_set_flip_vertically_on_load(false);

    struct DecodedTexture {
        std::string name;
        bool valid = false;
        bool isHDR = false;
        int texWidth = 0;
        int texHeight = 0;
        unsigned char* stbiPixels = nullptr;
        std::vector<uint16_t> halfPixels;
    };

    struct AssetEntry { std::string name; const EmbeddedAsset* asset; };
    std::vector<AssetEntry> assetList;
    assetList.reserve(embeddedAssets.size());
    for (const auto& [name, asset] : embeddedAssets) {
        if (textures.find(name) != textures.end()) {
            std::cout << "Warning: Duplicate texture name detected: " << name << ". Skipping.\n";
            continue;
        }
        assetList.push_back({name, &asset});
    }

    std::vector<DecodedTexture> decoded(assetList.size());
    auto decodeOne = [&](size_t idx) {
        const AssetEntry& entry = assetList[idx];
        DecodedTexture& out = decoded[idx];
        out.name = entry.name;
        const EmbeddedAsset& asset = *entry.asset;
        const bool isHDRFile = (std::strcmp(asset.ext, ".hdr") == 0);
        int texChannels = 0;
        if (isHDRFile) {
            float* floatPixels = stbi_loadf_from_memory(asset.data, static_cast<int>(asset.size),
                &out.texWidth, &out.texHeight, &texChannels, STBI_rgb_alpha);
            if (!floatPixels) {
                std::cerr << "Failed to load HDR texture: " << entry.name << std::endl;
                return;
            }
            const size_t numFloats = static_cast<size_t>(out.texWidth) * static_cast<size_t>(out.texHeight) * 4;
            out.halfPixels.resize(numFloats);
            for (size_t i = 0; i < numFloats; ++i) {
                out.halfPixels[i] = floatToHalf(floatPixels[i]);
            }
            stbi_image_free(floatPixels);
            out.isHDR = true;
        } else {
            out.stbiPixels = stbi_load_from_memory(asset.data, static_cast<int>(asset.size),
                &out.texWidth, &out.texHeight, &texChannels, STBI_rgb_alpha);
            if (!out.stbiPixels) {
                std::cerr << "Failed to load texture: " << entry.name << std::endl;
                return;
            }
            out.isHDR = false;
        }
        out.valid = true;
    };

    // parallel decode
    if (assetList.size() > 1) {
        engine::ThreadPool::global().parallel_for_chunks(0, assetList.size(), 1,
            [&](size_t b, size_t e, size_t) {
                for (size_t i = b; i < e; ++i) decodeOne(i);
            });
    } else if (assetList.size() == 1) {
        decodeOne(0);
    }

    // serial Vulkan upload
    for (DecodedTexture& dec : decoded) {
        if (!dec.valid) continue;
        const std::string& textureName = dec.name;
        const int texWidth = dec.texWidth;
        const int texHeight = dec.texHeight;
        const bool isHDR = dec.isHDR;
        void* pixels = isHDR ? static_cast<void*>(dec.halfPixels.data()) : static_cast<void*>(dec.stbiPixels);
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
            stbi_image_free(dec.stbiPixels);
            dec.stbiPixels = nullptr;
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

void engine::TextureManager::registerTextureFromRGBA(const std::string& name, const uint8_t* rgba, int width, int height) {
    createTextureFromRGBA(name, rgba, width, height);
}

bool engine::TextureManager::createTextureFromRGBA(const std::string& name, const unsigned char* rgba, int width, int height) {
    if (rgba == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    VkImage image;
    VkDeviceMemory memory;
    std::tie(image, memory) = renderer->createImageFromPixels(
        const_cast<uint8_t*>(rgba),
        static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height) * 4,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        1,
        0
    );
    renderer->transitionImageLayout(
        image,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1,
        1
    );
    VkImageView view = renderer->createImageView(
        image,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        VK_IMAGE_VIEW_TYPE_2D,
        1
    );
    VkSampler sampler = renderer->createTextureSampler(
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        0.0f,
        VK_FALSE,
        1.0f,
        VK_FALSE,
        VK_COMPARE_OP_ALWAYS,
        0.0f,
        0.0f,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        VK_FALSE
    );
    Texture tex = {
        .name = name,
        .image = image,
        .imageView = view,
        .imageMemory = memory,
        .imageSampler = sampler,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .width = width,
        .height = height
    };
    registerTexture(name, tex);
    return true;
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
