#pragma once

#include <engine/Renderer.h>
#include <engine/EmbeddedAssets.h>
#include <stb/stb_image.h>
#include <string>
#include <unordered_map>

namespace engine {
    struct Texture {
        std::string name;
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        VkSampler imageSampler = VK_NULL_HANDLE;
        VkFormat format = VK_FORMAT_UNDEFINED;
        int width = 0;
        int height = 0;

        bool operator==(const Texture& other) const {
            return name == other.name;
        }
    };

    class TextureManager {
    public:
        TextureManager(engine::Renderer* renderer);
        ~TextureManager();

        void registerEmbeddedTextures(const std::unordered_map<std::string, EmbeddedAsset>& assets);

        void init();

        Texture* getTexture(const std::string& name);
        void registerTexture(const std::string& name, const Texture& texture);
        void registerTextureFromRGBA(const std::string& name, const uint8_t* rgba, int width, int height);

        bool createTextureFromRGBA(const std::string& name, const unsigned char* rgba, int width, int height);

    private:
        std::unordered_map<std::string, Texture> textures;
        std::unordered_map<std::string, EmbeddedAsset> embeddedAssets;
        engine::Renderer* renderer;
    };
};