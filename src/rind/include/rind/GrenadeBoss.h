#pragma once

#include <rind/FlyingEnemy.h>

namespace rind {
    class GrenadeBoss : public FlyingEnemy {
    public:
        GrenadeBoss(
            engine::EntityManager* entityManager,
            rind::Player* player,
            rind::GameInstance* gameInstance,
            const std::string& name,
            const glm::mat4& transform,
            uint32_t& enemyCount
        );

        void shoot() override;
    protected:
        glm::vec3 getTrailColor() const override { return glm::vec3(1.0f, 0.5f, 0.0f); }
    private:
        int32_t getScoreWorth() const override { return 500; }
    };
};