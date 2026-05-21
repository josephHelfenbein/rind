#pragma once

#include <engine/EntityManager.h>
#include <engine/Collider.h>

namespace rind {
    class TempTrigger : public engine::OBBCollider{
    public:
        TempTrigger(
            engine::EntityManager* entityManager,
            const std::string& name,
            const glm::vec3& color,
            const glm::mat4& transform,
            float lifetime = 1.0f
        ) : engine::OBBCollider(entityManager, transform, name), lifetime(lifetime), color(color) {
            setIsTrigger(true);
        }

        void update(float deltaTime) override {
            lifetime -= deltaTime;
            if (lifetime <= 0.0f) {
                getEntityManager()->markForDeletion(this);
            }
        }

        glm::vec3 getColor() const { return color; }

    private:
        float lifetime = 0.5f;
        glm::vec3 color{1.0f, 1.0f, 1.0f};
    };
};