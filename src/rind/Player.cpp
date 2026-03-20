#include <rind/Player.h>
#include <rind/Enemy.h>
#include <rind/TempTrigger.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <engine/UIManager.h>
#include <engine/SceneManager.h>
#include <engine/SettingsManager.h>
#include <rind/Grenade.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

rind::Player::Player(
    engine::EntityManager* entityManager,
    engine::InputManager* inputManager,
    const std::string& name,
    const glm::mat4& transform
) : rind::CharacterEntity(entityManager, name, "", transform, {}, engine::Entity::EntityType::Player), inputManager(inputManager) {
        engine::Entity* head = new engine::Entity(
            entityManager,
            "playerHead",
            "",
            glm::mat4(1.0f),
            {},
            true,
            engine::Entity::EntityType::Empty
        );
        addChild(head);
        setHead(head);
        camHolder = new engine::Entity(
            entityManager,
            "camera",
            "",
            glm::mat4(1.0f),
            {},
            false,
            engine::Entity::EntityType::Empty
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
            true,
            engine::Entity::EntityType::Model
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
            false,
            engine::Entity::EntityType::Empty
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
            true,
            engine::Entity::EntityType::Model
        );
        playerModel->setCastShadow(false);
        playerModel->setModel(entityManager->getRenderer()->getModelManager()->getModel("robot-visible"));
        addChild(playerModel);
        playerModel->playAnimation("Run", true, 1.0f);
        playerShadow = new engine::Entity(
            entityManager,
            "playerShadow",
            "shadow",
            glm::mat4(1.0f),
            {},
            true,
            engine::Entity::EntityType::Model
        );
        playerShadow->setVisible(false);
        playerShadow->setModel(entityManager->getRenderer()->getModelManager()->getModel("robot"));
        playerModel->addChild(playerShadow);
        playerShadow->playAnimation("Run", true, 1.0f);
        playerArm = new engine::Entity(
            entityManager,
            "playerArm",
            "gbuffer",
            glm::mat4(1.0f),
            gunMaterial,
            true,
            engine::Entity::EntityType::Model
        );
        playerArm->setCastShadow(false);
        playerArm->setModel(entityManager->getRenderer()->getModelManager()->getModel("robot-arm"));
        playerModel->addChild(playerArm);
        playerArm->playAnimation("Run", true, 1.0f);
        playerArm->setVisible(false);
        inputManager->registerCallback("playerInput", [this](const std::vector<engine::InputEvent>& events) {
            this->registerInput(events);
        });
        inputManager->resetKeyStates();
        const float baseWidth = 1920.0f;
        const float baseHeight = 1080.0f;
        VkExtent2D displaySize = getEntityManager()->getRenderer()->getSwapChainExtent();
        float contentScale = 1.0f;
    #ifdef __APPLE__
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(getEntityManager()->getRenderer()->getWindow(), &xscale, &yscale);
        contentScale = std::max(xscale, yscale);
    #endif
        float layoutScale = std::max(getEntityManager()->getRenderer()->getUIScale() * contentScale, 0.0001f);
        float widthScale = static_cast<float>(displaySize.width) / (baseWidth * layoutScale);
        float heightScale = static_cast<float>(displaySize.height) / (baseHeight * layoutScale);
        healthbarEmptyObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 1.0f)),
            "healthbarEmpty",
            glm::vec4(1.0f),
            "ui_healthbar_empty",
            engine::Corner::Bottom
        );
        healthbarObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 0.0f)),
            "healthbarFull",
            glm::vec4(1.0f),
            "ui_healthbar_full",
            engine::Corner::Bottom
        );
        damageEffectObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, heightScale, 1.0f)), glm::vec3(0.0f, 0.0f, 2.0f)),
            "damageEffect",
            glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
            "ui_healthbar_overlay"
        );
        healEffectObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, heightScale, 1.0f)), glm::vec3(0.0f, 0.0f, 2.0f)),
            "healEffect",
            glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
            "ui_heal_overlay"
        );
        engine::UIObject* crosshair = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.2f, 0.2f, 1.0f)), glm::vec3(0.0f, 0.0f, 1.0f)),
            "crosshair",
            glm::vec4(1.0f, 1.0f, 1.0f, 0.8f),
            "ui_cursor_crosshair",
            engine::Corner::Center
        );
        hitmarkerObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.2f, 0.2f, 1.0f)), glm::vec3(0.0f, 0.0f, 1.0f)),
            "hitmarker",
            glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
            "ui_cursor_hitmarker",
            engine::Corner::Center
        );
        grenadeEmptyIconObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, -0.3f, 1.0f)), glm::vec3(100.0f, -300.0f, 1.0f)),
            "grenadeEmptyIcon",
            glm::vec4(1.0f),
            "ui_grenade_empty",
            engine::Corner::BottomLeft
        );
        grenadeKeybindHintObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.6f, -0.6f, 1.0f)), glm::vec3(80.0f, -120.0f, 0.5f)),
            "grenadeKeybindHint",
            glm::vec4(1.0f),
            "inputs_keyboard_common_q",
            engine::Corner::BottomLeft
        );
        grenadeFullIconObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.3f, -0.3f, 1.0f)), glm::vec3(100.0f, -300.0f, 0.0f)),
            "grenadeFullIcon",
            glm::vec4(1.0f),
            "ui_grenade_full",
            engine::Corner::BottomLeft
        );
        grenadeFullIconObject->setUVClip(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        entityManager->getRenderer()->getInputManager()->registerRecreateSwapChainCallback("playerHealthbarResize", [this]() {
            this->resizeHealthbar();
        });
        keybindHintObject = new engine::UIObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.75f, -0.75f, 1.0f)), glm::vec3(-80.0f, 80.0f, 1.0f)),
            "keybindHint",
            glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
            "inputs_keyboard_mouse_right",
            engine::Corner::TopRight
        );
        keybindHintTextObject = new engine::TextObject(
            entityManager->getRenderer()->getUIManager(),
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.5f, 0.5f, 1.0f)), glm::vec3(-220.0f, -45.0f, 0.0f)),
            "keybindHintText",
            glm::vec4(1.0f, 1.0f, 1.0f, 0.0f),
            "",
            "Lato",
            engine::Corner::TopRight
        );
        scoreCounter = new ScoreCounter(entityManager, entityManager->getRenderer()->getUIManager());
        particleManager = entityManager->getRenderer()->getParticleManager();
        audioManager = entityManager->getRenderer()->getAudioManager();
        volumetricManager = entityManager->getRenderer()->getVolumetricManager();
        float messageChoice = dist(rng) * 0.5f + 0.5f;
        if (messageChoice < 0.33f) {
            showKeybindHint(HintActions::Dash, "To dash, press");
        } else if (messageChoice < 0.66f) {
            showKeybindHint(HintActions::Jump, "To double jump, double tap");
        } else {
            showKeybindHint(HintActions::Punch, "To punch, press");
        }
    }

