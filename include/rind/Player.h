#pragma once

#include <engine/CharacterEntity.h>
#include <engine/InputManager.h>
#include <engine/Camera.h>
#include <chrono>

namespace rind {
    class Player : public engine::CharacterEntity {
    public:
        Player(engine::EntityManager* entityManager, engine::InputManager* inputManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures);

        void update(float deltaTime) override;

        void showPauseMenu(bool uiOnly = false);
        void hidePauseMenu(bool uiOnly = false);

        void registerInput(const std::vector<engine::InputEvent>& events);
        void shoot();
        void damage(float amount) override;

    private:
        engine::Camera* camera = nullptr;
        engine::InputManager* inputManager = nullptr;
        float mouseSensitivity = 0.003f;

        engine::UIObject* pauseUIObject = nullptr;

        bool isDead = false;

        bool inputsDisconnected = false;
        
        float shootingCooldown = 0.2f;
        std::chrono::steady_clock::time_point lastShotTime = std::chrono::steady_clock::now();
        
        bool canDash = false;
        long long dashCooldown = 2000; // ms
        std::chrono::steady_clock::time_point lastDashTime = std::chrono::steady_clock::now();
    };
};
