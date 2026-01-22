#include <rind/Enemy.h>
#include <engine/ParticleManager.h>
#include <glm/gtc/quaternion.hpp>

#define PI 3.14159265358979323846f

rind::Enemy::Enemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures)
    : engine::CharacterEntity(entityManager, name, shader, transform, textures), targetPlayer(player) {
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
        enemyModel->setModel(entityManager->getRenderer()->getModelManager()->getModel("enemy"));
        face->setModel(entityManager->getRenderer()->getModelManager()->getModel("enemy-head"));
        enemyModel->addChild(face);
        addChild(enemyModel);
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
        audioManager = getEntityManager()->getRenderer()->getAudioManager();
        particleManager = getEntityManager()->getRenderer()->getParticleManager();
    }

void rind::Enemy::update(float deltaTime) {
    const glm::vec3& vel = getVelocity();
    float horizontalSpeed = glm::length(glm::vec3(vel.x, 0.0f, vel.z));
    float rotateSpeed = std::abs(getRotateVelocity().y);
    float speed = horizontalSpeed + std::abs(rotateSpeed);
    if (speed > 0.1f) {
        if (enemyModel->getAnimationState().currentAnimation != "Walk") {
            enemyModel->playAnimation("Walk", true, speed / 5.0f);
        } else {
            enemyModel->getAnimationState().playbackSpeed = speed / 5.0f;
        }
    } else {
        if (enemyModel->getAnimationState().currentAnimation != "Idle") {
            enemyModel->playAnimation("Idle", true, 1.0f);
        }
    }
    if (targetPlayer) {
        glm::vec3 toPlayer = targetPlayer->getWorldPosition() + glm::vec3(0.0f, 1.0f, 0.0f) - getWorldPosition();
        toPlayer.y = 0.0f;
        float distanceToPlayer = glm::length(toPlayer);
        switch (state) {
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
                forward.y = 0.0f;
                forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, -1.0f);
                glm::vec3 backward = -forward;
                const float maxBackupDist = 15.0f;
                float lo = 0.0f;
                float hi = maxBackupDist;
                float maxSafeBackup = 0.0f;
                const int binarySearchIterations = 8;
                for (int i = 0; i < binarySearchIterations; ++i) {
                    float mid = (lo + hi) * 0.5f;
                    glm::vec3 testPos = getWorldPosition() + backward * mid;
                    glm::vec3 rayOrigin = testPos + glm::vec3(0.0f, 2.0f, 0.0f);
                    size_t hits = engine::Collider::raycast(getEntityManager(), rayOrigin, glm::vec3(0.0f, -1.0f, 0.0f), 5.0f).size();
                    if (hits > 0 && hits <= 2) {
                        maxSafeBackup = mid;
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                const float desiredDistance = 12.0f;
                float safeDistance = glm::min(desiredDistance, distanceToPlayer + maxSafeBackup);
                glm::vec3 targetDir = glm::normalize(glm::vec3(toPlayer.x, 0.0f, toPlayer.z));
                float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
                float angle = acos(dot);
                glm::vec3 cross = glm::cross(forward, targetDir);
                float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
                float maxRotation = deltaTime * PI;
                float rotationAmount = glm::min(angle, maxRotation) * rotationDir;
                rotate(glm::vec3(0.0f, rotationAmount, 0.0f));
                bool facingPlayer = (angle < PI / 4.0f);
                float distanceError = distanceToPlayer - safeDistance;
                if (std::abs(distanceError) < 0.5f) {
                    stopMove(getPressed(), false);
                    state = EnemyState::Attacking;
                } else if (distanceError > 0.0f && facingPlayer) {
                    if (getPressed() != glm::vec3(0.0f, 0.0f, 1.0f)) {
                        stopMove(getPressed(), false);
                        move(glm::vec3(0.0f, 0.0f, 1.0f), false);
                    }
                } else if (distanceError < 0.0f && maxSafeBackup > 0.5f) {
                    if (getPressed() != glm::vec3(0.0f, 0.0f, -1.0f)) {
                        stopMove(getPressed(), false);
                        move(glm::vec3(0.0f, 0.0f, -1.0f), false);
                    }
                } else if (!facingPlayer) {
                    stopMove(getPressed(), false);
                } else {
                    stopMove(getPressed(), false);
                    state = EnemyState::Attacking;
                }
                break;
            }
            case EnemyState::Attacking: {
                float backToChaseDistance = 20.0f;
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
                headEuler.z = glm::clamp(headEuler.z + headPitchAmount, -glm::half_pi<float>() + 0.01f, glm::half_pi<float>() - 0.01f);
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
                glm::vec3 strafeDir = glm::normalize(glm::vec3(randX, 0.0f, 0.0f));
                glm::vec3 right = glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f));
                glm::vec3 testPos = getWorldPosition() + right * strafeDir.x * 2.0f;
                glm::vec3 rayOrigin = testPos + glm::vec3(0.0f, 2.0f, 0.0f);
                size_t hits = engine::Collider::raycast(getEntityManager(), rayOrigin, glm::vec3(0.0f, -1.0f, 0.0f), 5.0f).size();
                if (hits > 0 && hits <= 2 ) {
                    if (getPressed() != strafeDir) {
                        stopMove(getPressed(), false);
                        move(strafeDir, false);
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
            character->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, true);
        } else if (rind::Enemy* character = dynamic_cast<rind::Enemy*>(collision.other->getParent())) {
            character->damage(10.0f);
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, true);
        } else {
            audioManager->playSound3D("laser_ground_impact", collision.worldHitPoint, 0.5f, true);
        }
    }
    audioManager->playSound3D("laser_shot", gunPos, 0.5f, true);
    trailFramesRemaining = maxTrailFrames;
    trailEndPos = endPos;
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

