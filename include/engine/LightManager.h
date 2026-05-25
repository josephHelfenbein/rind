#pragma once

#include <engine/EntityManager.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>
#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace engine {
    class LightManager;

    // stable handle for lights
    using LightHandle = uint64_t;
    inline constexpr LightHandle kInvalidLightHandle = 0;

    class Light {
    public:
        Light(
            LightManager* lightManager,
            LightHandle handle,
            const std::string& name,
            const glm::mat4& transform,
            const glm::vec3& color,
            float intensity,
            float radius
        );

        struct ShadowResources {
            std::vector<VkImage> images;
            std::vector<VkDeviceMemory> memories;
            std::vector<VkImageView> views;
        };

        LightHandle getHandle() const { return handle; }

        glm::vec3 getColor() const { return color; }
        void setColor(const glm::vec3& color);

        float getIntensity() const { return intensity; }
        void setIntensity(float intensity);

        float getRadius() const { return radius; }
        void setRadius(float radius);

        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform);

        void updateLightIdx(uint32_t newIdx);

        bool shadowMapReady() const { return hasShadowMap; }
        uint32_t getShadowMapSize() const { return shadowMapSize; }
        LightManager* getLightManager() const { return lightManager; }
        glm::vec3 getWorldPosition() const { return glm::vec3(transform[3]); }

        PointLight getPointLightData();
        VkImageView getShadowImageView(size_t frameIndex = 0) const {
            if (shadowDepthImageViews.empty()) {
                return VK_NULL_HANDLE;
            }
            const size_t idx = frameIndex % shadowDepthImageViews.size();
            return shadowDepthImageViews[idx];
        }
        void fillShadowLightEntry(ShadowLightEntry& entry) const;

        void createShadowMaps(engine::Renderer* renderer, bool forceRecreate = false);
        void bakeShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer);
        void renderShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame);
        bool isBaked() const { return shadowBaked; }

        ShadowResources takeShadowResources();
        static void freeShadowResources(VkDevice device, ShadowResources& resources);
        void destroyShadowResources(VkDevice device);

    private:
        void updateShadowMatrices();

        glm::vec3 color;
        float intensity;
        float radius;
        glm::mat4 shadowProj;
        uint32_t shadowMapSize = 2048;

        glm::mat4 transform;

        glm::mat4 viewProjs[6];
        glm::vec3 getWorldAABBCenter(const engine::AABB& aabb, const glm::mat4& worldTransform) const {
            const glm::vec3 localCenter = (aabb.min + aabb.max) * 0.5f;
            return glm::vec3(worldTransform * glm::vec4(localCenter, 1.0f));
        }
        float getWorldAABBBoundingRadius(const engine::AABB& aabb, const glm::mat4& worldTransform) const {
            const glm::vec3 localExtents = (aabb.max - aabb.min) * 0.5f;
            glm::mat3 basis = glm::mat3(worldTransform);
            basis[0] = glm::abs(basis[0]);
            basis[1] = glm::abs(basis[1]);
            basis[2] = glm::abs(basis[2]);
            const glm::vec3 worldExtents = basis * localExtents;
            return glm::length(worldExtents);
        }
        bool intersectsShadowRange(const engine::AABB& aabb, const glm::mat4& worldTransform) const {
            const glm::vec3 center = getWorldAABBCenter(aabb, worldTransform);
            const float boundsRadius = getWorldAABBBoundingRadius(aabb, worldTransform);
            const float distanceToLight = glm::length(center - getWorldPosition());
            const float cullRangeScale = 1.02f;
            return (distanceToLight - boundsRadius) <= (radius * cullRangeScale);
        }
        LightHandle handle = kInvalidLightHandle; // stable id
        uint32_t lightIdx = 0xFFFFFFFF; // upload-slot index, reassigned by reorderLights

        // dynamic shadow map, sent to shader
        std::vector<VkImage> shadowDepthImages;
        std::vector<VkDeviceMemory> shadowDepthMemories;
        std::vector<VkImageView> shadowDepthImageViews;
        std::vector<std::array<VkImageView, 6>> shadowDepthFaceViews;
        std::vector<VkImageView> shadowDepthArrayViews;

        // baked shadow map, static
        VkImage bakedShadowImage = VK_NULL_HANDLE;
        VkDeviceMemory bakedShadowMemory = VK_NULL_HANDLE;
        VkImageView bakedShadowImageView = VK_NULL_HANDLE;
        VkImageView bakedShadowFaceViews[6] = { VK_NULL_HANDLE };
        VkImageView bakedShadowArrayView = VK_NULL_HANDLE;
        
        bool hasShadowMap = false;
        std::vector<uint8_t> shadowImageReady;
        bool shadowBaked = false;
        bool bakedImageReady = false;

        LightManager* lightManager;
    };

    class LightManager {
    public:
        LightManager(Renderer* renderer);
        ~LightManager();
        void clear();

        LightHandle addLight(const std::string& name, const glm::mat4& transform, const glm::vec3& color, float intensity, float radius);
        void unregisterLight(LightHandle handle);
        Light* getLight(LightHandle handle);
        std::vector<std::unique_ptr<Light>>& getLights() { return lights; }
        void processDeferredDestroys();
        void createLightsUBO();
        void updateLightsUBO(uint32_t frameIndex);
        void createShadowLightsBuffers();
        void updateShadowLightsBuffer(uint32_t frameIndex);
        void createAllShadowMaps();
        void renderShadows(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        std::vector<VkBuffer>& getLightsBuffers() { return lightsBuffers; }
        std::vector<VkBuffer>& getShadowLightsBuffers() { return shadowLightsBuffers; }

        void markLightsDirty();

        Renderer* getRenderer() const { return renderer; }

    private:
        void reorderLights();
        void scheduleShadowResourceDestroy(Light::ShadowResources&& resources);
        void flushDeferredDestroys();

        struct DeferredShadowDestroy {
            Light::ShadowResources resources;
            uint32_t framesRemaining;
        };

        Renderer* renderer;
        std::vector<std::unique_ptr<Light>> lights;
        std::unordered_map<LightHandle, Light*> lightLookup;
        LightHandle nextHandle = kInvalidLightHandle + 1;
        std::vector<DeferredShadowDestroy> deferredDestroys;
        std::vector<VkBuffer> lightsBuffers;
        std::vector<VkDeviceMemory> lightsBuffersMemory;
        std::vector<void*> lightBuffersMapped;
        std::vector<uint8_t> lightsDirty;
        std::vector<VkBuffer> shadowLightsBuffers;
        std::vector<VkDeviceMemory> shadowLightsMemories;
        std::vector<void*> shadowLightsMapped;
    };
}