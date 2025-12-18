#pragma once

#include <engine/EntityManager.h>
#include <engine/Collider.h>

namespace engine {
    class CharacterEntity : public Entity {
    public:
        CharacterEntity(EntityManager* entityManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures = {})
            : Entity(entityManager, name, shader, transform, textures, true) {}
        
        void update(float deltaTime) override;
        void move(const glm::vec3& delta);
        void stopMove(const glm::vec3& delta);
        void jump(float strength);
        void rotate(const glm::vec3& delta);

        void setCollider(OBBCollider* collider) { this->collider = collider; }

        Collider::Collision willCollide(const glm::mat4& deltaTransform);
    private:
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 pressed = glm::vec3(0.0f);
        OBBCollider* collider = nullptr;
        const float moveSpeed = 10.0f;
        const float jumpSpeed = 1.5f;
        const float coyoteTime = 0.10f;
        const float groundedNormalThreshold = 0.5f; // normals with y > threshold count as ground
        float gravity = 9.81f;
        bool grounded = false;
        float groundedTimer = 1.0f;
    };
}