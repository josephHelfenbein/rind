#pragma once

#include <engine/CharacterEntity.h>
#include <engine/InputManager.h>
#include <engine/Camera.h>
#include <rind/ScoreCounter.h>
#include <chrono>

namespace rind {
    class Player : public engine::CharacterEntity {
    public:
        Player(engine::EntityManager* entityManager, engine::InputManager* inputManager, const std::string& name, glm::mat4 transform);
        ~Player();

        void update(float deltaTime) override;

        void showPauseMenu(bool uiOnly = false);
        void hidePauseMenu(bool uiOnly = false);

        void registerInput(const std::vector<engine::InputEvent>& events);
        void shoot();
        void damage(float amount) override;

        void resizeHealthbar();
        void addScore(uint32_t score);

    private:
        engine::Camera* camera = nullptr;
        engine::Entity* gunEndPosition = nullptr;
        float cameraShakeIntensity = 0.0f;

        const float gunModelScale = 0.16f;
        const glm::vec3 gunModelTranslation = glm::vec3(0.55856f, -0.273792f, -0.642208f);
        engine::Entity* gunModel = nullptr;
        engine::Entity* playerModel = nullptr;
        engine::Entity* camHolder = nullptr;

        bool canDoubleJump = false;
        bool resetDoubleJump = false;

        float heartbeatOffset = 0.0f;
        float lastHeartbeat = 0.0f;

        glm::vec3 currentGunRotOffset = glm::vec3(0.0f);
        glm::vec3 currentGunLocOffset = glm::vec3(0.0f);
        
        engine::InputManager* inputManager = nullptr;
        engine::ParticleManager* particleManager = nullptr;
        engine::AudioManager* audioManager = nullptr;
        float mouseSensitivity = 0.003f;

        engine::UIObject* pauseUIObject = nullptr;
        engine::UIObject* healthbarObject = nullptr;
        engine::UIObject* healthbarEmptyObject = nullptr;
        engine::UIObject* damageEffectObject = nullptr;
        ScoreCounter* scoreCounter = nullptr;

        bool isDead = false;

        bool inputsDisconnected = false;
        
        float shootingCooldown = 0.2f;
        std::chrono::steady_clock::time_point lastShotTime = std::chrono::steady_clock::now();
        
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

        bool canDash = false;
        long long dashCooldown = 500; // ms
        std::chrono::steady_clock::time_point lastDashTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(dashCooldown);

        uint32_t trailFramesRemaining = 0u;
        uint32_t maxTrailFrames = 5u;
        glm::vec3 trailEndPos = glm::vec3(0.0f);
        glm::vec4 trailColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    };
};
