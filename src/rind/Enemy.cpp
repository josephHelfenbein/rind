#include <rind/Enemy.h>
#include <rind/TempTrigger.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <glm/gtc/quaternion.hpp>
#include <stdexcept>

rind::Enemy::Enemy(
    engine::EntityManager* entityManager,
    rind::Player* player,
    rind::GameInstance* gameInstance,
    const std::string& name,
    const glm::mat4& transform,
    uint32_t& enemyCount
) : rind::CharacterEntity(entityManager, name, "", transform, {}, engine::Entity::EntityType::Enemy), targetPlayer(player), enemyCount(enemyCount), gameInstance(gameInstance) {
        if (player == nullptr) {
            throw std::runtime_error("Enemy spawned without player reference");
        }
        audioManager = getEntityManager()->getRenderer()->getAudioManager();
        particleManager = getEntityManager()->getRenderer()->getParticleManager();
        volumetricManager = getEntityManager()->getRenderer()->getVolumetricManager();
    }

void rind::Enemy::shoot() {
    glm::vec3 rayDir = glm::normalize(glm::vec3(getHead()->getWorldTransform()[0]));
    glm::vec3 gunPos = gunEndPosition->getWorldPosition();
    particleManager->burstParticles(
        gunPos,
        getTrailColor(),
        rayDir * 10.0f,
        20,
        1.5f,
        0.35f,
        0.8f
    );
    particleManager->burstParticles(
        gunPos,
        getTrailColor(),
        rayDir * 15.0f,
        60,
        2.0f,
        0.35f,
        0.3f
    );
    engine::Collider::Collision hit = engine::Collider::raycastFirst(
        getEntityManager(),
        gunPos,
        rayDir,
        1000.0f,
        this->getCollider()
    );
    glm::vec3 endPos = gunPos + rayDir * 1000.0f;
    if (hit.other) {
        engine::Collider::Collision collision = hit;
        endPos = collision.worldHitPoint;
        glm::vec3 normal = glm::normalize(collision.mtv.normal);
        glm::vec3 reflectedDir = glm::reflect(rayDir, normal);
        particleManager->burstParticles(
            collision.worldHitPoint,
            getTrailColor(),
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f,
            0.9f
        );
        particleManager->burstParticles(
            collision.worldHitPoint,
            getTrailColor(),
            reflectedDir * 25.0f,
            100,
            4.0f,
            0.4f,
            0.3f
        );
        particleManager->burstParticles(
            collision.worldHitPoint,
            getTrailColor(),
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.3f,
            0.7f
        );
        particleManager->burstParticles(
            collision.worldHitPoint,
            getTrailColor(),
            reflectedDir * 30.0f,
            40,
            3.0f,
            0.35f,
            1.1f
        );
        engine::Entity* other = collision.other->getParent();
        if (other->getType() == engine::Entity::EntityType::Player) {
            Player* player = static_cast<Player*>(other);
            player->damage(5.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, 0.2f);
        } else if (other->getType() == engine::Entity::EntityType::Enemy) {
            rind::Enemy* enemy = static_cast<rind::Enemy*>(other);
            enemy->damage(5.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, 0.2f);
        } else {
            audioManager->playSound3D("laser_ground_impact", collision.worldHitPoint, 0.5f, 0.2f);
        }
    }
    audioManager->playSound3D("laser_shot", gunPos, 0.5f, 0.2f);
    trailFramesRemaining = maxTrailFrames;
    trailEndPos = endPos;
}

void rind::Enemy::update(float deltaTime) {
    rind::CharacterEntity::update(deltaTime);
    if (trailFramesRemaining > 0) {
        float deltaTime = getEntityManager()->getRenderer()->getDeltaTime();
        glm::vec3 velocityOffset = getVelocity() * deltaTime;
        glm::vec3 currentGunEndPos = glm::vec3(gunEndPosition->getWorldTransform()[3]) + velocityOffset;
        glm::vec3 rayDir = -glm::normalize(glm::vec3(glm::rotate(getHead()->getWorldTransform(), glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f))[2]));
        if (trailFramesRemaining == maxTrailFrames) {
            volumetricManager->createVolumetric(
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 0.1f
                    ),
                    glm::vec3(0.2f, 0.2f, 0.2f)
                ),
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 0.8f
                    ),
                    glm::vec3(1.6f, 1.6f, 1.6f)
                ),
                glm::vec4(glm::min(getTrailColor() + glm::vec3(0.1f), glm::vec3(1.0f)), 15.0f),
                0.1f
            );
            volumetricManager->createVolumetric(
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 0.25f
                    ),
                    glm::vec3(0.5f, 0.5f, 0.5f)
                ),
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 2.5f
                    ),
                    glm::vec3(5.0f, 5.0f, 5.0f)
                ),
                glm::vec4(0.1f, 0.1f, 0.1f, 0.6f),
                4.0f
            );
        }
        particleManager->spawnTrail(
            currentGunEndPos,
            trailEndPos - currentGunEndPos,
            getTrailColor(),
            deltaTime * 2.0f,
            (static_cast<float>(maxTrailFrames) - static_cast<float>(trailFramesRemaining)) / static_cast<float>(maxTrailFrames) * deltaTime
        );
        trailFramesRemaining--;
    }
    float spawnCloud = pow(dist(rng) + 1.0f, (getMaxHealth() - getHealth()) / getMaxHealth());
    if (spawnCloud < 0.2f) {
        volumetricManager->createVolumetric(
            glm::scale(
                glm::translate(glm::mat4(1.0f), getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f)),
                glm::vec3(2.0f, 2.0f, 2.0f)
            ),
            glm::scale(
                glm::translate(glm::mat4(1.0f), getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f)),
                glm::vec3(20.0f, 20.0f, 20.0f)
            ),
            glm::vec4(0.1f, 0.1f, 0.1f, 0.6f),
            2.0f
        );
        audioManager->playSound3D("enemy_smoke", getWorldPosition(), 0.8f, 0.5f);
    }
    float playTalk = dist(rng) + 1.0f;
    if (playTalk > 1.999f) {
        audioManager->playSound3D("enemy_talk", getWorldPosition(), 0.5f, 0.5f);
    }
    shootingCooldown = 1.5f / (static_cast<float>(gameInstance->getDifficultyLevel()) + 1.0f);
}

