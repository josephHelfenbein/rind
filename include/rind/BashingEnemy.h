#pragma once

#include <rind/Enemy.h>

namespace rind {
    class BashingEnemy : public rind::Enemy {
    public:
        BashingEnemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, glm::mat4 transform, uint32_t& enemyCount);

        void update(float deltaTime) override;

        void wander() override;
        void wanderTo(float deltaTime) override;

        void hit();
    private:
        uint32_t scoreWorth = 200u;
    };
};
