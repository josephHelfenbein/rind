#pragma once

#include <engine/EntityManager.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>

namespace engine {
    class Light : public Entity {
    public:
        Light(
            EntityManager* entityManager,
            const std::string& name,
            const glm::mat4& transform,
            const glm::vec3& color,
            float intensity,
            float radius,
            bool isMovable = false
        ) : Entity(entityManager, name, "", transform, {}, isMovable, EntityType::Light), color(color), intensity(intensity), radius(radius), shadowProj(glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius)) {
                entityManager->addLight(this);
                lightIdx = entityManager->getLights().size() - 1;
                createShadowMaps(entityManager->getRenderer());
            }
        ~Light();

        glm::vec3 getColor() const { return color; }
        void setColor(const glm::vec3& color) { this->color = color; }

        float getIntensity() const { return intensity; }
        void setIntensity(float intensity) { this->intensity = intensity; }

        float getRadius() const { return radius; }
        void setRadius(float radius) {
            this->radius = radius;
            shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius);
        }

        void updateLightIdx(uint32_t newIdx) { lightIdx = newIdx; }

        uint32_t getShadowMapSize() const { return shadowMapSize; }

        PointLight getPointLightData();
        VkImageView getShadowImageView() const { return shadowDepthImageView; }

        void createShadowMaps(engine::Renderer* renderer, bool forceRecreate = false);
        void bakeShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer);
        void renderShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame);
        bool isBaked() const { return shadowBaked; }

    private:
        glm::vec3 color;
        float intensity;
        float radius;
        glm::mat4 shadowProj;
        uint32_t shadowMapSize = 2048;

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

        void destroyShadowResources(VkDevice device);
    };
}