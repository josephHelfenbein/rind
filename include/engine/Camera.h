#pragma once

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <engine/EntityManager.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {
    class Camera : public Entity {
    public:
        Camera(EntityManager* entityManager, const std::string& name, glm::mat4 transform, float fovY, float nearPlane, float farPlane, bool isMovable = true)
            : Entity(entityManager, name, "", transform, {}, isMovable),
              fovY(fovY), nearPlane(nearPlane), farPlane(farPlane) {
                entityManager->setCamera(this);
                VkExtent2D swapChainExtent = entityManager->getRenderer()->getSwapChainExtent();
                aspectRatio = static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
              }

        void setPerspective(float fovY, float aspectRatio, float nearPlane, float farPlane) {
            this->fovY = fovY;
            this->aspectRatio = aspectRatio;
            this->nearPlane = nearPlane;
            this->farPlane = farPlane;
        }

        void setAspectRatio(float aspectRatio) {
            this->aspectRatio = aspectRatio;
        }
        float getFovY() const { return fovY; }
        float getAspectRatio() const { return aspectRatio; }
        float getNearPlane() const { return nearPlane; }
        float getFarPlane() const { return farPlane; }
        glm::mat4 getViewMatrix() const {
            glm::mat4 worldTransform = getWorldTransform();
            return glm::inverse(worldTransform);
        }
        glm::mat4 getProjectionMatrix() const {
            glm::mat4 proj = glm::perspective(glm::radians(fovY), aspectRatio, nearPlane, farPlane);
            proj[1][1] *= -1.0f;
            return proj;
        }
        void update(float deltaTime) override {
            updateFrustumPlanes();
        }
        void updateFrustumPlanes() {
            glm::mat4 viewProj = glm::transpose(getProjectionMatrix() * getViewMatrix());
            // Left
            frustumPlanes[0] = viewProj[3] + viewProj[0];
            // Right
            frustumPlanes[1] = viewProj[3] - viewProj[0];
            // Bottom
            frustumPlanes[2] = viewProj[3] + viewProj[1];
            // Top
            frustumPlanes[3] = viewProj[3] - viewProj[1];
            // Near
            frustumPlanes[4] = viewProj[3] + viewProj[2];
            // Far
            frustumPlanes[5] = viewProj[3] - viewProj[2];
            for (auto& plane : frustumPlanes) {
                float length = glm::length(glm::vec3(plane));
                plane /= length;
            }
        }
        bool isSphereInFrustum(const glm::vec3& center, float radius) const {
            for (const auto& plane : frustumPlanes) {
                if (glm::dot(glm::vec3(plane), center) + plane.w + radius < 0) {
                    return false;
                }
            }
            return true;
        }
        bool isAABBInFrustum(const engine::AABB& aabb, const glm::mat4& transform) const {
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
            for (const auto& plane : frustumPlanes) {
                int out = 0;
                for (const auto& corner : corners) {
                    glm::vec3 worldCorner = glm::vec3(transform * glm::vec4(corner, 1.0f));
                    if (glm::dot(glm::vec3(plane), worldCorner) + plane.w < 0) {
                        out++;
                    }
                }
                if (out == 8) {
                    return false;
                }
            }
            return true;
        }
        const std::array<glm::vec4, 6>& getFrustumPlanes() const {
            return frustumPlanes;
        }
    private:
        float fovY;
        float aspectRatio;
        float nearPlane;
        float farPlane;
        std::array<glm::vec4, 6> frustumPlanes;
    };
};
