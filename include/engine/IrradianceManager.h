#pragma once

#include <engine/EntityManager.h>
#include <engine/PushConstants.h>
#include <array>
#include <vector>

namespace engine {
    class IrradianceManager;
    class IrradianceProbe {
    public:
        IrradianceProbe(IrradianceManager* irradianceManager, const std::string& name, const glm::mat4& transform, float radius = 10.0f);
        void destroy();

        float getRadius() const { return radius; }
        void setRadius(float radius) { this->radius = radius; }

        const std::array<glm::vec3, 9>& getSHCoeffs() const { return shCoeffs; }
        void setSHCoeffs(const std::array<glm::vec3, 9>& shCoeffs) { this->shCoeffs = shCoeffs; }

        glm::vec3 getWorldPosition() const { return glm::vec3(transform[3]); }
        void createCubemaps(Renderer* renderer);
        void bakeCubemap(Renderer* renderer, VkCommandBuffer commandBuffer);
        void copyBakedToDynamic(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t frameIndex = 0);
        void renderDynamicCubemap(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void dispatchSHCompute(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t frameIndex = 0);
        void processSHProjection(Renderer* renderer, uint32_t frameIndex = 0);

        IrradianceProbeData getProbeData() const;

    private:
        IrradianceManager* irradianceManager;
        void createComputeResources(Renderer* renderer);
        void cleanupComputeResources(Renderer* renderer);

        glm::mat4 transform;
        float radius;
        std::array<glm::vec3, 9> shCoeffs{};

        glm::mat4 viewProjs[6];

        VkImage bakedCubemapImage = VK_NULL_HANDLE;
        VkImageView bakedCubemapView = VK_NULL_HANDLE;
        VkDeviceMemory bakedCubemapMemory = VK_NULL_HANDLE;
        VkImageView bakedCubemapFaceViews[6] = { VK_NULL_HANDLE };

        std::vector<VkImage> dynamicCubemapImages;
        std::vector<VkImageView> dynamicCubemapViews;
        std::vector<VkDeviceMemory> dynamicCubemapMemories;
        std::vector<std::array<VkImageView, 6>> dynamicCubemapFaceViews;

        VkSampler cubemapSampler = VK_NULL_HANDLE;

        const uint32_t cubemapSize = 32;
        
        static constexpr uint32_t WORKGROUP_SIZE = 8;
        uint32_t numWorkgroupsX = 0;
        uint32_t numWorkgroupsY = 0;
        uint32_t totalWorkgroups = 0;
        std::vector<VkBuffer> shOutputBuffers;
        std::vector<VkDeviceMemory> shOutputMemories;
        std::vector<void*> shOutputMappedData;
        std::vector<VkDescriptorSet> shDescriptorSets;

        bool hasImageMap = false;
        bool bakedImageReady = false;
        std::vector<uint8_t> dynamicImageReady;
        std::vector<uint8_t> dynamicCubemapDirty;
        std::vector<uint8_t> shComputePending;
        std::vector<uint8_t> initialSHComputed;
        bool computeResourcesCreated = false;
        size_t lastParticleCount = 0;
    };

    class IrradianceManager {
    public:
        IrradianceManager(Renderer* renderer);
        ~IrradianceManager();
        void clear();

        void addIrradianceProbe(std::string name, const glm::mat4& transform, float radius = 10.0f) {
            irradianceProbes.emplace_back(this, name, transform, radius);
            IrradianceProbe& probe = irradianceProbes.back();
            probe.createCubemaps(renderer);
            irradianceBakingPending = true;
        }
        void createAllIrradianceMaps();
        void createIrradianceProbesUBO();
        void updateIrradianceProbesUBO(uint32_t frameIndex);
        std::vector<IrradianceProbe>& getIrradianceProbes() { return irradianceProbes; }
        std::vector<VkBuffer>& getIrradianceProbesBuffers() { return irradianceBuffers; }
        void renderDynamicIrradianceGraphics(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void dispatchDynamicIrradianceSH(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void renderDynamicIrradiance(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void bakeIrradianceMaps(VkCommandBuffer commandBuffer);
        void recordIrradianceReadback(VkCommandBuffer commandBuffer);
        void processIrradianceSH();
        bool needsIrradianceBaking() const { return irradianceBakingPending; }
        void setIrradianceBakingPending(bool pending) { irradianceBakingPending = pending; }

        Renderer* getRenderer() const { return renderer; }
    private:
        Renderer* renderer;
        std::vector<IrradianceProbe> irradianceProbes;
        bool irradianceBakingPending = false;
        std::vector<VkBuffer> irradianceBuffers;
        std::vector<VkDeviceMemory> irradianceBuffersMemory;
        std::vector<void*> irradianceBuffersMapped;
    };
};
