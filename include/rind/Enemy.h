#pragma once

#include <engine/CharacterEntity.h>
#include <engine/AudioManager.h>
#include <engine/ParticleManager.h>
#include <rind/Player.h>
#include <random>

namespace rind {
    enum class EnemyState {
        Idle,
        Chasing,
        Attacking
    };
    class Enemy : public engine::CharacterEntity {
    public:
        Enemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures);

        void update(float deltaTime) override;
        void damage(float amount) override;

        void wander();
        void wanderTo(float deltaTime);
        void setWanderTarget(const glm::vec3& target) {
            waiting = false;
            wandering = true;
            wanderTarget = target;
        }

        void shoot();

        bool checkVisibilityOfPlayer();

        EnemyState getState() const { return state; }
        
        void rotateToPlayer();

    private:
        engine::AudioManager* audioManager = nullptr;
        engine::ParticleManager* particleManager = nullptr;
        EnemyState state = EnemyState::Idle;
        Player* targetPlayer = nullptr;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

        engine::AABB visionBox = {
            .min = glm::vec3(-5.0f, -2.5f, -20.0f),
            .max = glm::vec3(5.0f, 2.5f, 0.0f)
        };

        float shootingCooldown = 0.5f;
        std::chrono::steady_clock::time_point lastShotTime = std::chrono::steady_clock::now();

        glm::vec3 wanderTarget = glm::vec3(0.0f);
        bool wandering = false;
        bool waiting = false;

        engine::Entity* gunEndPosition = nullptr;

        uint32_t trailFramesRemaining = 0u;
        uint32_t maxTrailFrames = 5u;
        glm::vec3 trailEndPos = glm::vec3(0.0f);
        glm::vec4 trailColor = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
    };
};