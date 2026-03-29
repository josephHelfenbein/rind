#pragma once

#include <engine/EntityManager.h>
#include <engine/Collider.h>
#include <engine/AudioManager.h>

namespace rind {
    class CharacterEntity : public engine::Entity {
    public:
        CharacterEntity(
            engine::EntityManager* entityManager,
            const std::string& name,
            const std::string& shader,
            const glm::mat4& transform,
            std::vector<std::string> textures = {},
            const engine::Entity::EntityType& type = Entity::EntityType::Character
        ) : Entity(entityManager, name, shader, transform, textures, true, type) {}
        
        void update(float deltaTime) override;
        void updateMovement(float deltaTime);
        virtual void damage(float amount) { health -= amount; }

        void move(const glm::vec3& delta, bool remap = true);
        void stopMove(const glm::vec3& delta, bool remap = true);
        void jump(float strength);
        void rotate(const glm::vec3& delta);
        void dash(const glm::vec3& direction, float strength);

        const float getHealth() const { return health; }
        void setHealth(float health) { this->health = health; }
        const float getMaxHealth() const { return maxHealth; }
        void setMaxHealth(float maxHealth) { this->maxHealth = maxHealth; }

        const glm::vec3& getPressed() const { return pressed; }
        const glm::vec3& getVelocity() const { return velocity; }
        void setVelocity(const glm::vec3& velocity) { this->velocity = velocity; }
        void setGravityEnabled(bool enabled) { gravityEnabled = enabled; }
        void setGravity(float gravity) { this->gravity = gravity; }
        void setJumpSpeed(float jumpSpeed) { this->jumpSpeed = jumpSpeed; }
        void setMoveSpeed(float moveSpeed) { this->moveSpeed = moveSpeed; }

        void setCollider(engine::OBBCollider* collider) { this->collider = collider; }
        engine::OBBCollider* getCollider() const { return collider; }

        void setHead(Entity* head) { this->head = head; }
        Entity* getHead() const { return head; }

        const glm::vec3& getRotateVelocity() const { return rotateVelocity; }

        engine::Collider::Collision willCollide(const glm::vec3& worldOffset);
        bool isGrounded() const { return grounded || groundedTimer <= coyoteTime; }

    private:
        float health = 100.0f;
        float maxHealth = 100.0f;
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 pressed = glm::vec3(0.0f);
        glm::vec3 dashing = glm::vec3(0.0f);
        glm::vec3 dashVelocity = glm::vec3(0.0f); // persistent dash impulse that decays
        engine::OBBCollider* collider = nullptr;
        engine::Entity* head = nullptr;
        float moveSpeed = 10.0f;
        const float dashDecayRate = 8.0f; // how fast dash velocity decays
        float jumpSpeed = 1.5f;
        const float coyoteTime = 0.10f;
        glm::vec3 rotateVelocity = glm::vec3(0.0f);
        const float groundedNormalThreshold = 0.5f; // normals with y > threshold count as ground
        float gravity = 20.0f;
        bool gravityEnabled = true;
        bool grounded = false;
        float groundedTimer = 1.0f;
    };
}
