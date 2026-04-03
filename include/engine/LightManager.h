#pragma once

#include <engine/EntityManager.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>

namespace engine {
    class LightManager;
    class Light {
    public:
        Light(
            LightManager* lightManager,
            const std::string& name,
            const glm::mat4& transform,
            const glm::vec3& color,
            float intensity,
            float radius
        );

        glm::vec3 getColor() const { return color; }
        void setColor(const glm::vec3& color);

        float getIntensity() const { return intensity; }
        void setIntensity(float intensity);

        float getRadius() const { return radius; }
        void setRadius(float radius);

        void updateLightIdx(uint32_t newIdx);

        bool shadowMapReady() const { return hasShadowMap; }
        uint32_t getShadowMapSize() const { return shadowMapSize; }
        LightManager* getLightManager() const { return lightManager; }
        glm::vec3 getWorldPosition() const { return glm::vec3(transform[3]); }

        PointLight getPointLightData();
        VkImageView getShadowImageView() const { return shadowDepthImageView; }

        void createShadowMaps(engine::Renderer* renderer, bool forceRecreate = false);
        void bakeShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer);
        void renderShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame);
        bool isBaked() const { return shadowBaked; }
        void destroyShadowResources(VkDevice device);

    private:
        glm::vec3 color;
        float intensity;
        float radius;
        glm::mat4 shadowProj;
        uint32_t shadowMapSize = 2048;

        glm::mat4 transform;

        glm::mat4 viewProjs[6];
        uint32_t lightIdx = 0xFFFFFFFF; // idx in EntityManager's light list

        // dynamic shadow map, sent to shader
        VkImage shadowDepthImage = VK_NULL_HANDLE;
        VkDeviceMemory shadowDepthMemory = VK_NULL_HANDLE;
        VkImageView shadowDepthImageView = VK_NULL_HANDLE;
        VkImageView shadowDepthFaceViews[6] = { VK_NULL_HANDLE };
        
        // baked shadow map, static
        VkImage bakedShadowImage = VK_NULL_HANDLE;
        VkDeviceMemory bakedShadowMemory = VK_NULL_HANDLE;
        VkImageView bakedShadowImageView = VK_NULL_HANDLE;
        VkImageView bakedShadowFaceViews[6] = { VK_NULL_HANDLE };
        
        bool hasShadowMap = false;
        bool shadowImageReady = false;
        bool shadowBaked = false;
        bool bakedImageReady = false;

        LightManager* lightManager;
    };

    class LightManager {
    public:
        LightManager(Renderer* renderer);
        ~LightManager();
        void clear();

        void addLight(const std::string& name, const glm::mat4& transform, const glm::vec3& color, float intensity, float radius);
        void unregisterLight(uint32_t lightIdx);
        std::vector<Light>& getLights() { return lights; }
        void createLightsUBO();
        void updateLightsUBO(uint32_t frameIndex);
        void createAllShadowMaps();
        void renderShadows(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        std::vector<VkBuffer>& getLightsBuffers() { return lightsBuffers; }

        void markLightsDirty();

        Renderer* getRenderer() const { return renderer; }

    private:
        Renderer* renderer;
        std::vector<Light> lights;
        std::vector<VkBuffer> lightsBuffers;
        std::vector<VkDeviceMemory> lightsBuffersMemory;
        std::vector<void*> lightBuffersMapped;
        std::vector<uint8_t> lightsDirty;
    };
}