#include <rind/Player.h>
#include <rind/Enemy.h>
#include <engine/ParticleManager.h>
#include <engine/UIManager.h>
#include <engine/SceneManager.h>
#include <engine/SettingsManager.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

rind::Player::Player(engine::EntityManager* entityManager, engine::InputManager* inputManager, const std::string& name, glm::mat4 transform)
    : engine::CharacterEntity(entityManager, name, "", transform, {}), inputManager(inputManager) {
        engine::Entity* head = new engine::Entity(
            entityManager,
            "playerHead",
            "",
            glm::mat4(1.0f),
            {},
            true
        );
        addChild(head);
        setHead(head);
        camHolder = new engine::Entity(
            entityManager,
            "camera",
            "",
            glm::mat4(1.0f),
            {}
        );
        head->addChild(camHolder);
        camera = new engine::Camera(
            entityManager,
            "camera",
            glm::mat4(1.0f),
            60.0f,
            0.01f,
            1000.0f,
            true
        );
        camHolder->addChild(camera);
        entityManager->setCamera(camera);
        std::vector<std::string> gunMaterial = {
            "materials_lasergun_albedo",
            "materials_lasergun_metallic",
            "materials_lasergun_roughness",
            "materials_lasergun_normal"
        };
        gunModel = new engine::Entity(
            entityManager,
            "lasergun",
            "gbuffer",
            glm::scale(
                glm::rotate(
                    glm::translate(glm::mat4(1.0f), gunModelTranslation),
                    glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)
                ),
                glm::vec3(gunModelScale)
            ),
            gunMaterial,
            true
        );
        gunModel->setModel(entityManager->getRenderer()->getModelManager()->getModel("lasergun"));
        gunModel->setCastShadow(false);
        camHolder->addChild(gunModel);
        glm::vec3 cameraOffset = glm::vec3(0.4f, -0.15f, -1.0f);
        glm::vec3 offsetFromGunOrigin = cameraOffset - gunModelTranslation;
        glm::mat3 invRotation = glm::mat3(glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
        glm::vec3 rotatedOffset = invRotation * offsetFromGunOrigin;
        glm::vec3 gunEndLocalOffset = rotatedOffset / gunModelScale;
        gunEndPosition = new engine::Entity(
            entityManager,
            "playerGunEndPosition",
            "",
            glm::translate(glm::mat4(1.0f), gunEndLocalOffset),
            {},
            false
        );
        gunModel->addChild(gunEndPosition);
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 0.0f)),
            name,
            glm::vec3(0.5f, 1.8f, 0.5f)
        );
        box->setIsDynamic(true);
        addChild(box);
        setCollider(box);
        playerModel = new engine::Entity(
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
        inputManager->resetKeyStates();
        const float healthbarWidth = 1920.0f;
        float healthbarDisplayWidth = getEntityManager()->getRenderer()->getSwapChainExtent().width;
        float contentScale = 1.0f;
        #ifdef __APPLE__
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(getEntityManager()->getRenderer()->getWindow(), &xscale, &yscale);
        contentScale = std::max(xscale, yscale);
        #endif
        float layoutScale = std::max(getEntityManager()->getRenderer()->getUIScale() * contentScale, 0.0001f);
        float healthbarScale = healthbarDisplayWidth / (healthbarWidth * layoutScale);
        healthbarEmptyObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(healthbarScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 0.0f)),
            "healthbarEmpty",
            glm::vec4(1.0f),
            "ui_healthbar_empty",
            engine::Corner::Bottom
        );
        healthbarObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(healthbarScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 0.0f)),
            "healthbarFull",
            glm::vec4(1.0f),
            "ui_healthbar_full",
            engine::Corner::Bottom
        );
        entityManager->getRenderer()->getInputManager()->registerRecreateSwapChainCallback("playerHealthbarResize", [this]() {
            this->resizeHealthbar();
        });
        scoreCounter = new ScoreCounter(entityManager, entityManager->getRenderer()->getUIManager());
        particleManager = entityManager->getRenderer()->getParticleManager();
        audioManager = entityManager->getRenderer()->getAudioManager();
    }

