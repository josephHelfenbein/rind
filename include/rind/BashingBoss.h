#pragma once

#include <rind/BashingEnemy.h>

namespace rind {
    class BashingBoss : public BashingEnemy {
    public:
        BashingBoss(
            engine::EntityManager* entityManager,
            rind::Player* player,
            rind::GameInstance* gameInstance,
            const std::string& name,
            const glm::mat4& transform,
            uint32_t& enemyCount
        );

        void update(float deltaTime) override;
    private:
        uint32_t getScoreWorth() const override { return 300u; }
        long long dashCooldown = 500; // ms
        std::chrono::steady_clock::time_point lastDashTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(dashCooldown);
    };
};