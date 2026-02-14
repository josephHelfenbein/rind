#pragma once

#include <engine/EntityManager.h>
#include <engine/Collider.h>
#include <random>

namespace rind {
    class SlowBullet : public engine::Entity {
    public:
        SlowBullet(engine::EntityManager* entityManager, const std::string& name, glm::mat4 transform, const glm::vec3 velocity, const glm::vec4 color);
        void update(float deltaTime) override;

    private:
        glm::vec3 velocity;
        glm::vec4 color;
        float lifetime = 20.0f;
        float timeAlive = 0.0f;
        engine::OBBCollider* collider = nullptr;
        engine::ParticleManager* particleManager = nullptr;
        engine::AudioManager* audioManager = nullptr;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    };
};