rind::Player::~Player() {
    inputManager->unregisterCallback("playerInput");
    inputManager->unregisterCallback("playerHealthbarResize");
}

void rind::Player::resizeHealthbar() {
    const float baseWidth = 1920.0f;
    const float baseHeight = 1080.0f;
    VkExtent2D displaySize = getEntityManager()->getRenderer()->getSwapChainExtent();
    float contentScale = 1.0f;
#ifdef __APPLE__
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(getEntityManager()->getRenderer()->getWindow(), &xscale, &yscale);
    contentScale = std::max(xscale, yscale);
#endif
    float layoutScale = std::max(getEntityManager()->getRenderer()->getUIScale() * contentScale, 0.0001f);
    float widthScale = static_cast<float>(displaySize.width) / (baseWidth * layoutScale);
    float heightScale = static_cast<float>(displaySize.height) / (baseHeight * layoutScale);
    healthbarEmptyObject->setTransform(
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 1.0f))
    );
    healthbarObject->setTransform(
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, -0.08f, 1.0f)), glm::vec3(0.0f, -280.0f, 0.0f))
    );
    damageEffectObject->setTransform(
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, heightScale, 1.0f)), glm::vec3(0.0f, 0.0f, 2.0f))
    );
    healEffectObject->setTransform(
        glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(widthScale, heightScale, 1.0f)), glm::vec3(0.0f, 0.0f, 2.0f))
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
    if (speed > 0.1f && punchTimer <= 0.2f) {
        if (playerModel->getAnimationState().currentAnimation != "Run") {
            playerModel->playAnimation("Run", true, speed / 5.0f);
        } else {
            playerModel->getAnimationState().playbackSpeed = speed / 5.0f;
        }
        if (playerShadow->getAnimationState().currentAnimation != "Run") {
            playerShadow->playAnimation("Run", true, speed / 5.0f);
        } else {
            playerShadow->getAnimationState().playbackSpeed = speed / 5.0f;
        }
        if (playerArm->getAnimationState().currentAnimation != "Run") {
            playerArm->playAnimation("Run", true, speed / 5.0f);
        } else {
            playerArm->getAnimationState().playbackSpeed = speed / 5.0f;
        }
    } else if (punchTimer <= 0.2f) {
        playerModel->playAnimation("Idle", true, 1.0f);
        playerShadow->playAnimation("Idle", true, 1.0f);
        playerArm->playAnimation("Idle", true, 1.0f);
    }
    if ((rightStickX != 0.0f || rightStickY != 0.0f) && inputManager->getCursorLocked()) {
        float sensitivity = getEntityManager()->getRenderer()->getSettingsManager()->getSettings()->sensitivity;
        rotate(glm::vec3(0.0f, -rightStickX * sensitivity * 10.0f, -rightStickY * sensitivity * 10.0f));
    }
    rind::CharacterEntity::update(deltaTime);
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
    glm::vec3 localVelocity = glm::transpose(glm::mat3(camera->getWorldTransform())) * getVelocity();
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

    // grenade cooldown
    float timeSinceLastGrenade = std::chrono::duration<float>(std::chrono::steady_clock::now() - lastGrenadeTime).count();
    float cooldownRatio = std::min(timeSinceLastGrenade / grenadeCooldown, 1.0f);
    grenadeFullIconObject->setUVClip(glm::vec4(0.0f, 0.0f, cooldownRatio, 1.0f));
    if (cooldownRatio >= 0.99f) {
        grenadeKeybindHintObject->setTint(glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
    } else {
        grenadeKeybindHintObject->setTint(glm::vec4(0.25f, 0.25f, 0.25f, 0.75f));
    }

    // healthbar update
    if (healthbarObject->getUVClip().z != getHealth() / getMaxHealth()) {
        float dir = getHealth() - getMaxHealth() * healthbarObject->getUVClip().z;
        float changeAmount = deltaTime * 30.0f;
        if (std::abs(dir) < changeAmount) {
            healthbarObject->setUVClip(glm::vec4(0.0f, 0.0f, getHealth() / getMaxHealth(), 1.0f));
        } else {
            changeAmount *= (dir > 0.0f) ? 1.0f : -1.0f;
            healthbarObject->setUVClip(glm::vec4(0.0f, 0.0f, healthbarObject->getUVClip().z + changeAmount / getMaxHealth(), 1.0f));
        }
    }

    // keybind hint
    if (keybindHintDuration > 0.0f) {
        checkKeybindHint();
        float alpha = -(0.01f * std::pow(keybindHintDuration, 5) - (0.64f * keybindHintDuration));
        keybindHintObject->setTint(glm::vec4(1.0f, 1.0f, 1.0f, alpha));
        keybindHintTextObject->setTint(glm::vec4(1.0f, 1.0f, 1.0f, alpha));
        keybindHintDuration -= deltaTime;
    }

    // damage effect
    if (cameraShakeIntensity > 0.0f) {
        glm::vec3 randomCameraLoc = glm::vec3(dist(rng), dist(rng), dist(rng)) * cameraShakeIntensity * 0.05f;
        camHolder->setTransform(glm::translate(glm::mat4(1.0f), randomCameraLoc));
        float overlayAlpha = std::clamp(cameraShakeIntensity * 2.0f, std::min(1.0f - (getHealth() / getMaxHealth()), 0.8f), 0.8f);
        damageEffectObject->setTint(glm::vec4(1.0f, 1.0f, 1.0f, overlayAlpha));
        cameraShakeIntensity -= deltaTime;
    }
    if (heartbeatOffset > 0.0f) {
        lastHeartbeat += deltaTime;
        if (lastHeartbeat >= heartbeatOffset) {
            lastHeartbeat = 0.0f;
            audioManager->playSound("player_heartbeat", 0.3f, 0.1f);
        }
    }

    // heal effect
    if (healUIShowTime > 0.0f) {
        float alpha = -(0.5f * std::pow(healUIShowTime, 5) - (0.5f * healUIShowTime));
        healEffectObject->setTint(glm::vec4(healEffectColor, alpha));
        float amount = -(10.0f * std::pow(healUIShowTime, 5) - (10.0f * healUIShowTime));
        particleManager->burstParticles(
            getWorldPosition(),
            healEffectColor,
            glm::vec3(0.0f, 1.0f, 0.0f) * 2.0f,
            amount,
            1.5f,
            2.0f,
            0.2f
        );
        particleManager->burstParticles(
            getWorldPosition(),
            healEffectColor,
            glm::vec3(0.0f, 1.0f, 0.0f) * 2.0f,
            amount,
            1.0f,
            2.0f,
            0.4f
        );
        healUIShowTime -= deltaTime;
    } else {
        healEffectObject->setTint(glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    }

    // hitmarker effect
    if (showHitmarkerTime > 0.0f) {
        float alpha = -(std::pow(2.0f * showHitmarkerTime, 5) - (3.25f * showHitmarkerTime));
        hitmarkerObject->setTint(glm::vec4(hitmarkerColor, alpha));
        showHitmarkerTime -= deltaTime;
    } else {
        hitmarkerObject->setTint(glm::vec4(1.0f, 1.0f, 1.0f, 0.0f));
    }

    // heal zone check
    static thread_local std::vector<engine::Collider*> candidates;
    candidates.clear();
    getEntityManager()->getSpatialGrid().query(getCollider()->getWorldAABB(), candidates);
    bool foundHealZone = false;
    for (engine::Collider* collider : candidates) {
        if (collider->getType() == engine::Entity::EntityType::Trigger) {
            rind::TempTrigger* trigger = dynamic_cast<rind::TempTrigger*>(collider);
            if (trigger) {
                if (engine::Collider::aabbIntersects(getCollider()->getWorldAABB(), trigger->getWorldAABB())) {
                    inHealZone = true;
                    healEffectColor = trigger->getColor();
                    foundHealZone = true;
                    if (keybindHintDuration <= 0.0f) {
                        showKeybindHint(HintActions::Heal, "To heal, press");
                    }
                    break;
                }
            }
        }
    }
    if (!foundHealZone) {
        inHealZone = false;
    }

    // punch timer
    if (punchTimer > 0.0f) {
        punchTimer -= deltaTime;
        if (punchTimer <= 0.0f) {
            punchTimer = 0.0f;
            playerArm->setVisible(false);
        }
    }

    // gun trail effect
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
        glm::vec3 rayDir = -glm::normalize(glm::vec3(camera->getWorldTransform()[2]));
        if (trailFramesRemaining == maxTrailFrames) {
            volumetricManager->createVolumetric(
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 0.1f
                    ),
                    glm::vec3(0.2f, 0.2f, 0.2f)
                ),
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 0.65f
                    ),
                    glm::vec3(1.3f, 1.3f, 1.3f)
                ),
                glm::vec4(1.0f, 0.2f, 0.2f, 15.0f),
                0.1f
            );
            volumetricManager->createVolumetric(
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 0.25f
                    ),
                    glm::vec3(0.5f, 0.5f, 0.5f)
                ),
                glm::scale(
                    glm::translate(
                        glm::mat4(1.0f), currentGunEndPos + rayDir * 2.5f
                    ),
                    glm::vec3(5.0f, 5.0f, 5.0f)
                ),
                glm::vec4(0.1f, 0.1f, 0.1f, 0.6f),
                4.0f
            );
        }
        particleManager->spawnTrail(
            currentGunEndPos + rayDir * 0.1f,
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
                case GLFW_KEY_Q:
                    if ((std::chrono::steady_clock::now() - lastGrenadeTime) >= std::chrono::duration<float>(grenadeCooldown)) {
                        throwGrenade();
                        lastGrenadeTime = std::chrono::steady_clock::now();
                    }
                    break;
                case GLFW_KEY_F:
                    if ((std::chrono::steady_clock::now() - lastPunchTime) >= std::chrono::duration<float>(punchCooldown)) {
                        punch();
                        lastPunchTime = std::chrono::steady_clock::now();
                    }
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
                float sensitivity = renderer->getSettingsManager()->getSettings()->sensitivity;
                float xOffset = static_cast<float>(event.mouseMoveEvent.xPos) * sensitivity;
                float yOffset = static_cast<float>(event.mouseMoveEvent.yPos) * sensitivity;
                rotate(glm::vec3(0.0f, -xOffset, -yOffset));
            }
        } else if (event.type == engine::InputEvent::Type::MouseButtonPress && !renderer->isPaused()) {
            if (event.mouseButtonEvent.button == GLFW_MOUSE_BUTTON_LEFT) {
                if ((std::chrono::steady_clock::now() - lastShotTime) >= std::chrono::duration<float>(shootingCooldown)) {
                    shoot();
                    lastShotTime = std::chrono::steady_clock::now();
                }
            } else if (event.mouseButtonEvent.button == GLFW_MOUSE_BUTTON_RIGHT
             && (std::chrono::steady_clock::now() - lastShotTime) >= std::chrono::duration<float>(shootingCooldown)
             && inHealZone
            ) {
                damage(-10.0f); // heals from enemy explosions
                lastShotTime = std::chrono::steady_clock::now();
                break;
            }
        } else if (event.type == engine::InputEvent::Type::GamepadButtonPress) {
            if (renderer->isPaused() && event.gamepadButtonEvent.button != GLFW_GAMEPAD_BUTTON_START) {
                inputManager->resetKeyStates();
                continue;
            }
            switch (event.gamepadButtonEvent.button) {
                case GLFW_GAMEPAD_BUTTON_START:
                    renderer->getSettingsManager()->hideSettingsUI();
                    renderer->isPaused() ? hidePauseMenu() : showPauseMenu();
                    break;
                case GLFW_GAMEPAD_BUTTON_A:
                    if (isGrounded()) {
                        jump(8.0f);
                    } else if (canDoubleJump) {
                        canDash = true;
                        move(glm::vec3(0.0f, 3.0f, 0.0f));
                        canDoubleJump = false;
                        resetDoubleJump = true;
                    }
                    break;
                case GLFW_GAMEPAD_BUTTON_B:
                    if ((std::chrono::steady_clock::now() - lastShotTime) >= std::chrono::duration<float>(shootingCooldown)
                     && inHealZone
                    ) {
                        damage(-10.0f); // heals from enemy explosions
                        lastShotTime = std::chrono::steady_clock::now();
                    }
                    break;
                case GLFW_GAMEPAD_BUTTON_LEFT_BUMPER:
                    if ((std::chrono::steady_clock::now() - lastGrenadeTime) >= std::chrono::duration<float>(grenadeCooldown)) {
                        throwGrenade();
                        lastGrenadeTime = std::chrono::steady_clock::now();
                    }
                    break;
                case GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER:
                    if ((std::chrono::steady_clock::now() - lastPunchTime) >= std::chrono::duration<float>(punchCooldown)) {
                        punch();
                        lastPunchTime = std::chrono::steady_clock::now();
                    }
                    break;
                default:
                    break;
            }
        } else if (event.type == engine::InputEvent::Type::GamepadAxisPress) {
            switch (event.gamepadAxisEvent.axis) {
                case GLFW_GAMEPAD_AXIS_LEFT_X: {
                    float dir = event.gamepadAxisEvent.value > 0.0f ? 1.0f : -1.0f;
                    move({0.0f, 0.0f, dir});
                    break;
                }
                case GLFW_GAMEPAD_AXIS_LEFT_Y: {
                    float dir = event.gamepadAxisEvent.value > 0.0f ? -1.0f : 1.0f;
                    move({dir, 0.0f, 0.0f});
                    break;
                }
                default:
                    break;
            }
        } else if (event.type == engine::InputEvent::Type::GamepadAxisRelease) {
            switch (event.gamepadAxisEvent.axis) {
                case GLFW_GAMEPAD_AXIS_LEFT_X: {
                    float dir = event.gamepadAxisEvent.value > 0.0f ? 1.0f : -1.0f;
                    stopMove({0.0f, 0.0f, dir});
                    break;
                }
                case GLFW_GAMEPAD_AXIS_LEFT_Y: {
                    float dir = event.gamepadAxisEvent.value > 0.0f ? -1.0f : 1.0f;
                    stopMove({dir, 0.0f, 0.0f});
                    break;
                }
                case GLFW_GAMEPAD_AXIS_RIGHT_X:
                    rightStickX = 0.0f;
                    break;
                case GLFW_GAMEPAD_AXIS_RIGHT_Y:
                    rightStickY = 0.0f;
                    break;
                default:
                    break;
            }
        } else if (event.type == engine::InputEvent::Type::GamepadAxisMove) {
            switch (event.gamepadAxisEvent.axis) {
                case GLFW_GAMEPAD_AXIS_RIGHT_X:
                    rightStickX = event.gamepadAxisEvent.value;
                    break;
                case GLFW_GAMEPAD_AXIS_RIGHT_Y:
                    rightStickY = event.gamepadAxisEvent.value;
                    break;
                case GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER:
                    if (event.gamepadAxisEvent.value > 0.5f
                     && (std::chrono::steady_clock::now() - lastShotTime) >= std::chrono::duration<float>(shootingCooldown))
                    {
                        shoot();
                        lastShotTime = std::chrono::steady_clock::now();
                    }
                    break;
                case GLFW_GAMEPAD_AXIS_LEFT_TRIGGER:
                    if (event.gamepadAxisEvent.value > 0.5f) {
                        canDash = true;
                    }
                    break;
                default:
                    break;
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
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                trailColor,
                -glm::normalize(getVelocity()) * 15.0f,
                50,
                2.0f,
                2.0f,
                1.2f
            );
            particleManager->burstParticles(
                getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
                trailColor,
                -glm::normalize(getVelocity()) * 10.0f,
                50,
                2.0f,
                2.0f,
                0.5f
            );
            audioManager->playSound3D("player_dash", getWorldPosition(), 0.5f, 0.15f);
            lastDashTime = now;
        }
    }
    if (currentPress.y > 0.0f) {
        stopMove(glm::vec3(0.0f, currentPress.y, 0.0f));
    }
    canDash = false;
}

