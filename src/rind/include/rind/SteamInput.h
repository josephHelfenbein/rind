#pragma once
#include <string>
#include <vector>

#include <engine/InputManager.h>
#include <rind/GameAction.h>

namespace engine {
    class TextureManager;
}

// SDK-free and no-op if RIND_ENABLE_STEAM is undefined
namespace rind::steaminput {
    void init();
    void shutdown();

    void setTextureManager(engine::TextureManager* textureManager);
    void runFrame();
    bool isActive();

    void collectEvents(std::vector<engine::InputEvent>& out);
    void getCursorInput(float& x, float& y, float& trigger);
    std::string glyphTextureName(rind::GameAction action);
};
