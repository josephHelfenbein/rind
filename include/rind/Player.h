#pragma once

#include <engine/CharacterEntity.h>
#include <engine/InputManager.h>
#include <engine/Camera.h>
#include <chrono>

namespace rind {
    class Player : public engine::CharacterEntity {
    public:
        Player(engine::EntityManager* entityManager, engine::InputManager* inputManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures);
        
        void registerInput(const std::vector<engine::InputEvent>& events);

    private:
        engine::Camera* camera = nullptr;
        engine::InputManager* inputManager = nullptr;
        float mouseSensitivity = 0.003f;
        
        glm::vec3 lastPress = glm::vec3(0.0f);
        std::chrono::steady_clock::time_point lastPressTime = std::chrono::steady_clock::now();
        float dashCooldown = 2.0f; // seconds
        std::chrono::steady_clock::time_point lastDashTime = std::chrono::steady_clock::now();
    };
};