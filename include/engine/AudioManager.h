#pragma once

#include <engine/Renderer.h>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <miniaudio/miniaudio.h>
#include <random>
#include <glm/glm.hpp>

namespace engine {

    struct SoundData {
        ma_sound sound;
        bool isLoaded = false;
    };

    class AudioManager {
    public:
        AudioManager(Renderer* renderer, std::string audioDirectory);
        ~AudioManager();

        void update();
        void cleanup();

        void updateListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up);

        bool loadSound(const std::string& name, const std::string& filePath);
        
        void playSound(const std::string& name, float volume = 1.0f, bool varyPitch = false);
        void playSound3D(const std::string& name, const glm::vec3& position, float volume = 1.0f, bool varyPitch = false);
        
        void stopSound(const std::string& name);

        void setGlobalVolume(float volume);

    private:
        ma_engine m_engine;
        Renderer* renderer;
        std::string audioDirectory;
        std::map<std::string, std::string> m_soundPaths;
        std::map<std::string, std::unique_ptr<SoundData>> m_sounds;
        std::vector<std::unique_ptr<SoundData>> m_oneShots;
        bool m_initialized = false;

        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};
    };
}