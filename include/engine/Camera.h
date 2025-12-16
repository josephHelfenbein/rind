#pragma once

#include <engine/EntityManager.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace engine {
    class Camera  : public Entity {
    public:
        Camera(EntityManager* entityManager, const std::string& name, glm::mat4 transform, float fovY, float aspectRatio, float nearPlane, float farPlane, bool isMovable = true)
            : Entity(entityManager, name, "", transform, {}, isMovable),
              fovY(fovY), aspectRatio(aspectRatio), nearPlane(nearPlane), farPlane(farPlane) {}

        void setPerspective(float fovY, float aspectRatio, float nearPlane, float farPlane) {
            this->fovY = fovY;
            this->aspectRatio = aspectRatio;
            this->nearPlane = nearPlane;
            this->farPlane = farPlane;
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
            return glm::perspective(glm::radians(fovY), aspectRatio, nearPlane, farPlane);
        }
    private:
        float fovY;
        float aspectRatio;
        float nearPlane;
        float farPlane;
    };
};