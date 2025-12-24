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
                createShadowMap(entityManager->getRenderer());
            }

        glm::vec3 getColor() const { return color; }
        void setColor(const glm::vec3& color) { this->color = color; }

        float getIntensity() const { return intensity; }
        void setIntensity(float intensity) { this->intensity = intensity; }

        float getRadius() const { return radius; }
        void setRadius(float radius) { this->radius = radius; }

        PointLight getPointLightData();
        VkImageView getShadowImageView() const { return shadowImageView; }

        void createShadowMap(engine::Renderer* renderer);
        void renderShadowMap(engine::Renderer* renderer, VkCommandBuffer commandBuffer);

    private:
        glm::vec3 color;
        float intensity;
        float radius;

        VkImage shadowImage = VK_NULL_HANDLE;
        VkDeviceMemory shadowMemory = VK_NULL_HANDLE;
        VkImageView shadowImageView = VK_NULL_HANDLE;
        VkImageView shadowFaceViews[6] = { VK_NULL_HANDLE };
        
        VkImage shadowDepthImage = VK_NULL_HANDLE;
        VkDeviceMemory shadowDepthMemory = VK_NULL_HANDLE;
        VkImageView shadowDepthFaceViews[6] = { VK_NULL_HANDLE };
        
        bool hasShadowMap = false;
        bool shadowImageReady = false; // tracks whether layout has been transitioned at least once

        void destroyShadowResources(VkDevice device);
    };
}