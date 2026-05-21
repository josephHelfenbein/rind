#pragma once
#include <random>

namespace rind {
    struct StatusEffect {
        float jumpSpeed = 1.5f;
        float moveSpeed = 10.0f;
        float gravity = 20.0f;
        std::string statusText = "";
        glm::vec3 textColor = glm::vec3(1.0f);
        glm::vec3 overlayColor = glm::vec3(1.0f);
        float strengthMultiplier = 1.0f;
        float protectionMultiplier = 1.0f;
        float grenadeCooldown = 4.0f;
        float resetTime = 6.0f;
    };
    static const StatusEffect mainStatusEffect{};
    static const StatusEffect fastMoveStatusEffect{
        .moveSpeed = 20.0f,
        .statusText = "SPEED UP",
        .textColor = glm::vec3(0.7f, 1.0f, 0.7f),
        .overlayColor = glm::vec3(1.0f, 0.5f, 0.5f)
    };
    static const StatusEffect highJumpStatusEffect{
        .jumpSpeed = 3.0f,
        .statusText = "HIGH JUMP",
        .textColor = glm::vec3(0.7f, 1.0f, 0.7f),
        .overlayColor = glm::vec3(0.5f, 0.5f, 1.0f)
    };
    static const StatusEffect lowGravityStatusEffect{
        .gravity = 5.0f,
        .statusText = "LOW GRAVITY",
        .textColor = glm::vec3(0.7f, 1.0f, 0.7f),
        .overlayColor = glm::vec3(0.5f, 1.0f, 0.5f)
    };
    static const StatusEffect damageBoostStatusEffect{
        .statusText = "DAMAGE BOOST",
        .textColor = glm::vec3(0.7f, 1.0f, 0.7f),
        .overlayColor = glm::vec3(1.0f, 0.5f, 1.0f),
        .strengthMultiplier = 1.5f
    };
    static const StatusEffect instantKillStatusEffect{
        .statusText = "INSTANT KILL",
        .textColor = glm::vec3(0.7f, 1.0f, 0.7f),
        .overlayColor = glm::vec3(0.8f, 0.2f, 0.2f),
        .strengthMultiplier = 3.5f
    };
    static const StatusEffect infiniteGrenadesStatusEffect{
        .statusText = "INFINITE GRENADES",
        .textColor = glm::vec3(0.7f, 1.0f, 0.7f),
        .overlayColor = glm::vec3(0.2f, 0.9f, 0.3f),
        .grenadeCooldown = 0.1f
    };
    static const StatusEffect armorBoostStatusEffect{
        .statusText = "ARMOR BOOST",
        .textColor = glm::vec3(0.7f, 1.0f, 0.7f),
        .overlayColor = glm::vec3(0.5f, 1.0f, 1.0f),
        .protectionMultiplier = 2.0f
    };
    static const StatusEffect slowMoveStatusEffect{
        .moveSpeed = 5.0f,
        .statusText = "SLOW DOWN",
        .textColor = glm::vec3(1.0f, 0.65f, 0.6f),
        .overlayColor = glm::vec3(0.5f, 0.5f, 0.5f)
    };
    static const StatusEffect anchorStatusEffect{
        .jumpSpeed = 0.5f,
        .moveSpeed = 2.0f,
        .gravity = 30.0f,
        .statusText = "ANCHOR",
        .textColor = glm::vec3(1.0f, 0.65f, 0.6f),
        .overlayColor = glm::vec3(0.25f, 0.25f, 0.25f),
        .strengthMultiplier = 0.5f
    };
    static const StatusEffect weakStatusEffect{
        .statusText = "WEAKNESS",
        .textColor = glm::vec3(1.0f, 0.65f, 0.6f),
        .overlayColor = glm::vec3(0.5f, 0.25f, 0.25f),
        .strengthMultiplier = 0.75f,
        .protectionMultiplier = 0.75f
    };
    static const StatusEffect fragileStatusEffect{
        .statusText = "FRAGILITY",
        .textColor = glm::vec3(1.0f, 0.65f, 0.6f),
        .overlayColor = glm::vec3(0.25f, 0.25f, 0.25f),
        .strengthMultiplier = 0.5f,
        .protectionMultiplier = 0.5f
    };
    static std::vector<StatusEffect> statusEffects{
        fastMoveStatusEffect,
        highJumpStatusEffect,
        lowGravityStatusEffect,
        damageBoostStatusEffect,
        instantKillStatusEffect,
        infiniteGrenadesStatusEffect,
        armorBoostStatusEffect,
        slowMoveStatusEffect,
        weakStatusEffect,
        fragileStatusEffect,
        anchorStatusEffect
    };

    static const StatusEffect& getRandomStatusEffect(float randomValue) { // randomValue should be between 0 and 1
        int index = static_cast<int>(randomValue * statusEffects.size());
        index = std::max(0, std::min(index, static_cast<int>(statusEffects.size() - 1)));
        return statusEffects[index];
    }
};