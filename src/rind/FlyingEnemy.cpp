#include <rind/FlyingEnemy.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <glm/gtc/quaternion.hpp>
#include <rind/SlowBullet.h>
#include <cmath>
#include <numbers>

rind::FlyingEnemy::FlyingEnemy(
    engine::EntityManager* entityManager,
    rind::Player* player,
    const std::string& name,
    const glm::mat4& transform,
    uint32_t& enemyCount
) : rind::Enemy(entityManager, player, name, transform, enemyCount) {
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.4f, 0.0f)),
            name,
            glm::vec3(0.9f, 0.7f, 0.9f)
        );
        box->setIsDynamic(true);
        addChild(box);
        setCollider(box);
        std::vector<std::string> gunMaterial = {
            "materials_enemy_albedo",
            "materials_enemy_metallic",
            "materials_enemy_roughness",
            "materials_enemy_normal"
        };
        enemyModel = new engine::Entity(
            entityManager,
            name + "Model",
            "gbuffer",
            glm::rotate(
                glm::mat4(1.0f),
                glm::radians(90.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            gunMaterial,
            false,
            EntityType::Model
        );
        addChild(enemyModel);
        engine::Entity* face = new engine::Entity(
            entityManager,
            name + "Face",
            "gbuffer",
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(1.0f, 0.2f, 0.0f)
            ),
            gunMaterial,
            false,
            EntityType::Generic
        );
        enemyModel->addChild(face);
        enemyModel->setModel(entityManager->getRenderer()->getModelManager()->getModel("flyingenemy"));
        enemyModel->playAnimation("Flying", true);
        face->setModel(entityManager->getRenderer()->getModelManager()->getModel("enemy-head"));
        setHead(face);
        gunEndPosition = new engine::Entity(
            entityManager,
            name + "GunEndPosition",
            "",
            glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f)),
            {},
            false,
            EntityType::Empty
        );
        face->addChild(gunEndPosition);
        setGravityEnabled(false);
        visionBox = {
            .min = glm::vec3(-8.0f, -15.0f, -50.0f),
            .max = glm::vec3(8.0f, 6.0f, 0.0f)
        };
    }

