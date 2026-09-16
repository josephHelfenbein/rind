#pragma once

#include <engine/PushConstants.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <string>
#include <cstdint>

namespace engine {
    class Renderer;
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
        VkImage getBakedCubemapImage() const { return bakedCubemapImage; }
        bool isBakedImageReady() const { return bakedImageReady; }
        void markDynamicImageReady(uint32_t frameIndex);
        bool needsDynamicParticleUpdate(Renderer* renderer, uint32_t frameIndex);
        VkImageView getBakedCubemapView() const { return bakedCubemapView; }

        IrradianceProbeData getProbeData() const;

    private:
        IrradianceManager* irradianceManager;

        glm::mat4 transform;
        float radius;
        std::array<glm::vec3, 9> shCoeffs{};

        glm::mat4 viewProjs[6];

        VkImage bakedCubemapImage = VK_NULL_HANDLE;
        VkImageView bakedCubemapView = VK_NULL_HANDLE;
        VkDeviceMemory bakedCubemapMemory = VK_NULL_HANDLE;
        VkImageView bakedCubemapFaceViews[6] = { VK_NULL_HANDLE };


        VkSampler cubemapSampler = VK_NULL_HANDLE;

        const uint32_t cubemapSize = 16;

        bool hasImageMap = false;
        bool bakedImageReady = false;
        std::vector<uint8_t> dynamicImageReady;
        std::vector<size_t> lastParticleCount;
    };

    class IrradianceManager {
    public:
        struct ActiveProbeFrame {
            std::vector<uint32_t> indices;
            std::vector<float> distanceSq;
            std::vector<uint32_t> computeIndices;
            uint32_t count = 0;
            uint32_t exitIndex = 0;
            uint32_t computeCount = 0;
            uint32_t totalProbes = 0;
            uint32_t culledProbes = 0;
        };

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
        void createActiveProbeIndexBuffers();
        VkBuffer getActiveProbeIndexBuffer(uint32_t frameIndex) const;
        void createDynamicSHPartialBuffers();
        VkBuffer getDynamicSHPartialBuffer(uint32_t frameIndex) const;
        void createDynamicSHOutputBuffers();
        VkBuffer getDynamicSHOutputBuffer(uint32_t frameIndex) const;
        uint32_t getDynamicComputeProbeCount(uint32_t frameIndex) const;
        void fillBakedProbeCubemapImageInfos(uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) const;
        void fillDynamicProbeCubemapImageInfos(uint32_t frameIndex, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos);
        void fillDynamicProbeStorageImageInfos(uint32_t frameIndex, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos);
        void buildActiveProbeFrame(uint32_t frameIndex);
        const ActiveProbeFrame* getActiveProbeFrame(uint32_t frameIndex) const;
        uint32_t getActiveProbeCount(uint32_t frameIndex) const;
        void prepareDynamicIrradianceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void finalizeDynamicIrradianceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void dispatchDynamicIrradianceSH(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void dispatchDynamicIrradianceSHReduce(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        void bakeIrradianceMaps(VkCommandBuffer commandBuffer);
        void recordIrradianceReadback(VkCommandBuffer commandBuffer);
        void processIrradianceSH();
        bool needsIrradianceBaking() const { return irradianceBakingPending; }
        void setIrradianceBakingPending(bool pending) { irradianceBakingPending = pending; }

        Renderer* getRenderer() const { return renderer; }
    private:
        struct DynamicProbeImage {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            VkImageView storageView = VK_NULL_HANDLE;
            VkImageView cubeArrayView = VK_NULL_HANDLE;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        };
        DynamicProbeImage& ensureDynamicProbeImage(uint32_t frameIndex);
        void destroyDynamicProbeImages();
        void copyBakedToDynamic(VkCommandBuffer commandBuffer, uint32_t frameIndex);
        void transitionDynamicProbeImage(VkCommandBuffer commandBuffer, DynamicProbeImage& target, VkImageLayout newLayout);
        Renderer* renderer;
        std::vector<IrradianceProbe> irradianceProbes;
        bool irradianceBakingPending = false;
        static constexpr uint32_t maxActiveProbesPerFrame = kMaxIrradianceProbes;
        std::vector<ActiveProbeFrame> activeProbeFrames;
        std::vector<uint8_t> activeProbeFrameBuilt;
        std::vector<VkBuffer> irradianceBuffers;
        std::vector<VkDeviceMemory> irradianceBuffersMemory;
        std::vector<void*> irradianceBuffersMapped;
        std::vector<VkBuffer> activeProbeIndexBuffers;
        std::vector<VkDeviceMemory> activeProbeIndexBuffersMemory;
        std::vector<void*> activeProbeIndexBuffersMapped;
        std::vector<VkBuffer> dynamicSHOutputBuffers;
        std::vector<VkDeviceMemory> dynamicSHOutputBuffersMemory;
        std::vector<VkBuffer> dynamicSHPartialBuffers;
        std::vector<VkDeviceMemory> dynamicSHPartialBuffersMemory;
        std::vector<DynamicProbeImage> dynamicProbeImages;
        static constexpr uint32_t dynamicProbeCubemapSize = 16u;
    };
};
