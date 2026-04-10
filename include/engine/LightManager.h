#pragma once

#include <engine/EntityManager.h>
#include <engine/PushConstants.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <vector>

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
        VkImageView getShadowImageView(size_t frameIndex = 0) const {
            if (shadowDepthImageViews.empty()) {
                return VK_NULL_HANDLE;
            }
            const size_t idx = frameIndex % shadowDepthImageViews.size();
            return shadowDepthImageViews[idx];
        }

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
        std::array<glm::vec4, 6> frustumPlanes[6];
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
        bool shouldSkipFaceFrustumCull(const engine::AABB& aabb, const glm::mat4& worldTransform) const {
            const glm::vec3 center = getWorldAABBCenter(aabb, worldTransform);
            const float boundsRadius = getWorldAABBBoundingRadius(aabb, worldTransform);
            const float distanceToLight = glm::length(center - getWorldPosition());
            const float overlapDistance = std::max(1.0f, radius * 0.35f) + boundsRadius;
            return distanceToLight <= overlapDistance;
        }
        float getShadowCullPlaneSlack() const {
            return std::max(0.05f, radius * 0.005f);
        }
        bool isAABBInFrustum(int idx, const engine::AABB& aabb, const glm::mat4& transform) const {
            glm::vec3 corners[8] = {
                glm::vec3(aabb.min.x, aabb.min.y, aabb.min.z),
                glm::vec3(aabb.max.x, aabb.min.y, aabb.min.z),
                glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z),
                glm::vec3(aabb.max.x, aabb.max.y, aabb.min.z),
                glm::vec3(aabb.min.x, aabb.min.y, aabb.max.z),
                glm::vec3(aabb.max.x, aabb.min.y, aabb.max.z),
                glm::vec3(aabb.min.x, aabb.max.y, aabb.max.z),
                glm::vec3(aabb.max.x, aabb.max.y, aabb.max.z)
            };
            const float cullSlack = getShadowCullPlaneSlack();
            for (const auto& plane : frustumPlanes[idx]) {
                int out = 0;
                for (const auto& corner : corners) {
                    glm::vec3 worldCorner = glm::vec3(transform * glm::vec4(corner, 1.0f));
                    if (glm::dot(glm::vec3(plane), worldCorner) + plane.w < -cullSlack) {
                        out++;
                    }
                }
                if (out == 8) {
                    return false;
                }
            }
            return true;
        }

        uint32_t lightIdx = 0xFFFFFFFF; // idx in EntityManager's light list

        // dynamic shadow map, sent to shader
        std::vector<VkImage> shadowDepthImages;
        std::vector<VkDeviceMemory> shadowDepthMemories;
        std::vector<VkImageView> shadowDepthImageViews;
        std::vector<std::array<VkImageView, 6>> shadowDepthFaceViews;
        
        // baked shadow map, static
        VkImage bakedShadowImage = VK_NULL_HANDLE;
        VkDeviceMemory bakedShadowMemory = VK_NULL_HANDLE;
        VkImageView bakedShadowImageView = VK_NULL_HANDLE;
        VkImageView bakedShadowFaceViews[6] = { VK_NULL_HANDLE };
        
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
        void reorderLights();

        Renderer* renderer;
        std::vector<Light> lights;
        std::vector<VkBuffer> lightsBuffers;
        std::vector<VkDeviceMemory> lightsBuffersMemory;
        std::vector<void*> lightBuffersMapped;
        std::vector<uint8_t> lightsDirty;
    };
}