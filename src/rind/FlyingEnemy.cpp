#include <rind/FlyingEnemy.h>
#include <engine/ParticleManager.h>
#include <glm/gtc/quaternion.hpp>

#define PI 3.14159265358979323846f

rind::FlyingEnemy::FlyingEnemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, glm::mat4 transform, uint32_t& enemyCount)
    : rind::Enemy(entityManager, player, name, transform, enemyCount) {
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.3f, 0.0f)),
            name,
            glm::vec3(0.9f, 0.7f, 0.9f)
        );
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
            "enemy1_model",
            "gbuffer",
            glm::rotate(
                glm::mat4(1.0f),
                glm::radians(90.0f),
                glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            gunMaterial
        );
        addChild(enemyModel);
        engine::Entity* face = new engine::Entity(
            entityManager,
            "enemy1_face",
            "gbuffer",
            glm::translate(
                glm::mat4(1.0f),
                glm::vec3(0.9f, 2.22f, 0.0f)
            ),
            gunMaterial
        );
        enemyModel->addChild(face);
        enemyModel->setModel(entityManager->getRenderer()->getModelManager()->getModel("enemy"));
        face->setModel(entityManager->getRenderer()->getModelManager()->getModel("enemy-head"));
        setHead(face);
        gunEndPosition = new engine::Entity(
            entityManager,
            "playerGunEndPosition",
            "",
            glm::translate(glm::mat4(1.0f), glm::vec3(0.5f, 0.0f, 0.0f)),
            {},
            false
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
                    audioManager->playSound3D("enemy_see", getWorldPosition(), 0.5f, true);
                }
                break;
            }
            case EnemyState::Chasing: {
                if (!checkVisibilityOfPlayer()) {
                    state = EnemyState::Idle;
                    stopMove(getPressed(), false);
                    audioManager->playSound3D("enemy_lose", getWorldPosition(), 0.5f, true);
                    break;
                }
                glm::mat4 t = getTransform();
                glm::vec3 forward = -glm::vec3(t[2]);
                forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 targetDir = glm::normalize(toPlayer);
                float dot = glm::clamp(glm::dot(glm::vec3(forward.x, 0.0f, forward.z), glm::vec3(targetDir.x, 0.0f, targetDir.z)), -1.0f, 1.0f);
                float angle = acos(dot);
                glm::vec3 cross = glm::cross(forward, targetDir);
                float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
                float maxRotation = deltaTime * PI;
                float rotationAmount = glm::min(angle, maxRotation) * rotationDir;
                rotate(glm::vec3(0.0f, rotationAmount, 0.0f));
                float desiredHeight = targetPlayer->getWorldPosition().y + 3.0f;
                float currentHeight = getWorldPosition().y;
                float yDiff = desiredHeight - currentHeight;
                float targetYVel = yDiff * 10.0f;
                float currentYVel = getVelocity().y;
                float yVel = currentYVel + (targetYVel - currentYVel) * deltaTime * 5.0f;
                yVel = std::clamp(yVel, -10.0f, 10.0f);
                setVelocity(glm::vec3(getVelocity().x, yVel, getVelocity().z));
                bool facingPlayer = (angle < PI / 4.0f);
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
                float switchToChaseChance = dist(rng) + 1.0f;
                if (!checkVisibilityOfPlayer() 
                || distanceToPlayer > backToChaseDistance
                || switchToChaseChance > 1.9f) {
                    state = EnemyState::Chasing;
                    break;
                }
                glm::mat4 t = getTransform();
                glm::vec3 forward = -glm::vec3(t[2]);
                forward.y = 0.0f;
                forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 targetDir = glm::normalize(glm::vec3(toPlayer.x, 0.0f, toPlayer.z));
                float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
                float angle = acos(dot);
                glm::vec3 cross = glm::cross(forward, targetDir);
                float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
                float maxRotation = deltaTime * PI;
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
                if (std::abs(randX) < 0.95f && getPressed() != glm::vec3(0.0f)) {
                    break;
                }
                float strafeDir = randX > 0.0f ? 1.0f : -1.0f;
                glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
                size_t hits = engine::Collider::raycast(getEntityManager(), getWorldPosition(), right * strafeDir, 2.0f, this->getCollider()).size();
                if (hits > 0) {
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
        direction = (dist(rng) + 1.0f) * PI;
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
        size_t rayHits = engine::Collider::raycast(getEntityManager(), getWorldPosition(), goal, amount).size();
        if (rayHits == 0) {
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
        float escapeWait = dist(rng) + 1.0f;
        if (escapeWait > 1.95f) {
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
    glm::mat4 t = getTransform();
    glm::vec3 forward = -glm::vec3(t[2]);
    forward.y = 0.0f;
    forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 targetDir = glm::normalize(glm::vec3(toTarget.x, 0.0f, toTarget.z));
    float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
    float angle = acos(dot);
    glm::vec3 cross = glm::cross(forward, targetDir);
    float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
    float maxRotation = deltaTime * 2 * PI;
    float rotationAmount = glm::min(angle, maxRotation) * rotationDir;
    rotate(glm::vec3(0.0f, rotationAmount, 0.0f));
    float yDiff = wanderTarget.y - getWorldPosition().y;
    float targetYVel = yDiff * 10.0f;
    float currentYVel = getVelocity().y;
    float yVel = currentYVel + (targetYVel - currentYVel) * deltaTime * 5.0f;
    yVel = std::clamp(yVel, -10.0f, 10.0f);
    setVelocity(glm::vec3(getVelocity().x, yVel, getVelocity().z));
    stopMove(getPressed(), false);
    move(glm::vec3(1.0f, 0.0f, 0.0f));
}