void rind::Player::damage(float amount) {
    if (isDead) {
        return;
    }
    bool earlyReturn = false;
    if (amount < 0.0f) {
        if (getHealth() >= getMaxHealth()) {
            return;
        }
        else {
            audioManager->playSound("player_heal", 0.4f, 0.4f);
            healUIShowTime = 1.0f;
            showHitmarker(glm::clamp(healEffectColor + glm::vec3(0.3f), glm::vec3(0.0f), glm::vec3(1.0f)));
            earlyReturn = true;
        }
    }
    setHealth(std::min(getHealth() - amount, getMaxHealth()));
    if (getHealth() <= 0.5f * getMaxHealth()) {
        heartbeatOffset = 0.3f + getHealth() / getMaxHealth();
    } else {
        heartbeatOffset = 0.0f;
    }
    if (earlyReturn) {
        return;
    }
    cameraShakeIntensity = dist(rng) * 0.5f + 1.2f;
    if (getHealth() <= 0.0f && !isDead) {
        heartbeatOffset = 0.0f;
        isDead = true;
        stopMove(getPressed(), false);
        audioManager->playSound("player_death", 0.5f, 0.2f);
        engine::UIManager* uiManager = getEntityManager()->getRenderer()->getUIManager();
        if (getEntityManager()->getRenderer()->isPaused()) {
            hidePauseMenu();
        }
        engine::UIObject* windowTint = new engine::UIObject(
            uiManager,
            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(3.0f, 3.0f, 1.0f)), glm::vec3(0.0f, 0.0f, -1.0f)), 
            "deathWindowTint",
            glm::vec4(0.5f, 0.0f, 0.0f, 0.8f),
            "ui_window"
        );
        engine::TextObject* diedText = new engine::TextObject(
            uiManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 100.0f, -2.0f)),
            "deathWindowText",
            glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
            "You Died",
            "ColdNightForAlligators"
        );
        engine::SceneManager* sceneManager = getEntityManager()->getRenderer()->getSceneManager();
        engine::ButtonObject* quitButton = new engine::ButtonObject(
            uiManager,
            glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -300.0f, -2.0f)), glm::vec3(0.15, 0.05, 1.0)),
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
            getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
            trailColor,
            glm::vec3(0.0f, 1.0f, 0.0f) * 2.0f,
            200,
            5.0f,
            2.0f,
            0.3f
        );
        particleManager->burstParticles(
            getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
            trailColor,
            glm::vec3(0.0f, 1.0f, 0.0f) * 2.0f,
            200,
            5.0f,
            2.0f,
            0.6f
        );
        particleManager->burstParticles(
            getWorldPosition() + glm::vec3(0.0f, 0.5f, 0.0f),
            trailColor,
            glm::vec3(0.0f, 1.0f, 0.0f) * 2.0f,
            200,
            8.0f,
            2.0f,
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
        gunPos,
        trailColor,
        rayDir * 10.0f,
        20,
        1.5f,
        0.35f,
        0.8f
    );
    particleManager->burstParticles(
        gunPos,
        trailColor,
        rayDir * 15.0f,
        60,
        2.0f,
        0.35f,
        0.3f
    );
    engine::Collider::Collision hit = engine::Collider::raycastFirst(
        getEntityManager(),
        camera->getWorldPosition(),
        rayDir,
        1000.0f,
        this->getCollider()
    );
    glm::vec3 endPos = gunPos + rayDir * 1000.0f;
    if (hit.other) {
        endPos = hit.worldHitPoint;
        glm::vec3 normal = glm::normalize(hit.mtv.normal);
        glm::vec3 reflectedDir = glm::reflect(rayDir, normal);
        particleManager->burstParticles(
            hit.worldHitPoint,
            trailColor,
            reflectedDir * 40.0f,
            50,
            4.0f,
            0.5f,
            0.9f
        );
        particleManager->burstParticles(
            hit.worldHitPoint,
            trailColor,
            reflectedDir * 25.0f,
            100,
            4.0f,
            0.4f,
            0.4f
        );
        particleManager->burstParticles(
            hit.worldHitPoint,
            trailColor,
            reflectedDir * 10.0f,
            50,
            2.0f,
            0.3f,
            0.7f
        );
        particleManager->burstParticles(
            hit.worldHitPoint,
            trailColor,
            reflectedDir * 30.0f,
            40,
            3.0f,
            0.35f,
            1.1f
        );
        engine::Entity* other = hit.other->getParent();
        if (other->getType() == engine::Entity::EntityType::Enemy) {
            rind::Enemy* character = static_cast<rind::Enemy*>(other);
            const float damageAmount = 34.0f;
            if (character->getHealth() - damageAmount <= 0.0f) {
                showHitmarker(glm::vec3(1.0f, 0.2f, 0.2f));
                audioManager->playSound("hitmarker_death", 0.6f, 0.25f);
            } else {
                showHitmarker(glm::vec3(1.0f, 1.0f, 1.0f));
                audioManager->playSound("hitmarker", 0.5f, 0.2f);
            }
            character->damage(damageAmount);
            if (character->getState() == EnemyState::Idle) {
                character->rotateToPlayer();
                if (!character->checkVisibilityOfPlayer()) {
                    character->setWanderTarget(getWorldPosition());
                    character->wanderTo(getEntityManager()->getRenderer()->getDeltaTime());
                    audioManager->playSound3D("enemy_track", character->getWorldPosition(), 0.5f, 0.2f);
                }
            }
            audioManager->playSound3D("laser_enemy_impact", hit.worldHitPoint, 0.5f, 0.2f);
        } else {
            audioManager->playSound3D("laser_ground_impact", hit.worldHitPoint, 0.5f, 0.2f);
        }
    }
    audioManager->playSound3D("laser_shot", gunPos, 0.5f, 0.2f);
    trailFramesRemaining = maxTrailFrames;
    trailEndPos = endPos;
}

