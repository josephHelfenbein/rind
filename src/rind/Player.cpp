#include <rind/Player.h>
#include <rind/Enemy.h>
#include <engine/ParticleManager.h>
#include <engine/UIManager.h>
#include <engine/SceneManager.h>
#include <engine/SettingsManager.h>

rind::Player::Player(engine::EntityManager* entityManager, engine::InputManager* inputManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures = {})
    : engine::CharacterEntity(entityManager, name, shader, transform, textures), inputManager(inputManager) {
        engine::Entity* head = new engine::Entity(
            entityManager,
            "player_head",
            "",
            glm::mat4(1.0f),
            {},
            true
        );
        addChild(head);
        setHead(head);
        camera = new engine::Camera(
            entityManager,
            "camera",
            glm::mat4(1.0f),
            60.0f,
            0.01f,
            1000.0f,
            true
        );
        head->addChild(camera);
        entityManager->setCamera(camera);
        std::vector<std::string> gunMaterial = {
            "materials_lasergun_albedo",
            "materials_lasergun_metallic",
            "materials_lasergun_roughness",
            "materials_lasergun_normal"
        };
        engine::Entity* gunModel = new engine::Entity(
            entityManager,
            "lasergun",
            "gbuffer",
            glm::scale(
                glm::rotate(
                    glm::translate(glm::mat4(1.0f), glm::vec3(0.55856, -0.273792, -0.642208)),
                    glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)
                ),
                glm::vec3(0.16f)
            ),
            gunMaterial,
            true
        );
        gunModel->setModel(entityManager->getRenderer()->getModelManager()->getModel("lasergun"));
        gunModel->setCastShadow(false);
        head->addChild(gunModel);
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 0.0f)),
            name,
            glm::vec3(0.5f, 1.8f, 0.5f)
        );
        addChild(box);
        setCollider(box);
        engine::Entity* playerModel = new engine::Entity(
            entityManager,
            "playerModel",
            "gbuffer",
            glm::scale(
                glm::rotate(
                    glm::translate(
                        glm::mat4(1.0f), glm::vec3(0.0f, -0.4f, 0.2f)
                    ),
                    glm::radians(180.0f), glm::vec3(0.0f, 1.0f, 0.0f)
                ),
                glm::vec3(0.22f)
            ),
            gunMaterial,
            true
        );
        playerModel->setCastShadow(false);
        playerModel->setModel(entityManager->getRenderer()->getModelManager()->getModel("robot-visible"));
        addChild(playerModel);
        playerModel->playAnimation("Run", true, 1.0f);
        engine::Entity* playerShadow = new engine::Entity(
            entityManager,
            "playerShadow",
            "shadow",
            glm::mat4(1.0f),
            {},
            true
        );
        playerShadow->setModel(entityManager->getRenderer()->getModelManager()->getModel("robot"));
        playerModel->addChild(playerShadow);
        playerShadow->playAnimation("Run", true, 1.0f);
        inputManager->registerCallback("playerInput", [this](const std::vector<engine::InputEvent>& events) {
            this->registerInput(events);
        });
    }

void rind::Player::update(float deltaTime) {
    const glm::vec3& vel = getVelocity();
    float horizontalSpeed = glm::length(glm::vec3(vel.x, 0.0f, vel.z));
    float rotateSpeed = getRotateSpeed();
    float speed = horizontalSpeed + std::abs(rotateSpeed);
    if (speed > 0.1f) {
        if (getChildByName("playerModel")->getAnimationState().currentAnimation != "Run") {
            getChildByName("playerModel")->playAnimation("Run", true, speed / 5.0f);
            getChildByName("playerModel")->getChildByName("playerShadow")->playAnimation("Run", true, speed / 5.0f);
        } else {
            getChildByName("playerModel")->getAnimationState().playbackSpeed = speed / 5.0f;
            getChildByName("playerModel")->getChildByName("playerShadow")->getAnimationState().playbackSpeed = speed / 5.0f;
        }
    } else {
        if (getChildByName("playerModel")->getAnimationState().currentAnimation != "Idle") {
            getChildByName("playerModel")->playAnimation("Idle", true, 1.0f);
            getChildByName("playerModel")->getChildByName("playerShadow")->playAnimation("Idle", true, 1.0f);
        }
    }
    engine::CharacterEntity::update(deltaTime);
}

