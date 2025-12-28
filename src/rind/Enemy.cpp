#include <rind/Enemy.h>

#define PI 3.14159265358979323846f

rind::Enemy::Enemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures)
    : engine::CharacterEntity(entityManager, name, shader, transform, textures), targetPlayer(player) {
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 0.0f)),
            name,
            glm::vec3(0.5f, 1.8f, 0.5f)
        );
        addChild(box);
        setCollider(box);
    }

void rind::Enemy::update(float deltaTime) {
    if (targetPlayer) {
        glm::vec3 toPlayer = targetPlayer->getWorldPosition() - getWorldPosition();
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
                }
                break;
            }
            case EnemyState::Chasing: {
                if (!checkVisibilityOfPlayer()) {
                    state = EnemyState::Idle;
                    stopMove(getPressed(), false);
                    break;
                }
                glm::mat4 t = getTransform();
                glm::vec3 forward = glm::vec3(t[2]);
                forward.y = 0.0f;
                forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);
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
                const float desiredDistance = 15.0f;
                float safeDistance = glm::min(desiredDistance, distanceToPlayer + maxSafeBackup);
                glm::vec3 targetDir = -toPlayer / distanceToPlayer;
                float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
                float angle = acos(dot);
                glm::vec3 cross = glm::cross(forward, targetDir);
                float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
                float maxRotation = deltaTime * 2 * PI;
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
                glm::vec3 forward = glm::vec3(t[2]);
                forward.y = 0.0f;
                forward = glm::length(forward) > 1e-6f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);
                glm::vec3 targetDir = -toPlayer / distanceToPlayer;
                float dot = glm::clamp(glm::dot(forward, targetDir), -1.0f, 1.0f);
                float angle = acos(dot);
                glm::vec3 cross = glm::cross(forward, targetDir);
                float rotationDir = cross.y > 0.0f ? 1.0f : -1.0f;
                float maxRotation = deltaTime * 2 * PI;
                float rotationAmount = glm::min(angle, maxRotation) * rotationDir;
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
}

void rind::Enemy::shoot() {
    std::cout << "Shooting weapon!" << std::endl;
    std::vector<engine::Collider::Collision> hits = engine::Collider::raycast(
        getEntityManager(),
        getWorldPosition(),
        -glm::normalize(glm::vec3(getWorldTransform()[2])),
        1000.0f
    );
    for (engine::Collider::Collision& collision : hits) {
        rind::Player* character = dynamic_cast<rind::Player*>(collision.other->getParent());
        if (character) {
            character->damage(25.0f);
            std::cout << "Hit " << character->getName() << " for 25 damage!" << std::endl;
        }
    }
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
        std::cout<< "Enemy " << getName() << " has died." << std::endl;
        getEntityManager()->markForDeletion(this);
    }
}