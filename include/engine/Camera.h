#pragma once

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <engine/EntityManager.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {
    class Camera  : public Entity {
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
    private:
        float fovY;
        float aspectRatio;
        float nearPlane;
        float farPlane;
    };
};