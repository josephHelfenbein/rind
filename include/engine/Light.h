#pragma once

#include <engine/EntityManager.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>

namespace engine {
    class Light : public Entity {
    public:
        Light(EntityManager* entityManager, const std::string& name, glm::mat4 transform, glm::vec3 color, float intensity, float radius, bool isMovable = false)
            : Entity(entityManager, name, "", transform, {}, isMovable), color(color), intensity(intensity), radius(radius) {
                entityManager->addLight(this);
                createShadowMaps(entityManager->getRenderer());
            }
        ~Light();

        glm::vec3 getColor() const { return color; }
        void setColor(const glm::vec3& color) { this->color = color; }

        float getIntensity() const { return intensity; }
        void setIntensity(float intensity) { this->intensity = intensity; }

        float getRadius() const { return radius; }
        void setRadius(float radius) { this->radius = radius; }

        uint32_t getShadowMapSize() const { return shadowMapSize; }
        void setShadowMapSize(uint32_t size);

        PointLight getPointLightData();
        VkImageView getShadowImageView() const { return shadowDepthImageView; }

        void createShadowMaps(engine::Renderer* renderer);
        void bakeShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer);
        void renderShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer);
        bool isBaked() const { return shadowBaked; }

    private:
        glm::vec3 color;
        float intensity;
        float radius;
        uint32_t shadowMapSize = 2048;

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