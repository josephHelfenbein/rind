#pragma once

#include <rind/CharacterEntity.h>
#include <engine/InputManager.h>
#include <engine/Camera.h>
#include <rind/ScoreCounter.h>
#include <rind/StatusEffect.h>
#include <chrono>

namespace rind {
    class Player : public CharacterEntity {
    public:
        Player(
            engine::EntityManager* entityManager,
            engine::InputManager* inputManager,
            const std::string& name,
            const glm::mat4& transform
        );
        ~Player();

        void update(float deltaTime) override;

        void showPauseMenu(bool uiOnly = false);
        void hidePauseMenu(bool uiOnly = false);

        void registerInput(const std::vector<engine::InputEvent>& events);
        void shoot();
        void throwGrenade();
        void punch();
        void damage(float amount) override;

        enum class HintActions {
            Dash,
            Heal,
            Grenade,
            Jump,
            Punch
        };
        void showKeybindHint(const HintActions& action, const std::string& hint) {
            std::string texture;
            if (inputManager->isControllerMode()) {
                texture = actionTexturesGamepad[action];
            } else {
                texture = actionTexturesKeyboard[action];
            }
            activeKeybindHint = action;
            keybindHintObject->setTexture(texture);
            keybindHintTextObject->setText(hint);
            keybindHintDuration = 2.8f;
        }
        void checkKeybindHint() {
            std::string texture;
            if (inputManager->isControllerMode()) {
                texture = actionTexturesGamepad[activeKeybindHint];
            } else {
                texture = actionTexturesKeyboard[activeKeybindHint];
            }
            if (keybindHintObject->getTexture() != texture) {
                keybindHintObject->setTexture(texture);
            }
            std::string grenadeTexture;
            if (inputManager->isControllerMode()) {
                grenadeTexture = actionTexturesGamepad[HintActions::Grenade];
            } else {
                grenadeTexture = actionTexturesKeyboard[HintActions::Grenade];
            }
            if (grenadeKeybindHintObject->getTexture() != grenadeTexture) {
                grenadeKeybindHintObject->setTexture(grenadeTexture);
            }
        }

        void resizeHealthbar();
        void addScore(uint32_t score);

        void showHitmarker(const glm::vec3& color) {
            hitmarkerColor = color;
            showHitmarkerTime = 0.5f;
        }

        void setStatusEffect(const StatusEffect& status, bool isMain = false) {
            setJumpSpeed(status.jumpSpeed);
            setMoveSpeed(status.moveSpeed);
            setGravity(status.gravity);
            strengthMultiplier = status.strengthMultiplier;
            protectionMultiplier = status.protectionMultiplier;
            statusTextObject->setText(status.statusText);
            statusTextObject->setTint(glm::vec4(status.textColor, 1.0f));
            statusEffectOverlayObject->setTint(glm::vec4(status.overlayColor, 1.0f));
            currentStatusEffect = status;
            if (!isMain) {
                audioManager->playSound("status_effect", 0.5f, 0.2f);
                hasStatus = true;
                statusResetTime = status.resetTime;
            }
        }
        float getStatusRemaining() const {
            if (!hasStatus) return 0.0f;
            return statusResetTime;
        }
        bool statusEnabled() const {
            return hasStatus;
        }

    private:
        engine::Camera* camera = nullptr;
        engine::Entity* gunEndPosition = nullptr;
        float cameraShakeIntensity = 0.0f;

        const float gunModelScale = 0.16f;
        const glm::vec3 gunModelTranslation = glm::vec3(0.55856f, -0.273792f, -0.642208f);
        glm::quat gunModelInitialQuat = glm::angleAxis(glm::radians(-90.0f), glm::vec3(0,1,0));
        glm::quat gunModelOverheatedRot = 
            glm::angleAxis(glm::radians(-45.0f), glm::vec3(0,1,0)) *
            glm::angleAxis(glm::radians(-40.0f), glm::vec3(1,0,0));
        engine::Entity* gunModel = nullptr;
        engine::Entity* playerModel = nullptr;
        engine::Entity* camHolder = nullptr;
        engine::Entity* playerShadow = nullptr;
        engine::Entity* playerArm = nullptr;

        float punchTimer = 0.0f;
        const engine::AABB punchHitbox{
            .min = glm::vec3(-0.5f, -0.5f, -6.5f),
            .max = glm::vec3(0.5f, 0.5f, 0.0f)
        };

