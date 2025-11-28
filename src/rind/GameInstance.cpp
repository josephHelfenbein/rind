#include <game/GameInstance.h>
#include <engine/SceneManager.h>

rind::GameInstance::GameInstance() {
    renderer = std::make_unique<engine::Renderer>();

    std::vector<std::unique_ptr<engine::Scene>> scenes = {
        std::make_unique<engine::Scene>(titleScreenScene),
        std::make_unique<engine::Scene>(mainGameScene)
    };

    sceneManager = std::make_unique<engine::SceneManager>(renderer.get(), std::move(scenes));
    textureManager = std::make_unique<engine::TextureManager>(renderer.get(), "src/assets/textures/");
    shaderManager = std::make_unique<engine::ShaderManager>(renderer.get(), "src/assets/shaders/compiled/");
    
}

rind::GameInstance::~GameInstance() {
}

// Scenes

std::function<void()> titleScreenScene = [](){
    // Title screen logic here
};

std::function<void()> mainGameScene = [](){
    // Gameplay scene logic here
};