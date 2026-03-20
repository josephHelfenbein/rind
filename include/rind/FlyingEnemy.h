#pragma once

#include <rind/Enemy.h>

namespace rind {
    class FlyingEnemy : public rind::Enemy {
    public:
        FlyingEnemy(
            engine::EntityManager* entityManager,
            rind::Player* player,
            rind::GameInstance* gameInstance,
            const std::string& name,
            const glm::mat4& transform,
            uint32_t& enemyCount
        );
 
        void update(float deltaTime) override;
        
        void shoot() override;

        void wander() override;
        void wanderTo(float deltaTime) override;
    private:
        uint32_t spawnedBullets = 0u;
        glm::vec3 getTrailColor() const override { return glm::vec3(1.0f, 1.0f, 0.0f); }
        uint32_t getScoreWorth() const override { return 150u; }
    };
};
