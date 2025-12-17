#include <game/GameInstance.h>

#include <engine/Camera.h>
#include <engine/EntityManager.h>
#include <engine/UIManager.h>
#include <engine/ModelManager.h>

static std::function<void(engine::EntityManager*, engine::UIManager*, engine::SceneManager*)> titleScreenScene = [](engine::EntityManager* entityManager, engine::UIManager* uiManager, engine::SceneManager* sceneManager){
    // Title screen UI setup
    engine::TextObject* titleText = new engine::TextObject(
        uiManager,
        glm::mat4(1.0f),
        "TitleText",
        glm::vec3(1.0f, 1.0f, 1.0f),
        "Rind",
        "Lato",
        engine::Corner::Center
    );
    engine::ButtonObject* startButton = new engine::ButtonObject(
        uiManager,
        glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -100.0f, 0.0f)), glm::vec3(0.3, 0.1, 1.0)),
        "StartButton",
        glm::vec3(0.5f, 0.5f, 0.6f),
        glm::vec3(1.0f, 1.0f, 1.0f),
        "ui_window",
        "Start Game",
        "Lato",
        [sceneManager]() {
            sceneManager->setActiveScene(1);
        }
    );
};

static std::function<void(engine::EntityManager*, engine::UIManager*, engine::SceneManager*)> mainGameScene = [](engine::EntityManager* entityManager, engine::UIManager* uiManager, engine::SceneManager* sceneManager){
    // Gameplay scene logic here
    engine::Camera* mainCamera = new engine::Camera(
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

rind::GameInstance::GameInstance() {
    renderer = std::make_unique<engine::Renderer>("Rind");

    std::vector<std::unique_ptr<engine::Scene>> scenes;
    scenes.emplace_back(std::make_unique<engine::Scene>(titleScreenScene));
    scenes.emplace_back(std::make_unique<engine::Scene>(mainGameScene));
    
    entityManager = std::make_unique<engine::EntityManager>(renderer.get());
    inputManager = std::make_unique<engine::InputManager>(renderer.get());
    sceneManager = std::make_unique<engine::SceneManager>(renderer.get(), std::move(scenes));
    textureManager = std::make_unique<engine::TextureManager>(renderer.get(), "src/assets/textures/");
    shaderManager = std::make_unique<engine::ShaderManager>(renderer.get(), "src/assets/shaders/compiled/");
    std::string fontDirectory = "src/assets/fonts/";
    uiManager = std::make_unique<engine::UIManager>(renderer.get(), fontDirectory);
    modelManager = std::make_unique<engine::ModelManager>(renderer.get(), "src/assets/models/");
}

void rind::GameInstance::run() {
    renderer->run();
}