void rind::Enemy::rotateToPlayer() {
    glm::vec3 toPlayer = targetPlayer->getWorldPosition() + glm::vec3(0.0f, 1.0f, 0.0f) - getWorldPosition();
    toPlayer.y = 0.0f;
    float distanceToPlayer = glm::length(toPlayer);
    const glm::mat4& t = getTransform();
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
    const glm::mat4 invEnemyWorld = glm::inverse(getWorldTransform());
    engine::AABB playerWorldAABB = targetPlayer->getCollider()->getWorldAABB();
    std::array<glm::vec3, 8> playerCorners = engine::Collider::getCornersFromAABB(playerWorldAABB);
    for (auto& corner : playerCorners) {
        corner = glm::vec3(invEnemyWorld * glm::vec4(corner, 1.0f));
    }
    engine::AABB playerLocalAABB = engine::Collider::aabbFromCorners(playerCorners);
    return engine::Collider::aabbIntersects(visionBox, playerLocalAABB);
}

void rind::Enemy::damage(float amount) {
    if (getHealth() <= 0.0f) return; // already dead, pending deletion
    setHealth(getHealth() - amount);
    if (getHealth() <= 0.0f) {
        float statusChance = (dist(rng) * 0.5f) + 0.5f; // 0 to 1
        if (statusChance <= 0.1f // 10% chance
         && !targetPlayer->statusEnabled()) { // status effect not already enabled
            float randomValue = (dist(rng) * 0.5f) + 0.5f; // 0 to 1
            targetPlayer->setStatusEffect(getRandomStatusEffect(randomValue));
        }
        targetPlayer->addScore(getScoreWorth());
        volumetricManager->createVolumetric(
            glm::scale(
                getWorldTransform(),
                glm::vec3(2.0f, 2.0f, 2.0f)
            ),
            glm::scale(
                getWorldTransform(),
                glm::vec3(10.0f, 10.0f, 10.0f)
            ),
            glm::vec4(glm::min(getTrailColor() + glm::vec3(0.2f), glm::vec3(1.0f)), 20.0f),
            0.5f
        );
        volumetricManager->createVolumetric(
            glm::scale(
                getWorldTransform(),
                glm::vec3(5.0f, 5.0f, 5.0f)
            ),
            glm::scale(
                getWorldTransform(),
                glm::vec3(20.0f, 20.0f, 20.0f)
            ),
            glm::vec4(glm::min(getTrailColor() + glm::vec3(0.2f), glm::vec3(1.0f)), 1.0f),
            2.0f
        );
        volumetricManager->createVolumetric(
            glm::scale(
                getWorldTransform(),
                glm::vec3(6.0f, 6.0f, 6.0f)
            ),
            glm::scale(
                getWorldTransform(),
                glm::vec3(25.0f, 25.0f, 25.0f)
            ),
            glm::vec4(0.1f, 0.1f, 0.1f, 0.4f),
            4.0f
        );
        particleManager->burstParticles(
            getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
            getTrailColor(),
            glm::vec3(0.0f, 1.0f, 0.0f) * 5.0f,
            250,
            5.0f,
            0.5f,
            0.5f
        );
        particleManager->burstParticles(
            getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
            getTrailColor(),
            glm::vec3(0.0f, 1.0f, 0.0f) * 10.0f,
            160,
            7.0f,
            1.0f,
            1.0f
        );
        particleManager->burstParticles(
            getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
            getTrailColor(),
            glm::vec3(0.0f, 1.0f, 0.0f) * 11.0f,
            50,
            5.0f,
            1.0f,
            1.5f
        );
        particleManager->burstParticles(
            getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
            getTrailColor(),
            glm::vec3(0.0f, 1.0f, 0.0f) * 12.0f,
            300,
            6.0f,
            2.0f,
            0.4f
        );
        rind::TempTrigger* triggerCollider = new rind::TempTrigger(
            getEntityManager(),
            "enemyExplosionTrigger" + getName(),
            getTrailColor(),
            glm::scale(glm::translate(getWorldTransform(), glm::vec3(0.0f, 0.5f, 0.0f)), glm::vec3(9.0f, 9.0f, 9.0f)),
            4.0f
        );
        audioManager->playSound3D("enemy_death", getWorldPosition(), 1.2f, 0.15f);
        getEntityManager()->markForDeletion(this);
    }
}
