#include <rind/BashingEnemy.h>

#include <engine/ParticleManager.h>
#include <glm/gtc/quaternion.hpp>

#define PI 3.14159265358979323846f

rind::BashingEnemy::BashingEnemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, glm::mat4 transform, uint32_t& enemyCount)
    : rind::Enemy(entityManager, player, name, transform, enemyCount) {
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.2f, 0.25f)),
            name,
            glm::vec3(0.6f, 0.8f, 2.0f)
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
            gunMaterial
        );
        addChild(enemyModel);
        engine::ModelManager* modelManager = entityManager->getRenderer()->getModelManager();
        enemyModel->setModel(modelManager->getModel("bashingenemy"));
    }


void rind::BashingEnemy::update(float deltaTime) {
    if (targetPlayer) {
        glm::vec3 toPlayer = targetPlayer->getWorldPosition() + glm::vec3(0.0f, 1.0f, 0.0f) - getWorldPosition();
        toPlayer.y = 0.0f;
        float distanceToPlayer = glm::length(toPlayer);
        if (distanceToPlayer < 4.0f) {
            hit();
        }
        switch (state) {
            case EnemyState::Spawning: {
                size_t hits = engine::Collider::raycast(getEntityManager(), getWorldPosition(), glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, this->getCollider()).size();
                if (hits > 0 && hits <= 2) {
                    state = EnemyState::Idle;
                } else if (firstFrame) {
                    rotateToPlayer();
                    dash(glm::vec3(0.0f, 1.0f, 0.25f), 500.0f);
                    move(glm::vec3(0.0f, 0.0f, 1.0f), false);
                    firstFrame = false;
                }
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
                    size_t hits = engine::Collider::raycast(getEntityManager(), rayOrigin, glm::vec3(0.0f, -1.0f, 0.0f), 5.0f, this->getCollider()).size();
                    if (hits > 0 && hits <= 2) {
                        maxSafeBackup = mid;
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                const float desiredDistance = 10.0f;
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
                float backToChaseDistance = 12.0f;
                float tooClose = 3.0f;
                if (!checkVisibilityOfPlayer() 
                || distanceToPlayer > backToChaseDistance
                || distanceToPlayer < tooClose) {
                    state = EnemyState::Chasing;
                    break;
                }
                move(glm::vec3(0.0f, 0.0f, 1.0f), false);
                dash(glm::vec3(0.0f, 0.0f, 1.0f), 50.0f);
                stopMove(getPressed(), false);
                break;
            }
        }
    } else {
        wanderTo(deltaTime);
    }
    rind::Enemy::update(deltaTime);
}

void rind::BashingEnemy::wander() {
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

void rind::BashingEnemy::wanderTo(float deltaTime) {
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

void rind::BashingEnemy::hit() {
    if (lastShotTime + std::chrono::duration<float>(shootingCooldown) > std::chrono::steady_clock::now()) {
        return;
    }
    glm::vec3 forward = -glm::normalize(glm::vec3(getTransform()[2]));
    glm::vec3 rayOrigin = getWorldPosition() + glm::vec3(0.0f, 1.0f, 0.0f);
    std::vector<engine::Collider::Collision> hits = engine::Collider::raycast(
        getEntityManager(),
        rayOrigin,
        forward,
        4.0f,
        getCollider(),
        true
    );
    if (!hits.empty()) {
        engine::Collider::Collision collision = hits[0];
        if (rind::Player* character = dynamic_cast<rind::Player*>(hits[0].other->getParent())) {
            audioManager->playSound3D("laser_enemy_impact", getWorldPosition(), 0.5f, true);
            character->damage(20.0f);
            lastShotTime = std::chrono::steady_clock::now();
        }
    }
}