void rind::FlyingEnemy::update(float deltaTime) {
    if (targetPlayer) {
        glm::vec3 toPlayer = targetPlayer->getWorldPosition() + glm::vec3(0.0f, 1.0f, 0.0f) - getHead()->getWorldPosition();
        float distanceToPlayer = glm::length(toPlayer);
        switch (state) {
            case EnemyState::Spawning: {
                state = EnemyState::Idle;
                break;
            }
            case EnemyState::Idle: {
                wanderTo(deltaTime);
                if (checkVisibilityOfPlayer()) {
                    state = EnemyState::Chasing;
                    wandering = false;
                    waiting = false;
                    stopMove(getPressed(), false);
                    audioManager->playSound3D("enemy_see", getWorldPosition(), 0.5f, 0.2F);
                }
                break;
            }
            case EnemyState::Chasing: {
                if (!checkVisibilityOfPlayer()) {
                    state = EnemyState::Idle;
                    stopMove(getPressed(), false);
                    audioManager->playSound3D("enemy_lose", getWorldPosition(), 0.5f, 0.2F);
                    break;
                }
                const glm::mat4& t = getTransform();
                glm::vec3 forward = -glm::vec3(t[2]);
                forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 targetDir = glm::normalize(toPlayer);
                float dot = glm::clamp(glm::dot(glm::vec3(forward.x, 0.0f, forward.z), glm::vec3(targetDir.x, 0.0f, targetDir.z)), -1.0f, 1.0f);
                float angle = acos(dot);
                glm::vec3 cross = glm::cross(forward, targetDir);
                float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
                float maxRotation = deltaTime * std::numbers::pi_v<float>;
                float rotationAmount = glm::min(angle, maxRotation) * rotationDir;
                rotate(glm::vec3(0.0f, rotationAmount, 0.0f));
                float desiredHeight = targetPlayer->getWorldPosition().y + 3.0f;
                float currentHeight = getWorldPosition().y;
                float yDiff = desiredHeight - currentHeight;
                float targetYVel = yDiff * 10.0f;
                float currentYVel = getVelocity().y;
                float smoothFactor = 1.0f - std::exp(-5.0f * deltaTime);
                float yVel = currentYVel + (targetYVel - currentYVel) * smoothFactor;
                yVel = std::clamp(yVel, -10.0f, 10.0f);
                if (yVel < 0.0f) {
                    float groundCheckDist = std::abs(yVel * deltaTime) + 1.5f;
                    if (engine::Collider::raycastAny(
                        getEntityManager(),
                        getWorldPosition(),
                        glm::vec3(0.0f, -1.0f, 0.0f),
                        groundCheckDist,
                        this->getCollider(),
                        0.1f
                    )) {
                        yVel = 0.0f;
                    }
                }
                setVelocity(glm::vec3(getVelocity().x, yVel, getVelocity().z));
                bool facingPlayer = (angle < std::numbers::pi_v<float> / 4.0f);
                float desiredDistance = 12.0f;
                float distanceError = distanceToPlayer - desiredDistance;
                if (distanceError < 0.5f && distanceError > -0.5f) {
                    stopMove(getPressed(), false);
                    state = EnemyState::Attacking;
                } else if (distanceError > 0.5f && facingPlayer) {
                    glm::vec3 desiredMove = glm::vec3(0.0f, 0.0f, 1.0f);
                    if (getPressed() != desiredMove) {
                        stopMove(getPressed(), false);
                        move(desiredMove, false);
                    }
                } else if (distanceError < -0.5f) {
                    glm::vec3 desiredMove = glm::vec3(0.0f, 0.0f, -1.0f);
                    if (getPressed() != desiredMove) {
                        stopMove(getPressed(), false);
                        move(desiredMove, false);
                    }
                } else if (!facingPlayer) {
                    stopMove(getPressed(), false);
                    state = EnemyState::Idle;
                } else {
                    stopMove(getPressed(), false);
                    state = EnemyState::Attacking;
                }
                break;
            }
            case EnemyState::Attacking: {
                float backToChaseDistance = 16.0f;
                float switchToChaseProb = deltaTime * 3.0f;
                float switchRoll = (dist(rng) + 1.0f) / 2.0f;
                if (!checkVisibilityOfPlayer() 
                || distanceToPlayer > backToChaseDistance
                || switchRoll < switchToChaseProb) {
                    state = EnemyState::Chasing;
                    break;
                }
                const glm::mat4& t = getTransform();
                glm::vec3 forward = -glm::vec3(t[2]);
                forward.y = 0.0f;
                forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 targetDir = glm::normalize(glm::vec3(toPlayer.x, 0.0f, toPlayer.z));
                float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
                float angle = acos(dot);
                glm::vec3 cross = glm::cross(forward, targetDir);
                float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
                float maxRotation = deltaTime * std::numbers::pi_v<float>;
                float rotationAmount = glm::min(angle, maxRotation) * rotationDir;
                glm::vec3 headWorldPos = glm::vec3(getHead()->getWorldTransform()[3]);
                glm::vec3 toPlayerFromHead = targetPlayer->getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f) - headWorldPos;
                glm::mat4 headParentWorldTransform = getHead()->getParent()->getWorldTransform();
                glm::mat3 headParentRotation = glm::mat3(headParentWorldTransform);
                glm::vec3 toPlayerLocal = glm::transpose(headParentRotation) * toPlayerFromHead;
                float horizontalDist = glm::length(glm::vec2(toPlayerLocal.x, toPlayerLocal.z));
                float targetPitch = atan2(toPlayerLocal.y, horizontalDist);
                glm::mat4 headTransform = getHead()->getTransform();
                glm::quat headRotation = glm::quat_cast(headTransform);
                glm::vec3 headEuler = glm::eulerAngles(headRotation);
                float currentPitch = headEuler.z;
                float pitchDiff = targetPitch - currentPitch;
                float headPitchAmount = glm::clamp(pitchDiff, -maxRotation, maxRotation);
                headEuler.z = glm::clamp(currentPitch + headPitchAmount, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
                glm::mat4 newHeadTransform = glm::mat4_cast(glm::quat(headEuler));
                newHeadTransform[3] = headTransform[3];
                getHead()->setTransform(newHeadTransform);
                rotate(glm::vec3(0.0f, rotationAmount, 0.0f));
                if (lastShotTime + std::chrono::duration<float>(shootingCooldown) < std::chrono::steady_clock::now()) {
                    lastShotTime = std::chrono::steady_clock::now();
                    shoot();
                }
                float randX = dist(rng);
                float strafeChangeProb = deltaTime * 3.0f;
                float strafeRoll = (dist(rng) + 1.0f) / 2.0f;
                if (strafeRoll > strafeChangeProb && getPressed() != glm::vec3(0.0f)) {
                    break;
                }
                float strafeDir = randX > 0.0f ? 1.0f : -1.0f;
                glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
                if (engine::Collider::raycastAny(
                    getEntityManager(),
                    getWorldPosition(),
                    right * strafeDir, 2.0f,
                    this->getCollider(),
                    0.1f
                )) {
                    if (getPressed().x != strafeDir) {
                        stopMove(getPressed(), false);
                        move(glm::vec3(strafeDir, 0.0f, 0.0f), false);
                    }
                } else {
                    stopMove(getPressed(), false);
                }
                break;
            }
        }
    } else {
        wanderTo(deltaTime);
    }
    rind::Enemy::update(deltaTime);
}