rind::Player::~Player() {
    inputManager->unregisterCallback("playerInput");
    inputManager->unregisterCallback("playerHealthbarResize");
}

void rind::Player::resizeHealthbar() {
    const float healthbarWidth = 1920.0f;
    float healthbarDisplayWidth = getEntityManager()->getRenderer()->getSwapChainExtent().width;
    float contentScale = 1.0f;
    #ifdef __APPLE__
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(getEntityManager()->getRenderer()->getWindow(), &xscale, &yscale);
    contentScale = std::max(xscale, yscale);
    #endif
    float layoutScale = std::max(getEntityManager()->getRenderer()->getUIScale() * contentScale, 0.0001f);
    float healthbarScale = healthbarDisplayWidth / (healthbarWidth * layoutScale);
    healthbarEmptyObject->setTransform(
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(healthbarScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 0.0f))
    );
    healthbarObject->setTransform(
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(healthbarScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 0.0f))
    );
}

void rind::Player::addScore(uint32_t score) {
    scoreCounter->addScore(score);
}

void rind::Player::update(float deltaTime) {
    const glm::vec3& vel = getVelocity();
    float horizontalSpeed = glm::length(glm::vec3(vel.x, 0.0f, vel.z));
    float rotateSpeed = std::abs(getRotateVelocity().y);
    float speed = horizontalSpeed + std::abs(rotateSpeed);
    glm::vec3 rotateVelocity = getRotateVelocity();
    if (speed > 0.1f) {
        if (playerModel->getAnimationState().currentAnimation != "Run") {
            playerModel->playAnimation("Run", true, speed / 5.0f);
            playerModel->getChildByName("playerShadow")->playAnimation("Run", true, speed / 5.0f);
        } else {
            playerModel->getAnimationState().playbackSpeed = speed / 5.0f;
            playerModel->getChildByName("playerShadow")->getAnimationState().playbackSpeed = speed / 5.0f;
        }
    } else {
        if (playerModel->getAnimationState().currentAnimation != "Idle") {
            playerModel->playAnimation("Idle", true, 1.0f);
            playerModel->getChildByName("playerShadow")->playAnimation("Idle", true, 1.0f);
        }
    }
    engine::CharacterEntity::update(deltaTime);
    if (currentGunRotOffset != glm::vec3(0.0f)) {
        currentGunRotOffset -= currentGunRotOffset * deltaTime * 8.0f;
    }
    if (rotateVelocity != glm::vec3(0.0f)) {
        currentGunRotOffset -= glm::vec3(rotateVelocity.x, rotateVelocity.y, 0.0f) * deltaTime * 0.1f;
    }
    currentGunRotOffset = glm::clamp(currentGunRotOffset, glm::vec3(-0.4f, -0.4f, -0.4f), glm::vec3(0.4f, 0.4f, 0.4f));
    if (currentGunLocOffset != glm::vec3(0.0f)) {
        currentGunLocOffset -= currentGunLocOffset * deltaTime * 8.0f;
    }
    glm::vec3 localVelocity = glm::inverse(glm::mat3(camera->getWorldTransform())) * getVelocity();
    if (localVelocity != glm::vec3(0.0f)) {
        currentGunLocOffset -= localVelocity * deltaTime * 0.05f;
    }
    currentGunLocOffset = glm::clamp(currentGunLocOffset, glm::vec3(-0.25f, -0.25f, -0.25f), glm::vec3(0.25f, 0.25f, 0.25f));
    glm::mat4 offsetMat = glm::mat4(1.0f) *
        glm::rotate(glm::mat4(1.0f), currentGunRotOffset.y, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), currentGunRotOffset.x, glm::vec3(1.0f, 0.0f, 0.0f)) *
        glm::rotate(glm::mat4(1.0f), currentGunRotOffset.z, glm::vec3(0.0f, 0.0f, 1.0f));
    offsetMat = glm::translate(offsetMat, currentGunLocOffset);
    gunModel->setTransform(
        glm::scale(
            glm::rotate(
                glm::translate(offsetMat, gunModelTranslation),
                glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f)
            ),
            glm::vec3(gunModelScale)
        )
    );
    if (cameraShakeIntensity > 0.0f) {
        glm::vec3 randomCameraLoc = glm::vec3(dist(rng), dist(rng), dist(rng)) * cameraShakeIntensity * 0.05f;
        camHolder->setTransform(glm::translate(glm::mat4(1.0f), randomCameraLoc));
        cameraShakeIntensity -= deltaTime;
    }
    if (heartbeatOffset > 0.0f) {
        lastHeartbeat += deltaTime;
        if (lastHeartbeat >= heartbeatOffset) {
            lastHeartbeat = 0.0f;
            audioManager->playSound("player_heartbeat", 0.4f, true);
        }
    }
    if (trailFramesRemaining > 0) {
        float deltaTime = getEntityManager()->getRenderer()->getDeltaTime();
        glm::vec3 velocityOffset = getVelocity() * deltaTime;
        glm::vec3 gunEndWorldPos = glm::vec3(gunEndPosition->getWorldTransform()[3]);
        glm::vec3 playerWorldPos = getWorldPosition();
        glm::vec3 gunOffsetFromPlayer = gunEndWorldPos - playerWorldPos;
        glm::quat yawDelta = glm::angleAxis(rotateVelocity.y * deltaTime, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::vec3 gunAfterYaw = playerWorldPos + velocityOffset + (yawDelta * gunOffsetFromPlayer);
        glm::vec3 playerRight = glm::normalize(glm::vec3(getWorldTransform()[0]));
        glm::quat pitchDelta = glm::angleAxis(rotateVelocity.x * deltaTime, yawDelta * playerRight);
        glm::vec3 gunModelWorldPos = glm::vec3(gunModel->getWorldTransform()[3]);
        glm::vec3 predictedGunModelPos = playerWorldPos + velocityOffset + (yawDelta * (gunModelWorldPos - playerWorldPos));
        glm::vec3 gunOffsetFromGunModel = gunAfterYaw - predictedGunModelPos;
        glm::vec3 currentGunEndPos = predictedGunModelPos + (pitchDelta * gunOffsetFromGunModel);
        particleManager->spawnTrail(
            currentGunEndPos,
            trailEndPos - currentGunEndPos,
            trailColor,
            deltaTime * 1.5f,
            (static_cast<float>(maxTrailFrames) - static_cast<float>(trailFramesRemaining)) / static_cast<float>(maxTrailFrames) * deltaTime
        );
        trailFramesRemaining--;
    }
}

