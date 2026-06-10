#pragma once

#include <memory>
#include <cstdint>

namespace engine {
    class Renderer;
    class SceneManager;
    class ShaderManager;
    class TextureManager;
    class EntityManager;
    class InputManager;
    class UIManager;
    class ModelManager;
    class ParticleManager;
    class VolumetricManager;
    class LightManager;
    class IrradianceManager;
    class AudioManager;
    class SettingsManager;
}

namespace rind {
    class GameInstance {
    public:
        GameInstance();
        ~GameInstance();
        void run();

        uint32_t getDifficultyLevel() const { return difficulty; }

    private:
        std::unique_ptr<engine::Renderer> renderer;
        std::unique_ptr<engine::SceneManager> sceneManager;
        std::unique_ptr<engine::ShaderManager> shaderManager;
        std::unique_ptr<engine::TextureManager> textureManager;
        std::unique_ptr<engine::EntityManager> entityManager;
        std::unique_ptr<engine::InputManager> inputManager;
        std::unique_ptr<engine::UIManager> uiManager;
        std::unique_ptr<engine::ModelManager> modelManager;
        std::unique_ptr<engine::ParticleManager> particleManager;
        std::unique_ptr<engine::VolumetricManager> volumetricManager;
        std::unique_ptr<engine::LightManager> lightManager;
        std::unique_ptr<engine::IrradianceManager> irradianceManager;
        std::unique_ptr<engine::AudioManager> audioManager;
        std::unique_ptr<engine::SettingsManager> settingsManager;

        uint32_t difficulty = 1;
    };
};