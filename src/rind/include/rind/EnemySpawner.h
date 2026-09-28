#pragma once

#include <engine/EntityManager.h>
#include <engine/Random.h>
#include <rind/GameInstance.h>
#include <glm/gtc/matrix_transform.hpp>
#include <numbers>
#include <cmath>

namespace rind {
    class Player;

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
        ) : engine::Entity(entityManager, name, "", transform, {}, false), targetPlayer(player), baseSpawnRate(baseSpawnRate), spawnChance(spawnChance), maxEnemyMultiplier(maxEnemyMultiplier), baseMaxEnemies(baseMaxEnemies), gameInstance(gameInstance) {}

        void update(float deltaTime) override {
            countTimer += deltaTime;
            if (countTimer >= 20.0f) {
                countTimer = 0.0f;
            }
            float waveProgress = sinf(std::numbers::pi_v<float> * (countTimer / 5.0f)) + 1.0f;
            float difficultyScale = gameInstance->getDifficultyLevel() == 0u ? 0.4f : static_cast<float>(gameInstance->getDifficultyLevel());
            uint32_t maxEnemies = waveProgress * (maxEnemyMultiplier * difficultyScale + baseMaxEnemies);
            if (enemyCount >= maxEnemies) {
                readyToSpawn = false;
                return;
            }
            if (!readyToSpawn) {
                readyToSpawn = true;
                spawnTimer = 0.0f;
            }

            spawnTimer += deltaTime;
            float timeRandomness = random.randomFloat(baseSpawnRate * 0.25f);
            float adjustedSpawnInterval = (baseSpawnRate + timeRandomness) * ((5.0f - difficultyScale) / 5.0f);
            if (spawnTimer >= adjustedSpawnInterval) {
                if (spawnChance > 1e-9f) {
                    float spawnRoll = random.randomFloat(0.0f, 1.0f);
                    if (spawnRoll > spawnChance) {
                        spawnTimer = 0.0f;
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
                        glm::radians(random.randomFloat(180.0f)),
                        glm::vec3(0.0f, 1.0f, 0.0f)
                    ),
                    getWorldPosition()
                )
            );
            new EnemyType(
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
        engine::Random& random = engine::Random::global();
    };
};
