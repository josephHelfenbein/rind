#pragma once

#include <engine/EntityManager.h>
#include <rind/Enemy.h>
#include <glm/gtc/matrix_transform.hpp>
#include <rind/GameInstance.h>

namespace rind {
    template<typename EnemyType>
    class EnemySpawner : public engine::Entity {
    public:
        EnemySpawner(engine::EntityManager* entityManager, rind::GameInstance* gameInstance, rind::Player* player, const std::string& name, glm::mat4 transform)
            : engine::Entity(entityManager, name, "", transform, {}, false), gameInstance(gameInstance), targetPlayer(player) {}

        void update(float deltaTime) override {
            spawnTimer += deltaTime;
            float timeRandomness = (dist(rng) - 0.5f) * 2.0f;
            float adjustedSpawnInterval = spawnInterval + timeRandomness;
            if (spawnTimer >= adjustedSpawnInterval) {
                spawnTimer = 0.0f;
                spawnEnemy();
            }
        }
    private:
        void spawnEnemy() {
            maxEnemies = 2 + gameInstance->getDifficultyLevel() * 2;
            if (enemyCount >= maxEnemies) {
                return;
            }
            std::string enemyName = "enemy" + getName() + std::to_string(spawnedEnemies++);
            enemyCount++;
            setTransform(
                glm::translate(
                    glm::rotate(
                        glm::mat4(1.0f),
                        glm::radians(180.0f * dist(rng)),
                        glm::vec3(0.0f, 1.0f, 0.0f)
                    ),
                    getWorldPosition()
                )
            );
            EnemyType* enemy = new EnemyType(
                getEntityManager(),
                targetPlayer,
                enemyName,
                glm::translate(glm::mat4(1.0f), getWorldPosition()),
                enemyCount
            );
        }

        rind::Player* targetPlayer = nullptr;
        float spawnInterval = 8.0f;
        float spawnTimer = 5.0f;
        uint32_t enemyCount = 0u;
        uint32_t spawnedEnemies = 0u;
        uint32_t maxEnemies = 5u;
        rind::GameInstance* gameInstance = nullptr;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    };
};
