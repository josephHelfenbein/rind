#pragma once

#include <memory>
#include <engine/Renderer.h>

namespace rind {
    class GameInstance {
    public:
        GameInstance();
        ~GameInstance();
    private:
        std::unique_ptr<class engine::Renderer> renderer;
        std::unique_ptr<class engine::SceneManager> sceneManager;
    };
};