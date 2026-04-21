#pragma once

#include <rind/FlyingEnemy.h>

namespace rind {
    class MissileBoss : public FlyingEnemy {
    public:
        MissileBoss(
            engine::EntityManager* entityManager,
            rind::Player* player,
            rind::GameInstance* gameInstance,
            const std::string& name,
            const glm::mat4& transform,
            uint32_t& enemyCount
        );

        void shoot() override;
    protected:
        glm::vec3 getTrailColor() const override { return glm::vec3(1.0f, 0.15f, 0.65f); }
    private:
        uint32_t getScoreWorth() const override { return 1000u; }
    };
};