#pragma once

#include <engine/EntityManager.h>
#include <engine/PushConstants.h>

namespace engine {
    class IrradianceProbe : public Entity {
    public:
        IrradianceProbe(EntityManager* entityManager, const std::string& name, glm::mat4 transform, float radius = 10.0f);
        ~IrradianceProbe();

        float getRadius() const { return radius; }
        void setRadius(float radius) { this->radius = radius; }

        const std::array<glm::vec3, 9>& getSHCoeffs() const { return shCoeffs; }
        void setSHCoeffs(const std::array<glm::vec3, 9>& shCoeffs) { this->shCoeffs = shCoeffs; }

        void createCubemaps(Renderer* renderer);
        void bakeCubemap(Renderer* renderer, VkCommandBuffer commandBuffer);
        void recordCubemapReadback(Renderer* renderer, VkCommandBuffer commandBuffer);
        void processSHProjection(Renderer* renderer);

        IrradianceProbeData getProbeData() const;

    private:
        float radius;
        std::array<glm::vec3, 9> shCoeffs{};

        VkImage bakedCubemapImage = VK_NULL_HANDLE;
        VkImageView bakedCubemapView = VK_NULL_HANDLE;
        VkDeviceMemory bakedCubemapMemory = VK_NULL_HANDLE;
        VkImageView bakedCubemapFaceViews[6] = { VK_NULL_HANDLE };

        const uint32_t cubemapSize = 32;
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

        bool hasImageMap = false;
        bool bakedImageReady = false;
        bool readbackRecorded = false;
    };
};