        bool canDoubleJump = false;
        bool resetDoubleJump = false;

        float heartbeatOffset = 0.0f;
        float lastHeartbeat = 0.0f;

        glm::vec3 currentGunRotOffset = glm::vec3(0.0f);
        glm::vec3 currentGunLocOffset = glm::vec3(0.0f);
        
        engine::InputManager* inputManager = nullptr;
        engine::ParticleManager* particleManager = nullptr;
        engine::AudioManager* audioManager = nullptr;
        engine::VolumetricManager* volumetricManager = nullptr;

        engine::UIObject* pauseUIObject = nullptr;
        engine::UIObject* healthbarObject = nullptr;
        engine::UIObject* healthbarEmptyObject = nullptr;
        engine::UIObject* damageEffectObject = nullptr;
        engine::UIObject* healEffectObject = nullptr;
        engine::UIObject* statusEffectOverlayObject = nullptr;
        engine::UIObject* grenadeEmptyIconObject = nullptr;
        engine::UIObject* grenadeFullIconObject = nullptr;
        engine::UIObject* grenadeKeybindHintObject = nullptr;
        engine::UIObject* hitmarkerObject = nullptr;
        ScoreCounter* scoreCounter = nullptr;

        std::unordered_map<HintActions, std::string> actionTexturesKeyboard = {
            {HintActions::Dash, "inputs_keyboard_common_shift"},
            {HintActions::Heal, "inputs_keyboard_mouse_right"},
            {HintActions::Grenade, "inputs_keyboard_common_q"},
            {HintActions::Jump, "inputs_keyboard_common_space"},
            {HintActions::Punch, "inputs_keyboard_common_f"}
        };
        std::unordered_map<HintActions, std::string> actionTexturesGamepad = {
            {HintActions::Dash, "inputs_gamepad_l2"},
            {HintActions::Heal, "inputs_gamepad_b"},
            {HintActions::Grenade, "inputs_gamepad_l1"},
            {HintActions::Jump, "inputs_gamepad_a"},
            {HintActions::Punch, "inputs_gamepad_r1"}
        };
        engine::UIObject* keybindHintObject = nullptr;
        engine::TextObject* keybindHintTextObject = nullptr;
        HintActions activeKeybindHint = HintActions::Dash;
        float keybindHintDuration = 0.0f;

        bool isDead = false;

        float statusResetTime = 0.0f;
        bool hasStatus = false;
        StatusEffect currentStatusEffect = mainStatusEffect;
        engine::TextObject* statusTextObject = nullptr;

        float healUIShowTime = 0.0f;
        bool inHealZone = false;
        glm::vec3 healEffectColor = glm::vec3(0.2f, 0.2f, 1.0f);

        float showHitmarkerTime = 0.0f;
        glm::vec3 hitmarkerColor = glm::vec3(1.0f, 1.0f, 1.0f);

        bool inputsDisconnected = false;
        
        float shootingCooldown = 0.1f;
        std::chrono::steady_clock::time_point lastShotTime = std::chrono::steady_clock::now();
        std::array<std::chrono::steady_clock::time_point, 10> shotTimes; // max 10 shots in 3 seconds for overheat
        const size_t maxShotTimes = 10;
        size_t shotTimesFront = 0;
        size_t shotTimesEnd = 0;
        float coolingTime = 0.0f;
        const float maxCoolingTime = 4.0f;
        float strengthMultiplier = 1.0f;
        float protectionMultiplier = 1.0f;

        float grenadeCooldown = 4.0f;
        std::chrono::steady_clock::time_point lastGrenadeTime = std::chrono::steady_clock::now();

        float punchCooldown = 0.5f;
        std::chrono::steady_clock::time_point lastPunchTime = std::chrono::steady_clock::now();
        
        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

        bool canDash = false;
        long long dashCooldown = 500; // ms
        std::chrono::steady_clock::time_point lastDashTime = std::chrono::steady_clock::now() - std::chrono::milliseconds(dashCooldown);

        uint32_t trailFramesRemaining = 0u;
        uint32_t maxTrailFrames = 5u;
        glm::vec3 trailEndPos = glm::vec3(0.0f);
        glm::vec3 trailColor = glm::vec3(1.0f, 0.0f, 0.0f);

        float rightStickX = 0.0f;
        float rightStickY = 0.0f;
    };
};
