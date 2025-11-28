#pragma once

#include <memory>
#include <engine/Renderer.h>

namespace rift {
    class GameInstance {
    public:
        GameInstance();
        ~GameInstance();
    private:
        std::unique_ptr<class engine::Renderer> renderer;
    };
};