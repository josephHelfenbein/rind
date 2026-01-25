#pragma once

#include <engine/EntityManager.h>
#include <rind/Enemy.h>
#include <glm/gtc/matrix_transform.hpp>

namespace rind {
    template<typename EnemyType>
    class EnemySpawner : public engine::Entity {
    public:
        EnemySpawner(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, glm::mat4 transform)
            : engine::Entity(entityManager, name, "", transform, {}, false), targetPlayer(player) {}

        void update(float deltaTime) override {
            spawnTimer += deltaTime;
            if (spawnTimer >= spawnInterval) {
                spawnTimer = 0.0f;
                spawnEnemy();
            }
        }
    private:
        void spawnEnemy() {
            if (enemyCount >= maxEnemies) {
                return;
            }
            std::string enemyName = "enemy" + std::to_string(enemyCount++);
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
                getWorldTransform(),
                enemyCount
            );
        }

        rind::Player* targetPlayer = nullptr;
        float spawnInterval = 5.0f;
        float spawnTimer = 0.0f;
        uint32_t enemyCount = 0u;
        uint32_t maxEnemies = 15u;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    };
};