void rind::FlyingEnemy::wander() {
    float direction = 0.0f;
    float yOffset = 0.0f;
    float amount = 0.0f;
    uint32_t tries = 0;
    glm::vec3 playerPos = targetPlayer->getWorldPosition();
    while (tries < 20) {
        direction = (dist(rng) + 1.0f) * std::numbers::pi_v<float>;
        yOffset = dist(rng) * 0.5f;
        amount = (dist(rng) + 1.0f) * 10.0f;
        if ((getWorldPosition().y + yOffset * amount <= -5.0f && yOffset < 0.0f)
         || (getWorldPosition().y + yOffset * amount >= 10.0f && yOffset > 0.0f)) {
            yOffset = -yOffset;
        }
        glm::vec3 goal = glm::normalize(glm::vec3(cos(direction), yOffset, sin(direction)));
        glm::vec3 worldGoal = getWorldPosition() + goal * amount;
        float originalDistanceToPlayer = glm::length(getWorldPosition() - playerPos);
        float distanceToPlayer = glm::length(worldGoal - playerPos);
        if (distanceToPlayer < 2.0f || (distanceToPlayer > 50.0f && distanceToPlayer > originalDistanceToPlayer)) {
            tries++;
            continue;
        }
        if (!engine::Collider::raycastAny(
            getEntityManager(),
            getWorldPosition(),
            goal,
            amount,
            getCollider()
        )) {
            wanderTarget = worldGoal;
            wandering = true;
            break;
        }
        tries++;
    }
    if (tries == 20) {
        waiting = true;
    }
}

void rind::FlyingEnemy::wanderTo(float deltaTime) {
    if (waiting) {
        float escapeProb = deltaTime * 1.5f;
        float roll = (dist(rng) + 1.0f) / 2.0f;
        if (roll < escapeProb) {
            waiting = false;
        } else {
            return;
        }
    }
    if (!wandering) {
        wander();
        return;
    }
    glm::vec3 toTarget = wanderTarget - getWorldPosition();
    if (glm::length(toTarget) <= 2.0f) {
        stopMove(getPressed(), false);
        wandering = false;
        waiting = true;
        return;
    }
    const glm::mat4& t = getTransform();
    glm::vec3 forward = -glm::vec3(t[2]);
    forward.y = 0.0f;
    forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 targetDir = glm::normalize(glm::vec3(toTarget.x, 0.0f, toTarget.z));
    float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
    float angle = acos(dot);
    glm::vec3 cross = glm::cross(forward, targetDir);
    float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
    float maxRotation = deltaTime * 2.0f * std::numbers::pi_v<float>;
    float rotationAmount = glm::min(angle, maxRotation) * rotationDir;
    rotate(glm::vec3(0.0f, rotationAmount, 0.0f));
    float yDiff = wanderTarget.y - getWorldPosition().y;
    float targetYVel = yDiff * 10.0f;
    float currentYVel = getVelocity().y;
    float smoothFactor = 1.0f - std::exp(-5.0f * deltaTime);
    float yVel = currentYVel + (targetYVel - currentYVel) * smoothFactor;
    yVel = std::clamp(yVel, -10.0f, 10.0f);
    if (yVel < 0.0f) {
        float groundCheckDist = std::abs(yVel * deltaTime) + 1.5f;
        if (engine::Collider::raycastAny(
            getEntityManager(),
            getWorldPosition(),
            glm::vec3(0.0f, -1.0f, 0.0f),
            groundCheckDist,
            this->getCollider()
        )) {
            yVel = 0.0f;
        }
    }
    setVelocity(glm::vec3(getVelocity().x, yVel, getVelocity().z));
    stopMove(getPressed(), false);
    move(glm::vec3(1.0f, 0.0f, 0.0f));
}

void rind::FlyingEnemy::shoot() {
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
    volumetricManager->createVolumetric(
        glm::scale(
            glm::translate(
                glm::mat4(1.0f), gunPos + rayDir * 0.1f
            ),
            glm::vec3(0.2f, 0.2f, 0.2f)
        ),
        glm::scale(
            glm::translate(
                glm::mat4(1.0f), gunPos + rayDir * 1.5f
            ),
            glm::vec3(2.5f, 2.5f, 2.5f)
        ),
        glm::vec4(glm::min(getTrailColor() + glm::vec3(0.1f), glm::vec3(1.0f)), 12.0f),
        0.1f
    );
    volumetricManager->createVolumetric(
        glm::scale(
            glm::translate(
                glm::mat4(1.0f), gunPos + rayDir * 0.25f
            ),
            glm::vec3(0.5f, 0.5f, 0.5f)
        ),
        glm::scale(
            glm::translate(
                glm::mat4(1.0f), gunPos + rayDir * 2.5f
            ),
            glm::vec3(8.0f, 8.0f, 8.0f)
        ),
        glm::vec4(0.1f, 0.1f, 0.1f, 0.5f),
        5.0f
    );
    audioManager->playSound3D("slowbullet_shot", gunPos, 0.5f, 0.15F);
    rind::SlowBullet* slowBullet = new rind::SlowBullet(
        getEntityManager(),
        "slowBullet" + getName() + std::to_string(spawnedBullets++),
        glm::translate(glm::mat4(1.0f), gunPos + rayDir * 0.5f),
        rayDir * 10.0f,
        getTrailColor()
    );
}