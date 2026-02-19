#pragma once

#include <rind/Enemy.h>

namespace rind {
    class WalkingEnemy : public rind::Enemy {
    public:
        WalkingEnemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, glm::mat4 transform, uint32_t& enemyCount);
 
        void update(float deltaTime) override;

        void wander() override;
        void wanderTo(float deltaTime) override;
    private:
        float cachedMaxSafeBackup = 0.0f;
        float backupSearchLo = 0.0f;
        float backupSearchHi = 15.0f;
        uint32_t getScoreWorth() const override { return 100u; }
    };
};
