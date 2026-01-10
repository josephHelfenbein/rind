#define MINIAUDIO_IMPLEMENTATION
#include <engine/AudioManager.h>
#include <engine/io.h>
#include <iostream>
#include <filesystem>

engine::AudioManager::AudioManager(Renderer* renderer, std::string audioDirectory) : renderer(renderer), audioDirectory(std::move(audioDirectory)) {
    renderer->registerAudioManager(this);
    ma_result result = ma_engine_init(NULL, &m_engine);
    if (result != MA_SUCCESS) {
        throw std::runtime_error("Failed to initialize audio engine");
    }
    m_initialized = true;
    std::vector<std::string> audioFiles = scanDirectory(this->audioDirectory);
    for (const auto& filePath : audioFiles) {
        if (!std::filesystem::is_regular_file(filePath)) {
            continue;
        }
        std::filesystem::path p(filePath);
        std::string baseName = p.stem().string(); // strip trailing extension
        if (m_soundPaths.find(baseName) != m_soundPaths.end()) {
            std::cout << "Warning: Duplicate audio file name detected: " << baseName << ". Skipping " << filePath << "\n";
            continue;
        }
        m_soundPaths[baseName] = filePath;
    }
    for (const auto& [name, filePath] : m_soundPaths) {
        if (!loadSound(name, filePath)) {
            std::cerr << "Failed to load sound: " << filePath << std::endl;
        }
    }
}

engine::AudioManager::~AudioManager() {
    cleanup();
}

void engine::AudioManager::update() {
    if (!m_initialized) return;
    std::erase_if(m_oneShots, [](const auto& soundData) {
        if (!soundData->isLoaded) return true;
        if (!ma_sound_is_playing(&soundData->sound) && ma_sound_at_end(&soundData->sound)) {
            ma_sound_uninit(&soundData->sound);
            return true;
        }
        return false;
    });
    if (!settings) {
        settings = renderer->getSettingsManager()->getSettings();
        return;
    }
    if (settings->masterVolume != ma_engine_get_volume(&m_engine)) {
        setGlobalVolume(settings->masterVolume);
    }
}

void engine::AudioManager::cleanup() {
    if (m_initialized) {
        for (auto& [name, data] : m_sounds) {
            if (data && data->isLoaded) {
                ma_sound_uninit(&data->sound);
            }
        }
        m_sounds.clear();
        for (auto& data : m_oneShots) {
            if (data && data->isLoaded) {
                ma_sound_uninit(&data->sound);
            }
        }
        m_oneShots.clear();

        ma_engine_uninit(&m_engine);
        m_initialized = false;
    }
}

void engine::AudioManager::updateListener(const glm::vec3& position, const glm::vec3& forward, const glm::vec3& up) {
    if (!m_initialized) return;
    ma_engine_listener_set_position(&m_engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_engine, 0, up.x, up.y, up.z);
}

bool engine::AudioManager::loadSound(const std::string& name, const std::string& filePath) {
    if (!m_initialized) return false;
    auto data = std::make_unique<SoundData>();
    ma_result result = ma_sound_init_from_file(&m_engine, filePath.c_str(), 0, NULL, NULL, &data->sound);
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to load sound: " << filePath << std::endl;
        return false;
    }
    data->isLoaded = true;
    m_sounds[name] = std::move(data);
    return true;
}

void engine::AudioManager::playSound3D(const std::string& name, const glm::vec3& position, float volume, bool varyPitch) {
    if (!m_initialized) return;
    auto it = m_soundPaths.find(name);
    if (it == m_soundPaths.end()) {
        std::cerr << "3D Sound not found: " << name << std::endl;
        return;
    }
    auto data = std::make_unique<SoundData>();
    ma_result result = ma_sound_init_from_file(&m_engine, it->second.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, NULL, NULL, &data->sound);
    
    if (result != MA_SUCCESS) {
        std::cerr << "Failed to init 3D sound: " << name << std::endl;
        return;
    }

    data->isLoaded = true;
    ma_sound_set_position(&data->sound, position.x, position.y, position.z);
    ma_sound_set_volume(&data->sound, volume);
    ma_sound_set_min_distance(&data->sound, 5.0f);
    ma_sound_set_rolloff(&data->sound, 0.5f);
    
    if (varyPitch) {
        float pitchVariation = dist(rng) * 0.1f;
        ma_sound_set_pitch(&data->sound, 1.0f + pitchVariation);
    }

    ma_sound_start(&data->sound);
    m_oneShots.push_back(std::move(data));
}

void engine::AudioManager::playSound(const std::string& name, float volume, bool varyPitch) {
    if (m_sounds.find(name) != m_sounds.end()) {
        ma_sound_set_volume(&m_sounds[name]->sound, volume);
        if (varyPitch) {
            float pitchVariation = dist(rng) * 0.1f;
            float baseFrequency = 1.0f;
            ma_sound_set_pitch(&m_sounds[name]->sound, baseFrequency + pitchVariation);
        } else {
            ma_sound_set_pitch(&m_sounds[name]->sound, 1.0f);
        }
        if (ma_sound_is_playing(&m_sounds[name]->sound)) {
            ma_sound_stop(&m_sounds[name]->sound);
            ma_sound_seek_to_pcm_frame(&m_sounds[name]->sound, 0);
        } else if (ma_sound_at_end(&m_sounds[name]->sound)) {
            ma_sound_seek_to_pcm_frame(&m_sounds[name]->sound, 0);
        }
        ma_sound_start(&m_sounds[name]->sound);
    }
}

void engine::AudioManager::stopSound(const std::string& name) {
    if (m_sounds.find(name) != m_sounds.end()) {
        ma_sound_stop(&m_sounds[name]->sound);
        ma_sound_seek_to_pcm_frame(&m_sounds[name]->sound, 0);
    }
}

void engine::AudioManager::setGlobalVolume(float volume) {
    if(m_initialized) {
        ma_engine_set_volume(&m_engine, volume);
    }
}