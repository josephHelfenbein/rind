#pragma once

#include <engine/EntityManager.h>
#include <engine/Collider.h>
#include <engine/AudioManager.h>

namespace engine {
    class CharacterEntity : public Entity {
    public:
        CharacterEntity(EntityManager* entityManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures = {})
            : Entity(entityManager, name, shader, transform, textures, true) {}
        
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

        const glm::vec3& getPressed() const { return pressed; }
        const glm::vec3& getVelocity() const { return velocity; }

        void setCollider(OBBCollider* collider) { this->collider = collider; }
        OBBCollider* getCollider() const { return collider; }

        void setHead(Entity* head) { this->head = head; }
        Entity* getHead() const { return head; }

        Collider::Collision willCollide(const glm::mat4& deltaTransform);
    private:
        float health = 100.0f;
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 pressed = glm::vec3(0.0f);
        glm::vec3 dashing = glm::vec3(0.0f);
        glm::vec3 dashVelocity = glm::vec3(0.0f); // persistent dash impulse that decays
        OBBCollider* collider = nullptr;
        Entity* head = nullptr;
        const float moveSpeed = 10.0f;
        const float dashDecayRate = 8.0f; // how fast dash velocity decays
        const float jumpSpeed = 1.5f;
        const float coyoteTime = 0.10f;
        const float groundedNormalThreshold = 0.5f; // normals with y > threshold count as ground
        float gravity = 9.81f;
        bool grounded = false;
        float groundedTimer = 1.0f;
    };
}