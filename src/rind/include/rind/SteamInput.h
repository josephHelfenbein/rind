#pragma once
#include <string>
#include <vector>

#include <engine/InputManager.h>
#include <engine/TextureManager.h>
#include <rind/GameAction.h>

// SDK-free and no-op if RIND_ENABLE_STEAM is undefined
namespace rind::steaminput {
    enum class ActionSet { Gameplay, Menu };

    void init();
    void shutdown();

    void setTextureManager(engine::TextureManager* textureManager);
    void runFrame();
    bool isActive();

    void setActionSet(ActionSet set);
    void collectEvents(std::vector<engine::InputEvent>& out);
    std::string glyphTextureName(rind::GameAction action);
};
