#define MINIAUDIO_IMPLEMENTATION
#include <engine/AudioManager.h>
#include <engine/EmbeddedAssets.h>
#include <audio/audio_registry.h>
#include <iostream>

ma_result init_engine_with_channels(ma_engine* engine, ma_uint32 channels) {
    ma_engine_config config = ma_engine_config_init();
    config.channels = channels;
    return ma_engine_init(&config, engine);
}

engine::AudioManager::AudioManager(Renderer* renderer) : renderer(renderer) {
    renderer->registerAudioManager(this);
    ma_result result = MA_ERROR;
    const ma_uint32 channelFallbacks[] = {6, 2, 1};
    for (ma_uint32 channels : channelFallbacks) {
        result = init_engine_with_channels(&m_engine, channels);
        if (result == MA_SUCCESS) {
            break;
        }
    }
    if (result != MA_SUCCESS) {
        throw std::runtime_error("Failed to initialize audio engine");
    }
    m_initialized = true;

    const auto& embeddedAudio = getEmbedded_audio();
    for (const auto& [name, asset] : embeddedAudio) {
        ma_resource_manager* pResourceManager = ma_engine_get_resource_manager(&m_engine);
        result = ma_resource_manager_register_encoded_data(pResourceManager, name.c_str(), asset.data, asset.size);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to register embedded audio: " << name << std::endl;
            continue;
        }
        auto data = std::make_unique<SoundData>();
        result = ma_sound_init_from_file(&m_engine, name.c_str(), 0, NULL, NULL, &data->sound);
        if (result != MA_SUCCESS) {
            std::cerr << "Failed to load sound: " << name << std::endl;
            continue;
        }
        data->isLoaded = true;
        m_sounds[name] = std::move(data);
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
    std::erase_if(m_persistentSounds, [](const auto& soundData) {
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
    if (std::abs(settings->masterVolume - ma_engine_get_volume(&m_engine)) > 0.01f) {
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

        for (auto& data : m_persistentSounds) {
            if (data && data->isLoaded) {
                ma_sound_uninit(&data->sound);
            }
        }

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

void engine::AudioManager::playSound3D(const std::string& name, const glm::vec3& position, float volume, float pitchVariation) {
    if (!m_initialized) return;
    auto data = std::make_unique<SoundData>();
    ma_result result = ma_sound_init_from_file(&m_engine, name.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, NULL, NULL, &data->sound);

    if (result != MA_SUCCESS) {
        std::cerr << "Failed to init 3D sound: " << name << std::endl;
        return;
    }

    data->isLoaded = true;
    ma_sound_set_position(&data->sound, position.x, position.y, position.z);
    ma_sound_set_volume(&data->sound, volume);
    ma_sound_set_min_distance(&data->sound, 7.0f);
    ma_sound_set_rolloff(&data->sound, 0.3f);

    if (pitchVariation != 0.0f) {
        float vary = dist(rng) * pitchVariation;
        ma_sound_set_pitch(&data->sound, 1.0f + vary);
    }

    ma_sound_start(&data->sound);
    m_oneShots.push_back(std::move(data));
}

void engine::AudioManager::playSound(const std::string& name, float volume, float pitchVariation, bool persistent) {
    if (!m_initialized) return;
    auto data = std::make_unique<SoundData>();
    ma_result result = ma_sound_init_from_file(&m_engine, name.c_str(), MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_ASYNC, NULL, NULL, &data->sound);

    if (result != MA_SUCCESS) {
        std::cerr << "Failed to init sound: " << name << std::endl;
        return;
    }

    data->isLoaded = true;
    ma_sound_set_spatialization_enabled(&data->sound, MA_FALSE);
    ma_sound_set_volume(&data->sound, volume);

    if (pitchVariation != 0.0f) {
        float vary = dist(rng) * pitchVariation;
        ma_sound_set_pitch(&data->sound, 1.0f + vary);
    }

    ma_sound_start(&data->sound);
    if (persistent) {
        m_persistentSounds.push_back(std::move(data));
    } else {
        m_oneShots.push_back(std::move(data));
    }
}

void engine::AudioManager::stopSound(const std::string& name) {
    if (m_sounds.find(name) != m_sounds.end()) {
        ma_sound_stop(&m_sounds[name]->sound);
        ma_sound_seek_to_pcm_frame(&m_sounds[name]->sound, 0);
    }
}

void engine::AudioManager::stopAllSounds() {
    for (auto& [name, data] : m_sounds) {
        if (data && data->isLoaded) {
            ma_sound_stop(&data->sound);
            ma_sound_seek_to_pcm_frame(&data->sound, 0);
        }
    }
    for (auto& data : m_oneShots) {
        if (data && data->isLoaded) {
            ma_sound_stop(&data->sound);
            ma_sound_seek_to_pcm_frame(&data->sound, 0);
        }
    }
}

void engine::AudioManager::setGlobalVolume(float volume) {
    if(m_initialized) {
        float clamped = std::clamp(volume, 0.0f, 1.0f);
        ma_engine_set_volume(&m_engine, clamped);
    }
}
