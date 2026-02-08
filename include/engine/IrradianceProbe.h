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
        void copyBakedToDynamic(Renderer* renderer, VkCommandBuffer commandBuffer);
        void renderDynamicCubemap(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void dispatchSHCompute(Renderer* renderer, VkCommandBuffer commandBuffer);
        void processSHProjection(Renderer* renderer);

        IrradianceProbeData getProbeData() const;

    private:
        void createComputeResources(Renderer* renderer);
        void cleanupComputeResources(Renderer* renderer);

        float radius;
        std::array<glm::vec3, 9> shCoeffs{};

        VkImage bakedCubemapImage = VK_NULL_HANDLE;
        VkImageView bakedCubemapView = VK_NULL_HANDLE;
        VkDeviceMemory bakedCubemapMemory = VK_NULL_HANDLE;
        VkImageView bakedCubemapFaceViews[6] = { VK_NULL_HANDLE };

        VkImage dynamicCubemapImage = VK_NULL_HANDLE;
        VkImageView dynamicCubemapView = VK_NULL_HANDLE;
        VkDeviceMemory dynamicCubemapMemory = VK_NULL_HANDLE;
        VkImageView dynamicCubemapFaceViews[6] = { VK_NULL_HANDLE };

        VkSampler cubemapSampler = VK_NULL_HANDLE;

        const uint32_t cubemapSize = 32;
        
        static constexpr uint32_t WORKGROUP_SIZE = 8;
        uint32_t numWorkgroupsX = 0;
        uint32_t numWorkgroupsY = 0;
        uint32_t totalWorkgroups = 0;
        VkBuffer shOutputBuffer = VK_NULL_HANDLE;
        VkDeviceMemory shOutputMemory = VK_NULL_HANDLE;
        void* shOutputMappedData = nullptr;
        VkDescriptorSet shDescriptorSet = VK_NULL_HANDLE;

        bool hasImageMap = false;
        bool bakedImageReady = false;
        bool dynamicImageReady = false;
        bool dynamicCubemapDirty = false;
        bool shComputePending = false;
        bool initialSHComputed = false;
        bool computeResourcesCreated = false;
        size_t lastParticleCount = 0;
    };
};
