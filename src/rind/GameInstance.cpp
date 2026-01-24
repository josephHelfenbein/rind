#include <rind/GameInstance.h>

#include <engine/Camera.h>
#include <engine/Light.h>
#include <engine/EntityManager.h>
#include <engine/UIManager.h>
#include <engine/ModelManager.h>
#include <engine/io.h>

#include <rind/Player.h>
#include <rind/Enemy.h>

static std::function<void(engine::Renderer*)> titleScreenScene = [](engine::Renderer* renderer){
    // Title screen UI setup
    engine::UIManager* uiManager = renderer->getUIManager();
    engine::EntityManager* entityManager = renderer->getEntityManager();
    engine::ModelManager* modelManager = renderer->getModelManager();
    engine::SceneManager* sceneManager = renderer->getSceneManager();
    engine::UIObject* logoObject = new engine::UIObject(
        uiManager,
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, -0.5f, 1.0f)), glm::vec3(0.0f, -200.0f, 0.0f)),
        "LogoObject",
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_logo-light",
        engine::Corner::Center
    );
    engine::ButtonObject* startButton = new engine::ButtonObject(
        uiManager,
        glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -100.0f, 0.0f)), glm::vec3(0.12, 0.04, 1.0)),
        "StartButton",
        glm::vec4(0.5f, 0.5f, 0.6f, 1.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_window",
        "START",
        "Lato",
        [sceneManager]() {
            sceneManager->setActiveScene(1);
        }
    );
    engine::ButtonObject* quitButton = new engine::ButtonObject(
        uiManager,
        glm::scale(glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -200.0f, 0.0f)), glm::vec3(0.12, 0.04, 1.0)),
        "QuitButton",
        glm::vec4(0.5f, 0.5f, 0.6f, 1.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_window",
        "QUIT",
        "Lato",
        []() {
            std::exit(0);
        }
    );
    std::function<void()> settingsCallback = [renderer, logoObject, startButton, quitButton]() {
        renderer->getSettingsManager()->showSettingsUI();
        renderer->getUIManager()->removeObject(logoObject->getName());
        renderer->getUIManager()->removeObject(startButton->getName());
        renderer->getUIManager()->removeObjectDeferred("SettingsButton");
        renderer->getUIManager()->removeObject(quitButton->getName());
        renderer->getSettingsManager()->setUIOnClose(
            [renderer](){
                renderer->getSceneManager()->setActiveScene(0);
            }
        );
    };
    engine::ButtonObject* settingsButton = new engine::ButtonObject(
        uiManager,
        glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -150.0f, 0.0f)), glm::vec3(0.12, 0.04, 1.0)),
        "SettingsButton",
        glm::vec4(0.5f, 0.5f, 0.6f, 1.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_window",
        "SETTINGS",
        "Lato",
        settingsCallback
    );
    engine::Camera* camera = new engine::Camera(
        entityManager,
        "titleCamera",
        glm::inverse(glm::lookAt(
            glm::vec3(0.0f, 0.5f, 3.0f),
            glm::vec3(0.0f, 1.0f, 0.0f),
            glm::vec3(0.0f, 1.0f, 0.0f)
        )),
        45.0f,
        0.1f,
        150.0f
    );
    std::vector<std::string> groundMaterial = {
        "materials_ground_albedo",
        "materials_ground_metallic",
        "materials_ground_roughness",
        "materials_ground_normal"
    };
    engine::Model* platformModel = modelManager->getModel("groundplatform");
    engine::Entity* boxplatform = new engine::Entity(
        renderer->getEntityManager(),
        "boxPlatform",
        "gbuffer",
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(1.5f, 1.0f,1.5f)), glm::vec3(0.0f, -2.0f, 0.0f)),
        groundMaterial
    );
    boxplatform->setModel(platformModel);
    engine::Model* playerModel = modelManager->getModel("robot");
    std::vector<std::string> gunMaterial = {
        "materials_lasergun_albedo",
        "materials_lasergun_metallic",
        "materials_lasergun_roughness",
        "materials_lasergun_normal"
    };
    std::vector<std::string> wallsMaterial = {
        "materials_walls_albedo",
        "materials_walls_metallic",
        "materials_walls_roughness",
        "materials_walls_normal"
    };
    engine::Model* walls = modelManager->getModel("walls");
    engine::Entity* wallEntity = new engine::Entity(
        entityManager,
        "walls",
        "gbuffer",
        glm::mat4(1.0f),
        wallsMaterial
    );
    wallEntity->setModel(walls);
    engine::Entity* playerEntity = new engine::Entity(
        entityManager,
        "titlePlayer",
        "gbuffer",
        glm::scale(glm::mat4(1.0f), glm::vec3(0.22f)),
        gunMaterial
    );
    playerEntity->setModel(playerModel);
    engine::Light* sceneLight = new engine::Light(
        entityManager,
        "titleLight",
        glm::translate(glm::mat4(1.0f), glm::vec3(-3.0f, 1.5f, -4.0f)),
        glm::vec3(1.0f, 0.5f, 0.5f),
        0.25f,
        30.0f,
        false
    );
    engine::Light* sceneLight2 = new engine::Light(
        entityManager,
        "titleLight2",
        glm::translate(glm::mat4(1.0f), glm::vec3(2.0f, 1.0f, 1.0f)),
        glm::vec3(0.5f, 0.5f, 1.0f),
        0.75f,
        15.0f,
        false
    );
    engine::Light* sceneLight3 = new engine::Light(
        entityManager,
        "titleLight3",
        glm::translate(glm::mat4(1.0f), glm::vec3(-30.0f, 2.0f, 0.0f)),
        glm::vec3(1.0f, 1.0f, 1.0f),
        2.0f,
        200.0f,
        false
    );
    renderer->getInputManager()->setUIFocused(true);
    renderer->toggleLockCursor(false);
};

