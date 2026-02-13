#pragma once

#include <rind/Enemy.h>

namespace rind {
    class FlyingEnemy : public rind::Enemy {
    public:
        FlyingEnemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, glm::mat4 transform, uint32_t& enemyCount);
 
        void update(float deltaTime) override;
        
        void shoot() override;

        void wander() override;
        void wanderTo(float deltaTime) override;
    private:
        uint32_t spawnedBullets = 0u;
        glm::vec4 trailColor = glm::vec4(1.0f, 1.0f, 0.0f, 1.0f);
        uint32_t scoreWorth = 150u;
    };
};
