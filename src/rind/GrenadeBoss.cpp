#include <rind/GrenadeBoss.h>
#include <rind/Grenade.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <glm/gtc/quaternion.hpp>
#include <rind/SlowBullet.h>
#include <cmath>
#include <numbers>

rind::GrenadeBoss::GrenadeBoss(
    engine::EntityManager* entityManager,
    rind::Player* player,
    rind::GameInstance* gameInstance,
    const std::string& name,
    const glm::mat4& transform,
    uint32_t& enemyCount
) : rind::FlyingEnemy(entityManager, player, gameInstance, name, transform, enemyCount) {
        setMaxHealth(300.0f);
        setHealth(300.0f);
        setTransform(
            glm::scale(transform, glm::vec3(1.75f))
        );
        std::vector<std::string> bossMaterial = {
            "materials_boss_albedo",
            "materials_boss_metallic",
            "materials_boss_roughness",
            "materials_boss_normal"
        };
        enemyModel->setTextures(bossMaterial);
    }

void rind::GrenadeBoss::shoot() {
    glm::vec3 headForward = -glm::normalize(glm::vec3(getHead()->getWorldTransform()[2]));
    glm::vec3 bodyForward = -glm::normalize(glm::vec3(getWorldTransform()[2]));
    bodyForward.y = 0.0f;
    if (glm::length2(bodyForward) > 1e-6f) {
        bodyForward = glm::normalize(bodyForward);
    } else {
        bodyForward = glm::vec3(0.0f, 0.0f, -1.0f);
    }
    const float verticalAim = headForward.y;
    glm::vec3 gunPos = glm::vec3(gunEndPosition->getWorldTransform()[3]);
    glm::vec3 vel = getVelocity();
    Grenade* grenade = new Grenade(
        getEntityManager(),
        nullptr,
        glm::translate(glm::mat4(1.0f), gunPos + bodyForward * 0.7f + glm::vec3(0.0f, verticalAim * 0.2f, 0.0f)),
        bodyForward * 20.0f + glm::vec3(0.0f, 3.0f + verticalAim * 20.0f, 0.0f) + vel * 0.25f,
        getTrailColor(),
        6.0f
    );
    audioManager->playSound3D("player_throw", gunPos, 0.5f, 0.2f);
}