void rind::Player::showPauseMenu(bool uiOnly) {
    engine::UIManager* uiManager = getEntityManager()->getRenderer()->getUIManager();
    pauseUIObject = new engine::UIObject(
        uiManager,
        glm::scale(glm::mat4(1.0f), glm::vec3(0.2f, 0.28f, 1.0f)),
        "pauseUI",
        glm::vec4(0.4f, 0.4f, 0.4f, 0.9f),
        "ui_window",
        engine::Corner::Center
    );
    pauseUIObject->addChild(new engine::TextObject(
        uiManager,
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.17f, 0.17f, 1.0f)), glm::vec3(0.0f, -120.0f, 0.0f)),
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
        "RESUME",
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
        "SETTINGS",
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
        "MENU",
        "Lato",
        [this]() {
            this->inputManager->unregisterCallback("playerInput");
            this->inputManager->unregisterCallback("playerHealthbarResize");
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
        "QUIT",
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
                    if (isGrounded()) {
                        jump(8.0f);
                    } else if (canDoubleJump) {
                        canDash = true;
                        move(glm::vec3(0.0f, 3.0f, 0.0f));
                        canDoubleJump = false;
                        resetDoubleJump = true;
                    }
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
    if (!isGrounded() && !resetDoubleJump) {
        canDoubleJump = true;
    } else if (isGrounded()) {
        resetDoubleJump = false;
    }
    const glm::vec3& currentPress = getPressed();
    if (glm::length(currentPress) > 1e-6f) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastDashTime).count();
        if (duration >= dashCooldown && canDash) {
            dash(currentPress, 100.0f);
            particleManager->burstParticles(
                glm::translate(getWorldTransform(), glm::vec3(0.0f, 0.5f, 0.0f)),
                trailColor,
                -glm::normalize(getVelocity()) * 20.0f,
                50,
                2.0f,
                2.0f
            );
            audioManager->playSound3D("player_dash", getWorldPosition(), 0.5f, true);
            lastDashTime = now;
        }
    }
    if (currentPress.y > 0.0f) {
        stopMove(glm::vec3(0.0f, currentPress.y, 0.0f));
    }
    canDash = false;
}

