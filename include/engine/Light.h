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
            }

        glm::vec3 getColor() const { return color; }
        void setColor(const glm::vec3& color) { this->color = color; }

        float getIntensity() const { return intensity; }
        void setIntensity(float intensity) { this->intensity = intensity; }

        float getRadius() const { return radius; }
        void setRadius(float radius) { this->radius = radius; }

        PointLight getPointLightData() {
            glm::vec3 worldPos = getWorldPosition();
            PointLight pl = {
                .positionRadius = glm::vec4(worldPos, radius),
                .colorIntensity = glm::vec4(color, intensity),
                .lightViewProj = {},
                .shadowParams = glm::vec4(0.0f),
                .shadowData = glm::uvec4(0)
            };
            return pl;
        }

    private:
        glm::vec3 color;
        float intensity;
        float radius;
    };
}