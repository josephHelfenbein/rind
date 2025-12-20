#include <rind/GameInstance.h>

#include <engine/Camera.h>
#include <engine/Light.h>
#include <engine/EntityManager.h>
#include <engine/UIManager.h>
#include <engine/ModelManager.h>

#include <rind/Player.h>

static std::function<void(engine::Renderer*)> titleScreenScene = [](engine::Renderer* renderer){
    // Title screen UI setup
    engine::UIManager* uiManager = renderer->getUIManager();
    engine::SceneManager* sceneManager = renderer->getSceneManager();
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
    renderer->getInputManager()->setUIFocused(true);
    renderer->toggleLockCursor(false);
};

static std::function<void(engine::Renderer*)> mainGameScene = [](engine::Renderer* renderer){
    // Gameplay scene logic here
    engine::ModelManager* modelManager = renderer->getModelManager();
    engine::SceneManager* sceneManager = renderer->getSceneManager();
    engine::EntityManager* entityManager = renderer->getEntityManager();
    std::vector<std::string> metalMaterial = {
        "materials_metal_albedo",
        "materials_metal_metallic",
        "materials_metal_roughness",
        "materials_metal_normal"
    };
    engine::Entity* groundplatform = new engine::Entity(
        entityManager,
        "groundplatform",
        "gbuffer",
        glm::scale(glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -1.8f, 0.0f)), glm::vec3(1.5f, 1.0f, 1.5f)),
        metalMaterial
    );
    engine::Model* platformModel = modelManager ? modelManager->getModel("groundplatform") : nullptr;
    groundplatform->setModel(platformModel);
    auto [platformVerts, platformIndices] = platformModel->loadVertsForModel();
    engine::ConvexHullCollider* platformCollider = new engine::ConvexHullCollider(
        entityManager,
        glm::mat4(1.0f),
        "groundplatform"
    );
    platformCollider->setVertsFromModel(
        std::move(platformVerts),
        std::move(platformIndices),
        glm::mat4(1.0f)
    );
    groundplatform->addChild(platformCollider);
    engine::Entity* groundblock = new engine::Entity(
        entityManager,
        "groundblock",
        "gbuffer",
        glm::scale(glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)),
        metalMaterial
    );
    engine::Model* groundModel = modelManager ? modelManager->getModel("groundblock") : nullptr;
    groundblock->setModel(groundModel);
    auto [vertices, indices] = groundModel->loadVertsForModel();
    engine::ConvexHullCollider* groundCollider = new engine::ConvexHullCollider(
        entityManager,
        glm::mat4(1.0f),
        "groundblock"
    );
    groundCollider->setVertsFromModel(
        std::move(vertices),
        std::move(indices),
        glm::mat4(1.0f)
    );
    groundblock->addChild(groundCollider);

    engine::Light* light = new engine::Light(
        entityManager,
        "light1",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 10.0f, 0.0f)),
        glm::vec3(1.0f),
        5.0f,
        50.0f,
        false
    );
    rind::Player* player = new rind::Player(
        entityManager,
        renderer->getInputManager(),
        "player1",
        "",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)),
        {}
    );
    renderer->getInputManager()->setUIFocused(false);
    renderer->toggleLockCursor(true);
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