void rind::Enemy::wander() {
    float direction = 0.0f;
    float amount = 0.0f;
    uint32_t tries = 0;
    while (tries < 20) {
        direction = (dist(rng) + 1.0f) * PI;
        amount = (dist(rng) + 1.0f) * 10.0f;
        glm::vec3 goal = glm::vec3(cos(direction), 0.0f, sin(direction)) * amount;
        glm::vec3 worldGoal = getWorldPosition() + goal;
        glm::vec3 rayOrigin = worldGoal + glm::vec3(0.0f, 2.0f, 0.0f);
        size_t rayHits = engine::Collider::raycast(getEntityManager(), rayOrigin, glm::vec3(0.0f, -1.0f, 0.0f), 5.0f).size();
        if (rayHits > 0 && rayHits <= 2 ) {
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

void rind::Enemy::wanderTo(float deltaTime) {
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
    if (glm::length(glm::vec3(toTarget.x, 0.0f, toTarget.z)) <= 2.0f) {
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
    if (getPressed() != glm::vec3(0.0f, 0.0f, -1.0f)) {
        stopMove(getPressed(), false);
        move(glm::vec3(1.0f, 0.0f, 0.0f));
    }
}

bool rind::Enemy::checkVisibilityOfPlayer() {
    if (!targetPlayer) {
        return false;
    }
    engine::AABB playerAABB = targetPlayer->getCollider()->getWorldAABB();
    std::array<glm::vec3, 8> corners = engine::Collider::getCornersFromAABB(visionBox);
    glm::mat4 worldTransform = getWorldTransform();
    for (auto& corner : corners) {
        corner = glm::vec3(worldTransform * glm::vec4(corner, 1.0f));
    }
    engine::AABB enemyVisionBox = engine::Collider::aabbFromCorners(corners);
    return engine::Collider::aabbIntersects(enemyVisionBox, playerAABB);
}

void rind::Enemy::damage(float amount) {
    setHealth(getHealth() - amount);
    if (getHealth() <= 0.0f) {
        getEntityManager()->markForDeletion(this);
    }
}