static std::function<void(engine::Renderer*)> mainGameScene = [](engine::Renderer* renderer){
    // Gameplay scene logic here
    engine::ModelManager* modelManager = renderer->getModelManager();
    engine::SceneManager* sceneManager = renderer->getSceneManager();
    engine::EntityManager* entityManager = renderer->getEntityManager();
    engine::UIManager* uiManager = renderer->getUIManager();
    engine::UIObject* crosshair = new engine::UIObject(
        uiManager,
        glm::scale(glm::mat4(1.0f), glm::vec3(0.2f, 0.2f, 1.0f)),
        "crosshair",
        glm::vec4(1.0f, 1.0f, 1.0f, 0.8f),
        "ui_crosshair",
        engine::Corner::Center
    );
    std::vector<std::string> rockMaterial = {
        "materials_rock_albedo",
        "materials_rock_metallic",
        "materials_rock_roughness",
        "materials_rock_normal"
    };
    std::vector<std::string> wallsMaterial = {
        "materials_walls_albedo",
        "materials_walls_metallic",
        "materials_walls_roughness",
        "materials_walls_normal"
    };
    std::vector<std::string> lightMaterial = {
        "materials_light_albedo",
        "materials_light_metallic",
        "materials_light_roughness",
        "materials_light_normal"
    };
    std::vector<std::string> groundMaterial = {
        "materials_ground_albedo",
        "materials_ground_metallic",
        "materials_ground_roughness",
        "materials_ground_normal"
    };
    engine::Entity* groundplatform = new engine::Entity(
        entityManager,
        "groundplatform",
        "gbuffer",
        glm::scale(glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -1.8f, 0.0f)), glm::vec3(1.5f, 1.0f, 1.5f)),
        groundMaterial
    );
    engine::Model* platformModel = modelManager ? modelManager->getModel("groundplatform") : nullptr;
    engine::Model* platformColliderModel = modelManager ? modelManager->getModel("groundplatform-collider") : nullptr;
    groundplatform->setModel(platformModel);
    auto [platformVerts, platformIndices] = platformColliderModel->loadVertsForModel();
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
        groundMaterial
    );
    engine::Model* groundModel = modelManager ? modelManager->getModel("groundblock") : nullptr;
    groundblock->setModel(groundModel);
    engine::Model* groundColliderModel = modelManager ? modelManager->getModel("groundblock-collider") : nullptr;
    auto [vertices, indices] = groundColliderModel->loadVertsForModel();
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

    engine::Model* groundCubesModel = modelManager ? modelManager->getModel("groundcubes") : nullptr;
    engine::Entity* groundcubes = new engine::Entity(
        entityManager,
        "groundcubes",
        "gbuffer",
        glm::scale(glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)),
        rockMaterial
    );
    groundcubes->setModel(groundCubesModel);

    engine::Model* trueGroundModel = modelManager ? modelManager->getModel("trueground") : nullptr;
    engine::Entity* trueground = new engine::Entity(
        entityManager,
        "trueground",
        "gbuffer",
        glm::scale(glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)),
        rockMaterial
    );
    trueground->setModel(trueGroundModel);

    engine::Model* walls = modelManager ? modelManager->getModel("walls") : nullptr;
    engine::Entity* wallEntity = new engine::Entity(
        entityManager,
        "walls",
        "gbuffer",
        glm::scale(glm::translate(glm::mat4(1.0), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)),
        wallsMaterial
    );
    wallEntity->setModel(walls);

    engine::Model* lightModel = modelManager ? modelManager->getModel("light") : nullptr;
    engine::Model* lightColliderModel = modelManager ? modelManager->getModel("light-collider") : nullptr;
    
    engine::Entity* lightObject1 = new engine::Entity(
        entityManager,
        "lightObject1",
        "gbuffer",
        glm::translate(glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)), engine::blenderRemap(glm::vec3(13.5296f, -13.3857f, -0.136268f))),
        lightMaterial
    );
    lightObject1->setModel(lightModel);
    engine::Light* light = new engine::Light(
        entityManager,
        "light1",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::vec3(1.0f),
        5.0f,
        150.0f,
        false
    );
    lightObject1->addChild(light);
    
    engine::ConvexHullCollider* lightCollider = new engine::ConvexHullCollider(
        entityManager,
        glm::mat4(1.0f),
        "lightObject1"
    );
    auto [lightVerts, lightIndices] = lightColliderModel->loadVertsForModel();
    lightCollider->setVertsFromModel(
        lightVerts,
        lightIndices,
        glm::mat4(1.0f)
    );
    lightObject1->addChild(lightCollider);

    engine::Entity* lightObject2 = new engine::Entity(
        entityManager,
        "lightObject2",
        "gbuffer",
        glm::translate(glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)), engine::blenderRemap(glm::vec3(13.5296f, 13.6124f, -0.136268f))),
        lightMaterial
    );
    lightObject2->setModel(lightModel);
    engine::Light* light2 = new engine::Light(
        entityManager,
        "light2",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::vec3(1.0f),
        5.0f,
        150.0f,
        false
    );
    lightObject2->addChild(light2);

    engine::ConvexHullCollider* light2Collider = new engine::ConvexHullCollider(
        entityManager,
        glm::mat4(1.0f),
        "lightObject2"
    );
    light2Collider->setVertsFromModel(
        lightVerts,
        lightIndices,
        glm::mat4(1.0f)
    );
    lightObject2->addChild(light2Collider);

    engine::Entity* lightObject3 = new engine::Entity(
        entityManager,
        "lightObject3",
        "gbuffer",
        glm::translate(glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)), engine::blenderRemap(glm::vec3(-13.365f, 13.6124f, -0.136268f))),
        lightMaterial
    );
    lightObject3->setModel(lightModel);
    engine::Light* light3 = new engine::Light(
        entityManager,
        "light3",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::vec3(1.0f),
        5.0f,
        150.0f,
        false
    );
    lightObject3->addChild(light3);

    engine::ConvexHullCollider* light3Collider = new engine::ConvexHullCollider(
        entityManager,
        glm::mat4(1.0f),
        "lightObject3"
    );
    light3Collider->setVertsFromModel(
        lightVerts,
        lightIndices,
        glm::mat4(1.0f)
    );
    lightObject3->addChild(light3Collider);

    engine::Entity* lightObject4 = new engine::Entity(
        entityManager,
        "lightObject4",
        "gbuffer",
        glm::translate(glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -1.5f, 0.0f)), glm::vec3(1.5f, 1.5f, 1.5f)), engine::blenderRemap(glm::vec3(-13.365f, -13.3857f, -0.136268f))),
        lightMaterial
    );
    lightObject4->setModel(lightModel);
    engine::Light* light4 = new engine::Light(
        entityManager,
        "light4",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 1.0f, 0.0f)),
        glm::vec3(1.0f),
        5.0f,
        150.0f,
        false
    );
    lightObject4->addChild(light4);

    engine::ConvexHullCollider* light4Collider = new engine::ConvexHullCollider(
        entityManager,
        glm::mat4(1.0f),
        "lightObject4"
    );
    light4Collider->setVertsFromModel(
        std::move(lightVerts),
        std::move(lightIndices),
        glm::mat4(1.0f)
    );
    lightObject4->addChild(light4Collider);

    rind::Player* player = new rind::Player(
        entityManager,
        renderer->getInputManager(),
        "player1",
        "",
        glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 5.0f, 0.0f)),
        {}
    );
    rind::Enemy* enemy1 = new rind::Enemy(
        entityManager,
        player,
        "enemy1",
        "",
        glm::translate(glm::mat4(1.0f), glm::vec3(30.0f, -10.0f, 0.0f)),
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
    uiManager = std::make_unique<engine::UIManager>(renderer.get(), "src/assets/fonts/");
    modelManager = std::make_unique<engine::ModelManager>(renderer.get(), "src/assets/models/");
    particleManager = std::make_unique<engine::ParticleManager>(renderer.get());
    audioManager = std::make_unique<engine::AudioManager>(renderer.get(), "src/assets/audio/");
    settingsManager = std::make_unique<engine::SettingsManager>(renderer.get());
}

void rind::GameInstance::run() {
    renderer->run();
}
