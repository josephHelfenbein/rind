#pragma once
#include <random>

namespace rind {
    struct StatusEffect {
        float jumpSpeed = 1.5f;
        float moveSpeed = 10.0f;
        float gravity = 20.0f;
        std::string statusText = "";
        glm::vec3 overlayColor = glm::vec3(1.0f);
        float damageDoneMultiplier = 1.0f;
        float damageCapableMultiplier = 1.0f;
        float resetTime = 6.0f;
    };
    static const StatusEffect mainStatusEffect{};
    static const StatusEffect fastMoveStatusEffect{
        .moveSpeed = 20.0f,
        .statusText = "SPEED UP +100%",
        .overlayColor = glm::vec3(1.0f, 0.5f, 0.5f)
    };
    static const StatusEffect highJumpStatusEffect{
        .jumpSpeed = 2.5f,
        .statusText = "HIGH JUMP",
        .overlayColor = glm::vec3(0.5f, 0.5f, 1.0f)
    };
    static const StatusEffect lowGravityStatusEffect{
        .gravity = 7.0f,
        .statusText = "LOW GRAVITY",
        .overlayColor = glm::vec3(0.5f, 1.0f, 0.5f)
    };
    static std::vector<StatusEffect> statusEffects{
        fastMoveStatusEffect,
        highJumpStatusEffect,
        lowGravityStatusEffect
    };

    static const StatusEffect& getRandomStatusEffect(float randomValue) { // randomValue should be between 0 and 1
        int index = static_cast<int>(randomValue * statusEffects.size());
        index = std::max(0, std::min(index, static_cast<int>(statusEffects.size() - 1)));
        return statusEffects[index];
    }
};