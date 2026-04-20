#include <rind/BashingBoss.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <glm/gtc/quaternion.hpp>
#include <rind/SlowBullet.h>
#include <cmath>
#include <numbers>

rind::BashingBoss::BashingBoss(
    engine::EntityManager* entityManager,
    rind::Player* player,
    rind::GameInstance* gameInstance,
    const std::string& name,
    const glm::mat4& transform,
    uint32_t& enemyCount
) : rind::BashingEnemy(entityManager, player, gameInstance, name, transform, enemyCount) {
        setMaxHealth(300.0f);
        setHealth(300.0f);
        setTransform(
            glm::scale(transform, glm::vec3(1.5f))
        );
        std::vector<std::string> bossMaterial = {
            "materials_boss_albedo",
            "materials_boss_metallic",
            "materials_boss_roughness",
            "materials_boss_normal"
        };
        enemyModel->setTextures(bossMaterial);
    }

void rind::BashingBoss::update(float deltaTime) {
    BashingEnemy::update(deltaTime);
    if (glm::dot(getPressed(), getPressed()) > 0.0f) {
        float dashChance = (dist(rng) + 1.0f) * 0.5f; // 0.0 to 1.0
        // 5% chance each frame to dash if moving, with a cooldown
        if (dashChance >= 0.95f && std::chrono::steady_clock::now() - lastDashTime > std::chrono::milliseconds(dashCooldown)) {
            glm::vec3 dashDirection = glm::normalize(getPressed());
            dash(dashDirection, 100.0f);
            particleManager->burstParticles(
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                getTrailColor(),
                -glm::normalize(getVelocity()) * 15.0f,
                50,
                2.0f,
                2.0f,
                1.2f
            );
            particleManager->burstParticles(
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                getTrailColor(),
                -glm::normalize(getVelocity()) * 10.0f,
                50,
                2.0f,
                2.0f,
                0.5f
            );
            audioManager->playSound3D("player_dash", getWorldPosition(), 0.5f, 0.15f);
            lastDashTime = std::chrono::steady_clock::now();
        }
    }
}