void rind::Player::showPauseMenu(bool uiOnly) {
    engine::UIManager* uiManager = getEntityManager()->getRenderer()->getUIManager();
    pauseUIObject = new engine::UIObject(
        uiManager,
        glm::scale(glm::mat4(1.0f), glm::vec3(0.25f, 0.3f, 1.0f)),
        "pauseUI",
        glm::vec4(1.0f, 1.0f, 1.0f, 0.8f),
        "ui_window",
        engine::Corner::Center
    );
    pauseUIObject->addChild(new engine::TextObject(
        uiManager,
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.15f, 0.15f, 1.0f)), glm::vec3(0.0f, -100.0f, 0.0f)),
        "pauseTitle",
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "Paused",
        "Lato",
        engine::Corner::Top
    ));
    pauseUIObject->addChild(new engine::ButtonObject(
        uiManager,
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.17f, 0.04f, 1.0f)), glm::vec3(0.0f, -1500.0f, 0.0f)),
        "resumeButton",
        glm::vec4(0.2f, 0.2f, 0.2f, 1.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_window",
        "Resume",
        "Lato",
        [this]() {
            this->hidePauseMenu();
            this->getEntityManager()->getRenderer()->getSettingsManager()->hideSettingsUI();
        },
        engine::Corner::Top
    ));
    pauseUIObject->addChild(new engine::ButtonObject(
        uiManager,
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.17f, 0.04f, 1.0f)), glm::vec3(0.0f, -2700.0f, 0.0f)),
        "graphicsSettingsButton",
        glm::vec4(0.2f, 0.2f, 0.2f, 1.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_window",
        "Graphics Settings",
        "Lato",
        [this]() {
            engine::Renderer* renderer = this->getEntityManager()->getRenderer();
            renderer->getSettingsManager()->showSettingsUI();
            renderer->getSettingsManager()->setUIOnClose(
                [this]() {
                    this->showPauseMenu(true);
                }
            );
            this->hidePauseMenu(true);
        },
        engine::Corner::Top
    ));
    pauseUIObject->addChild(new engine::ButtonObject(
        uiManager,
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.17f, 0.04f, 1.0f)), glm::vec3(0.0f, -3900.0f, 0.0f)),
        "quitButton",
        glm::vec4(0.2f, 0.2f, 0.2f, 1.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_window",
        "Main Menu",
        "Lato",
        [this]() {
            this->inputManager->unregisterCallback("playerInput");
            this->inputManager->resetKeyStates();
            this->hidePauseMenu();
            this->getEntityManager()->getRenderer()->getSceneManager()->setActiveScene(0);
        },
        engine::Corner::Top
    ));
    pauseUIObject->addChild(new engine::ButtonObject(
        uiManager,
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.17f, 0.04f, 1.0f)), glm::vec3(0.0f, -5100.0f, 0.0f)),
        "exitButton",
        glm::vec4(0.2f, 0.2f, 0.2f, 1.0f),
        glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
        "ui_window",
        "Quit Game",
        "Lato",
        [this]() {
            glfwSetWindowShouldClose(this->getEntityManager()->getRenderer()->getWindow(), GLFW_TRUE);
        },
        engine::Corner::Top
    ));
    engine::Renderer* renderer = getEntityManager()->getRenderer();
    if (!uiOnly) {
        renderer->setPaused(true);
        renderer->getInputManager()->setUIFocused(true);
        renderer->toggleLockCursor(false);
    }
    renderer->refreshDescriptorSets();
}

