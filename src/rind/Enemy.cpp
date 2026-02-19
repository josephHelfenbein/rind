#include <rind/Enemy.h>
#include <engine/ParticleManager.h>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>

#define PI 3.14159265358979323846f

rind::Enemy::Enemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, glm::mat4 transform, uint32_t& enemyCount)
    : engine::CharacterEntity(entityManager, name, "", transform, {}), targetPlayer(player), enemyCount(enemyCount) {
        if (player == nullptr) {
            throw std::runtime_error("Enemy spawned without player reference");
        }
        audioManager = getEntityManager()->getRenderer()->getAudioManager();
        particleManager = getEntityManager()->getRenderer()->getParticleManager();
    }

void rind::Enemy::shoot() {
    glm::vec3 rayDir = -glm::normalize(glm::vec3(glm::rotate(getHead()->getWorldTransform(), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f))[2]));
    glm::vec3 gunPos = gunEndPosition->getWorldPosition();
    particleManager->burstParticles(
        glm::translate(glm::mat4(1.0f), gunPos),
        trailColor,
        rayDir * 15.0f,
        10,
        3.0f,
        0.3f
    );
    std::vector<engine::Collider::Collision> hits = engine::Collider::raycast(
        getEntityManager(),
        gunPos,
        rayDir,
        1000.0f,
        this->getCollider(),
        true
    );
    glm::vec3 endPos = gunPos + rayDir * 1000.0f;
    if (!hits.empty()) {
        engine::Collider::Collision collision = hits[0];
        endPos = collision.worldHitPoint;
        glm::vec3 normal = glm::normalize(collision.mtv.normal);
        glm::vec3 reflectedDir = glm::reflect(rayDir, normal);
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            trailColor,
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            trailColor,
            reflectedDir * 25.0f,
            30,
            4.0f,
            0.4f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            trailColor,
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.3f
        );
        if (rind::Player* character = dynamic_cast<rind::Player*>(collision.other->getParent())) {
            character->damage(5.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, true);
        } else if (rind::Enemy* character = dynamic_cast<rind::Enemy*>(collision.other->getParent())) {
            character->damage(5.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, true);
        } else {
            audioManager->playSound3D("laser_ground_impact", collision.worldHitPoint, 0.5f, true);
        }
    }
    audioManager->playSound3D("laser_shot", gunPos, 0.5f, true);
    trailFramesRemaining = maxTrailFrames;
    trailEndPos = endPos;
}

void rind::Enemy::update(float deltaTime) {
    engine::CharacterEntity::update(deltaTime);
    if (trailFramesRemaining > 0) {
        float deltaTime = getEntityManager()->getRenderer()->getDeltaTime();
        glm::vec3 velocityOffset = getVelocity() * deltaTime;
        glm::vec3 currentGunEndPos = glm::vec3(gunEndPosition->getWorldTransform()[3]) + velocityOffset;
        particleManager->spawnTrail(
            currentGunEndPos,
            trailEndPos - currentGunEndPos,
            trailColor,
            deltaTime * 2.0f,
            (static_cast<float>(maxTrailFrames) - static_cast<float>(trailFramesRemaining)) / static_cast<float>(maxTrailFrames) * deltaTime
        );
        trailFramesRemaining--;
    }
}

void rind::Enemy::rotateToPlayer() {
    glm::vec3 toPlayer = targetPlayer->getWorldPosition() + glm::vec3(0.0f, 1.0f, 0.0f) - getWorldPosition();
    toPlayer.y = 0.0f;
    float distanceToPlayer = glm::length(toPlayer);
    glm::mat4 t = getTransform();
    glm::vec3 forward = -glm::vec3(t[2]);
    forward.y = 0.0f;
    forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 targetDir = glm::normalize(glm::vec3(toPlayer.x, 0.0f, toPlayer.z));
    float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
    float angle = acos(dot);
    glm::vec3 cross = glm::cross(forward, targetDir);
    float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
    float rotationAmount = angle * rotationDir;
    rotate(glm::vec3(0.0f, rotationAmount, 0.0f));
}

bool rind::Enemy::checkVisibilityOfPlayer() {
    if (!targetPlayer) {
        return false;
    }
    engine::AABB playerAABB = targetPlayer->getCollider()->getWorldAABB();
    std::array<glm::vec3, 8> corners = engine::Collider::getCornersFromAABB(visionBox);
    for (auto& corner : corners) {
        corner = glm::vec3(getWorldTransform() * glm::vec4(corner, 1.0f));
    }
    engine::AABB enemyVisionBox = engine::Collider::aabbFromCorners(corners);
    return engine::Collider::aabbIntersects(enemyVisionBox, playerAABB);
}

void rind::Enemy::damage(float amount) {
    setHealth(getHealth() - amount);
    if (getHealth() <= 0.0f) {
        targetPlayer->addScore(getScoreWorth());
        getEntityManager()->markForDeletion(this);
    }
}
