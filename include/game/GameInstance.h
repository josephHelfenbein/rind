#pragma once

#include <memory>
#include <engine/Renderer.h>
#include <engine/SceneManager.h>
#include <engine/ShaderManager.h>
#include <engine/TextureManager.h>

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
    };
};