void rind::Player::hidePauseMenu(bool uiOnly) {
    if (pauseUIObject) {
        engine::UIManager* uiManager = getEntityManager()->getRenderer()->getUIManager();
        uiManager->removeObject(pauseUIObject->getName());
        pauseUIObject = nullptr;
    }
    engine::Renderer* renderer = getEntityManager()->getRenderer();
    if (!uiOnly) {
        renderer->setPaused(false);
        renderer->getInputManager()->setUIFocused(false);
        renderer->toggleLockCursor(true);
    }
    renderer->refreshDescriptorSets();
}

void rind::Player::registerInput(const std::vector<engine::InputEvent>& events) {
    if (inputsDisconnected) {
        inputManager->unregisterCallback("playerInput");
        inputManager->resetKeyStates();
        return;
    }
    engine::Renderer* renderer = getEntityManager()->getRenderer();
    for (const auto& event : events) {
        if (event.type == engine::InputEvent::Type::KeyPress) {
            if (renderer->isPaused() && event.keyEvent.key != GLFW_KEY_ESCAPE) {
                inputManager->resetKeyStates();
                continue;
            }
            switch (event.keyEvent.key) {
                case GLFW_KEY_ESCAPE:
                    renderer->getSettingsManager()->hideSettingsUI();
                    renderer->isPaused() ? hidePauseMenu() : showPauseMenu();
                    break;
                case GLFW_KEY_W:
                    move(glm::vec3(1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_S:
                    move(glm::vec3(-1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_A:
                    move(glm::vec3(0.0f, 0.0f, -1.0f));
                    break;
                case GLFW_KEY_D:
                    move(glm::vec3(0.0f, 0.0f, 1.0f));
                    break;
                case GLFW_KEY_SPACE:
                    jump(5.0f);
                    break;
                case GLFW_KEY_LEFT_SHIFT:
                    canDash = true;
                    break;
                default:
                    break;
            }
        } else if (event.type == engine::InputEvent::Type::KeyRelease) {
            switch (event.keyEvent.key) {
                case GLFW_KEY_W:
                    stopMove(glm::vec3(1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_S:
                    stopMove(glm::vec3(-1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_A:
                    stopMove(glm::vec3(0.0f, 0.0f, -1.0f));
                    break;
                case GLFW_KEY_D:
                    stopMove(glm::vec3(0.0f, 0.0f, 1.0f));
                    break;
                case GLFW_KEY_LEFT_SHIFT:
                    canDash = false;
                    break;
                default:
                    break;
            }
        } else if (event.type == engine::InputEvent::Type::MouseMove) {
            if (inputManager->getCursorLocked()) {
                float xOffset = static_cast<float>(event.mouseMoveEvent.xPos) * mouseSensitivity;
                float yOffset = static_cast<float>(event.mouseMoveEvent.yPos) * mouseSensitivity;
                rotate(glm::vec3(0.0f, -xOffset, -yOffset));
            }
        } else if (event.type == engine::InputEvent::Type::MouseButtonPress && !renderer->isPaused()) {
            if (event.mouseButtonEvent.button == GLFW_MOUSE_BUTTON_LEFT) {
                if ((std::chrono::steady_clock::now() - lastShotTime) >= std::chrono::duration<float>(shootingCooldown)) {
                    shoot();
                    lastShotTime = std::chrono::steady_clock::now();
                }
            }
        }
    }
    const glm::vec3 currentPress = getPressed();
    if (glm::length(currentPress) > 1e-6f) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDashTime).count();
        if (duration >= dashCooldown && canDash) {
            dash(currentPress, 100.0f);
            lastDashTime = now;
        }
    }
}

void rind::Player::damage(float amount) {
    setHealth(getHealth() - amount);
    if (getHealth() <= 0.0f && !isDead) {
        isDead = true;
        stopMove(getPressed(), false);
        engine::UIManager* uiManager = getEntityManager()->getRenderer()->getUIManager();
        if (getEntityManager()->getRenderer()->isPaused()) {
            hidePauseMenu();
        }
        engine::UIObject* windowTint = new engine::UIObject(
            uiManager,
            glm::scale(glm::mat4(1.0f), glm::vec3(3.0f, 3.0f, 1.0f)), 
            "deathWindowTint",
            glm::vec4(1.0f, 1.0f, 1.0f, 0.25f),
            "ui_window"
        );
        engine::TextObject* diedText = new engine::TextObject(
            uiManager,
            glm::mat4(1.0f), 
            "deathWindowText",
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
            "You Died!",
            "Lato"
        );
        engine::SceneManager* sceneManager = getEntityManager()->getRenderer()->getSceneManager();
        engine::ButtonObject* quitButton = new engine::ButtonObject(
            uiManager,
            glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -100.0f, 0.0f)), glm::vec3(0.15, 0.05, 1.0)),
            "deathMenuButton",
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
            "ui_window",
            "Menu",
            "Lato",
            [sceneManager]() {
                sceneManager->setActiveScene(0);
            }
        );
        windowTint->addChild(quitButton);
        getEntityManager()->getRenderer()->refreshDescriptorSets();
        getEntityManager()->getRenderer()->getInputManager()->setUIFocused(true);
        getEntityManager()->getRenderer()->toggleLockCursor(false);
        inputsDisconnected = true;
    }
}

void rind::Player::shoot() {
    glm::vec3 rayDir = -glm::normalize(glm::vec3(camera->getWorldTransform()[2]));
    constexpr float framePrediction = 0.016f;
    glm::vec3 velocityOffset = getVelocity() * framePrediction;
    glm::vec3 gunPos = glm::vec3(camera->getWorldTransform() * glm::vec4(0.4f, -0.15f, -1.0f, 1.0f)) + velocityOffset;
    engine::ParticleManager* particleManager = getEntityManager()->getRenderer()->getParticleManager();
    particleManager->burstParticles(
        glm::translate(glm::mat4(1.0f), gunPos),
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
        rayDir * 15.0f,
        10,
        3.0f,
        0.3f
    );
    std::vector<engine::Collider::Collision> hits = engine::Collider::raycast(
        getEntityManager(),
        camera->getWorldPosition(),
        rayDir,
        1000.0f,
        true
    );
    glm::vec3 endPos = gunPos + rayDir * 1000.0f;
    engine::AudioManager* audioManager = getEntityManager()->getRenderer()->getAudioManager();
    for (engine::Collider::Collision& collision : hits) {
        if (collision.other->getParent() == this) continue; // ignore self hits
        endPos = collision.worldHitPoint;
        glm::vec3 normal = glm::normalize(collision.mtv.normal);
        glm::vec3 reflectedDir = glm::reflect(rayDir, normal);
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            reflectedDir * 25.0f,
            30,
            4.0f,
            0.4f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.3f
        );
        if (rind::Enemy* character = dynamic_cast<rind::Enemy*>(collision.other->getParent())) {
            character->damage(25.0f);
            if (character->getState() == EnemyState::Idle) {
                character->rotateToPlayer();
                if (!character->checkVisibilityOfPlayer()) {
                    character->setWanderTarget(getWorldPosition());
                    character->wanderTo(getEntityManager()->getRenderer()->getDeltaTime());
                    audioManager->playSound3D("enemy_track", character->getWorldPosition(), 0.5f, true);
                }
            }
            audioManager->playSound3D("laser_enemy_impact", collision.worldHitPoint, 0.5f, true);
        } else {
            audioManager->playSound3D("laser_ground_impact", collision.worldHitPoint, 0.5f, true);
        }
        break;
    }
    audioManager->playSound3D("laser_shot", gunPos, 0.5f, true);
    particleManager->spawnTrail(
        gunPos,
        endPos - gunPos,
        glm::vec4(1.0f, 0.0f, 0.0f, 1.0f),
        0.1f
    );
}