void rind::Player::damage(float amount) {
    setHealth(getHealth() - amount);
    healthbarObject->setUVClip(glm::vec4(0.0f, 0.0f, getHealth() / getMaxHealth(), 1.0f));
    cameraShakeIntensity = dist(rng) * 0.5f + 1.2f;
    if (getHealth() <= 0.5f * getMaxHealth()) {
        heartbeatOffset = 0.3f + getHealth() / getMaxHealth();
    }
    if (getHealth() <= 0.0f && !isDead) {
        heartbeatOffset = 0.0f;
        isDead = true;
        stopMove(getPressed(), false);
        audioManager->playSound("player_death", 0.5f, true);
        engine::UIManager* uiManager = getEntityManager()->getRenderer()->getUIManager();
        if (getEntityManager()->getRenderer()->isPaused()) {
            hidePauseMenu();
        }
        engine::UIObject* windowTint = new engine::UIObject(
            uiManager,
            glm::scale(glm::mat4(1.0f), glm::vec3(3.0f, 3.0f, 1.0f)), 
            "deathWindowTint",
            glm::vec4(0.5f, 0.0f, 0.0f, 0.8f),
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
            "MENU",
            "Lato",
            [sceneManager]() {
                sceneManager->setActiveScene(0);
            }
        );
        windowTint->addChild(quitButton);
        particleManager->burstParticles(
            glm::translate(getWorldTransform(), glm::vec3(0.0f, 0.5f, 0.0f)),
            trailColor,
            glm::vec3(0.0f, 1.0f, 0.0f) * 5.0f,
            200,
            5.0f,
            0.5f
        );
        particleManager->burstParticles(
            glm::translate(getWorldTransform(), glm::vec3(0.0f, 0.5f, 0.0f)),
            trailColor,
            glm::vec3(0.0f, 1.0f, 0.0f) * 10.0f,
            200,
            8.0f,
            1.0f
        );
        getCollider()->setTransform(
            glm::scale(getCollider()->getTransform(), glm::vec3(0.35f))
        );
        getEntityManager()->getRenderer()->refreshDescriptorSets();
        getEntityManager()->getRenderer()->getInputManager()->setUIFocused(true);
        getEntityManager()->getRenderer()->toggleLockCursor(false);
        inputsDisconnected = true;
    }
}

void rind::Player::shoot() {
    glm::vec3 rayDir = -glm::normalize(glm::vec3(camera->getWorldTransform()[2]));
    glm::vec3 gunPos = glm::vec3(gunEndPosition->getWorldTransform()[3]);
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
        this->getCollider(),
        true
    );
    glm::vec3 endPos = gunPos + rayDir * 1000.0f;
    if (!hits.empty()) {
        engine::Collider::Collision collision = hits[0];
        endPos = collision.worldHitPoint;
        glm::vec3 normal = glm::normalize(collision.mtv.normal);
        glm::vec3 reflectedDir = glm::reflect(rayDir, normal);
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            trailColor,
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            trailColor,
            reflectedDir * 25.0f,
            30,
            4.0f,
            0.4f
        );
        particleManager->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            trailColor,
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.3f
        );
        if (rind::Enemy* character = dynamic_cast<rind::Enemy*>(collision.other->getParent())) {
            character->damage(34.0f);
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
    }
    audioManager->playSound3D("laser_shot", gunPos, 0.5f, true);
    trailFramesRemaining = maxTrailFrames;
    trailEndPos = endPos;
}
