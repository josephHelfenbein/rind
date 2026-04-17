#pragma once

#include <engine/EntityManager.h>
#include <rind/Enemy.h>
#include <glm/gtc/matrix_transform.hpp>
#include <rind/GameInstance.h>
#include <random>
#include <numbers>

namespace rind {
    template<typename EnemyType>
    class EnemySpawner : public engine::Entity {
    public:
        EnemySpawner(
            engine::EntityManager* entityManager,
            rind::GameInstance* gameInstance,
            rind::Player* player,
            const std::string& name,
            const glm::mat4& transform,
            uint32_t maxEnemyMultiplier,
            uint32_t baseMaxEnemies,
            float baseSpawnRate,
            float spawnChance = 0.0f
        ) : engine::Entity(entityManager, name, "", transform, {}, false), gameInstance(gameInstance), targetPlayer(player), baseSpawnRate(baseSpawnRate), baseMaxEnemies(baseMaxEnemies), maxEnemyMultiplier(maxEnemyMultiplier), spawnChance(spawnChance) {}

        void update(float deltaTime) override {
            countTimer += deltaTime;
            if (countTimer >= 20.0f) {
                countTimer = 0.0f;
            }
            float waveProgress = sinf(std::numbers::pi_v<float> * (countTimer / 5.0f)) + 1.0f;
            uint32_t maxEnemies = waveProgress * (maxEnemyMultiplier * gameInstance->getDifficultyLevel() + baseMaxEnemies);
            if (enemyCount >= maxEnemies) {
                readyToSpawn = false;
                return;
            }
            if (!readyToSpawn) {
                readyToSpawn = true;
                spawnTimer = 0.0f;
            }

            spawnTimer += deltaTime;
            float timeRandomness = dist(rng) * baseSpawnRate * 0.25f; // +-25% of base spawn rate
            float adjustedSpawnInterval = (baseSpawnRate + timeRandomness) * ((5.0f - gameInstance->getDifficultyLevel()) / 5.0f);
            if (spawnTimer >= adjustedSpawnInterval) {
                if (spawnChance > 1e-9f) {
                    float spawnRoll = (dist(rng) + 1.0f) * 0.5f; // 0 to 1
                    if (spawnRoll > spawnChance) {
                        return;
                    }
                }
                spawnTimer = 0.0f;
                spawnEnemy();
            }
        }
    private:
        void spawnEnemy() {
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
                gameInstance,
                enemyName,
                glm::translate(glm::mat4(1.0f), getWorldPosition()),
                enemyCount
            );
        }

        rind::Player* targetPlayer = nullptr;
        float baseSpawnRate;
        float spawnChance = 0.0f;
        float spawnTimer = 0.0f;
        float countTimer = 0.0f;
        bool readyToSpawn = false;
        uint32_t enemyCount = 0u;
        uint32_t spawnedEnemies = 0u;
        uint32_t maxEnemyMultiplier;
        uint32_t baseMaxEnemies;
        uint32_t assumedDifficulty = 0u;
        rind::GameInstance* gameInstance = nullptr;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    };
};
