#include <game/GameInstance.h>

rind::GameInstance::GameInstance() {
    renderer = std::make_unique<engine::Renderer>();

    std::vector<std::unique_ptr<engine::Scene>> scenes = {
        std::make_unique<engine::Scene>(titleScreenScene),
        std::make_unique<engine::Scene>(mainGameScene)
    };
    
    entityManager = std::make_unique<engine::EntityManager>(renderer.get());
    inputManager = std::make_unique<engine::InputManager>(renderer.get());
    sceneManager = std::make_unique<engine::SceneManager>(renderer.get(), entityManager.get(), std::move(scenes));
    textureManager = std::make_unique<engine::TextureManager>(renderer.get(), "src/assets/textures/");
    shaderManager = std::make_unique<engine::ShaderManager>(renderer.get(), "src/assets/shaders/compiled/");
    uiManager = std::make_unique<engine::UIManager>(renderer.get(), std::string("src/assets/fonts/"));
    modelManager = std::make_unique<engine::ModelManager>(renderer.get(), "src/assets/models/");
}

rind::GameInstance::~GameInstance() {
}

// Scenes

std::function<void(engine::EntityManager*)> titleScreenScene = [](engine::EntityManager* entityManager){
    // Title screen logic here
};

std::function<void(engine::EntityManager*)> mainGameScene = [](engine::EntityManager* entityManager){
    // Gameplay scene logic here
    Camera* mainCamera = new Camera(
        entityManager,
        "MainCamera",
        glm::mat4(1.0f),
        45.0f,
        16.0f / 9.0f,
        0.1f,
        100.0f,
        true
    );
};