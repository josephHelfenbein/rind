#pragma once

#include <engine/EntityManager.h>
#include <engine/Collider.h>

namespace engine {
    class CharacterEntity : public Entity {
    public:
        CharacterEntity(EntityManager* entityManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures = {})
            : Entity(entityManager, name, shader, transform, textures, true) {
                Collider* collider = new AABBCollider(entityManager, transform, name, glm::vec3(0.5f, 1.0f, 0.5f));
                addChild(collider);
            }
        
        void update(float deltaTime) override;
        void move(const glm::vec3& delta);
        void stopMove(const glm::vec3&delta);
        void jump(float strength);
        void resetVelocity();
        void rotate(const glm::vec3& delta);
    private:
        glm::vec3 velocity = glm::vec3(0.0f);
        glm::vec3 pressed = glm::vec3(0.0f);
        const float moveSpeed = 10.0f;
        const float jumpSpeed = 10.0f;
        const float groundedNormalThreshold = 0.5f;
        const float coyoteTime = 0.10f;
        bool grounded = false;
        float groundedTimer = 1.0f;
        
        static bool aabbIntersects(const AABB& a, const AABB& b, float margin = 0.0f) {
            return (a.min.x - margin <= b.max.x && a.max.x + margin >= b.min.x) &&
                   (a.min.y - margin <= b.max.y && a.max.y + margin >= b.min.y) &&
                   (a.min.z - margin <= b.max.z && a.max.z + margin >= b.min.z);
        }
        Collider::Collision willCollide(const glm::mat4& deltaTransform);
    };
}