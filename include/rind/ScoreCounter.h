#pragma once
#include <engine/EntityManager.h>
#include <engine/UIManager.h>

namespace rind {
    class ScoreCounter : public engine::Entity {
    public:
        ScoreCounter(engine::EntityManager* entityManager, engine::UIManager* uiManager)
            : engine::Entity(entityManager, "scoreCounter", "", glm::mat4(1.0f), {}), uiManager(uiManager) {
            counter = new engine::TextObject(
                uiManager,
                glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -5.0f, 0.0f)), glm::vec3(0.5f, 0.5f, 1.0f)),
                "scoreCounter",
                glm::vec4(1.0f),
                "SCORE: 0",
                "Lato",
                engine::Corner::Top
            );
        };

        void update(float deltaTime) override {
            if (growFrame >= 0.0f) {
                float scaleAmount = 1.0f;
                if (growFrame < 0.1f) {
                    float t = growFrame / 0.1f;
                    t = 1.0f - (1.0f - t) * (1.0f - t);
                    scaleAmount = std::lerp(1.0f, growGoal, t);
                } else if (growFrame < 0.25f) {
                    float t = (growFrame - 0.1f) / 0.15f;
                    t = 1.0f - (1.0f - t) * (1.0f - t);
                    scaleAmount = std::lerp(growGoal, 1.0f, t);
                } else if (growFrame < 0.35f) {
                    float t = (growFrame - 0.25f) / 0.1f;
                    float peak = 1.0f + (growGoal - 1.0f) * 0.2f;
                    scaleAmount = 1.0f + (peak - 1.0f) * std::sin(t * 3.14159f);
                } else if (growFrame < 0.4f) {
                    float t = (growFrame - 0.35f) / 0.05f;
                    t = 1.0f - (1.0f - t) * (1.0f - t);
                    scaleAmount = std::lerp(1.0f + (growGoal - 1.0f) * 0.05f, 1.0f, t);
                }
                counter->setTransform(
                    glm::translate(
                        glm::scale(glm::mat4(1.0f), glm::vec3(0.5f * scaleAmount, 0.5f * scaleAmount, 1.0f)),
                        glm::vec3(0.0f, -5.0f, 0.0f)
                    )
                );
                growFrame += deltaTime;
            }
            if (growFrame >= 0.4f) {
                growFrame = -1.0f;
            }
        }

        void addScore(uint32_t points) {
            score += points;
            std::string newScoreText = "SCORE: " + std::to_string(score);
            counter->setText(std::move(newScoreText));
            float factor = (std::clamp(static_cast<float>(points), 100.0f, 300.0f) - 100.0f) / 200.0f;
            float minScale = std::lerp(1.25f, 1.4f, factor);
            float maxScale = std::lerp(1.4f, 1.6f, factor);
            growGoal = minScale + (dist(rng) + 1.0f) * 0.5f * (maxScale - minScale);
            growFrame = 0.0f;
        }

    private:
        engine::UIManager* uiManager;
        uint32_t score = 0u;
        engine::TextObject* counter = nullptr;

        float growGoal = 1.0f;
        float growFrame = -1.0f;
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    };
};