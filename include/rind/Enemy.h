#pragma once

#include <engine/CharacterEntity.h>
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

    private:
        EnemyState state = EnemyState::Idle;
        Player* targetPlayer = nullptr;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    };
};