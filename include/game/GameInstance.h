#pragma once

#include <memory>
#include <engine/Renderer.h>
#include <engine/SceneManager.h>
#include <engine/ShaderManager.h>
#include <engine/TextureManager.h>
#include <engine/EntityManager.h>
#include <engine/InputManager.h>

namespace rind {
    class GameInstance {
    public:
        GameInstance();
        ~GameInstance();
    private:
        std::unique_ptr<engine::Renderer> renderer;
        std::unique_ptr<engine::SceneManager> sceneManager;
        std::unique_ptr<engine::ShaderManager> shaderManager;
        std::unique_ptr<engine::TextureManager> textureManager;
        std::unique_ptr<engine::EntityManager> entityManager;
        std::unique_ptr<engine::InputManager> inputManager;
    };
};