void rind::Player::throwGrenade() {
    glm::vec3 forward = -glm::normalize(glm::vec3(camera->getWorldTransform()[2]));
    glm::vec3 gunPos = glm::vec3(gunEndPosition->getWorldTransform()[3]);
    Grenade* grenade = new Grenade(
        getEntityManager(),
        this,
        glm::translate(glm::mat4(1.0f), gunPos + forward * 0.5f),
        forward * 20.0f + glm::vec3(0.0f, 3.0f, 0.0f),
        trailColor,
        6.0f
    );
    audioManager->playSound3D("player_throw", gunPos, 0.5f, 0.2f);
}

void rind::Player::punch() {
    static thread_local std::vector<engine::Collider*> candidates;
    candidates.clear();
    glm::vec3 forward = -glm::normalize(glm::vec3(camera->getWorldTransform()[2]));
    glm::vec3 rayOrigin = getWorldPosition() + glm::vec3(0.0f, 1.0f, 0.0f);
    engine::Collider::Collision hit = engine::Collider::raycastFirst(
        getEntityManager(),
        rayOrigin,
        forward,
        9.0f,
        getCollider(),
        0.1f
    );
    playerShadow->playAnimation("Punch", true, 1.0f);
    playerArm->setVisible(true);
    playerArm->playAnimation("Punch", true, 1.0f);
    punchTimer = 0.5f;
    audioManager->playSound("punch", 0.5f, 0.2f);
    if (hit.other) {
        engine::Entity* other = hit.other->getParent();
        if (other->getType() == engine::Entity::EntityType::Enemy) {
            rind::Enemy* character = static_cast<rind::Enemy*>(other);
            const float damageAmount = 50.0f;
            if (character->getHealth() - damageAmount <= 0.0f) {
                showHitmarker(glm::vec3(1.0f, 0.2f, 0.2f));
                audioManager->playSound("hitmarker_death", 0.6f, 0.25f);
            } else {
                showHitmarker(glm::vec3(1.0f, 1.0f, 1.0f));
                audioManager->playSound("hitmarker", 0.5f, 0.2f);
                if (character->getState() == EnemyState::Idle) {
                    character->rotateToPlayer();
                    if (!character->checkVisibilityOfPlayer()) {
                        character->setWanderTarget(getWorldPosition());
                        character->wanderTo(getEntityManager()->getRenderer()->getDeltaTime());
                        audioManager->playSound3D("enemy_track", character->getWorldPosition(), 0.5f, 0.2f);
                    }
                }
            }
            character->damage(damageAmount);
            audioManager->playSound("punch_hit", 0.75f, 0.1f);
        }
    }
}