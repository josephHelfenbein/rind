#pragma once

#include <string>
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <engine/Renderer.h>
#include <engine/UIManager.h>
#include <engine/InputManager.h>
#include <engine/EntityManager.h>

namespace engine {
    class SettingsManager {
    public:
        struct Settings {
            uint32_t aoMode = 2; // 0 = disabled, 1 = ssao, 2 = gtao
            float fpsLimit = 0.0f;
            float shadowQuality = 2.0f; // 0=512 4 samples, 1=1024 8 samples, 2=2048 16 samples, 3=2048 32 samples
            float masterVolume = 1.0f;
            bool fxaaEnabled = true;
            bool ssrEnabled = true;
            bool showFPS = false;
        };

        SettingsManager(engine::Renderer* renderer) : renderer(renderer) {
            renderer->registerSettingsManager(this);
            loadSettings();
        };

        ~SettingsManager() {
            delete currentSettings;
        }

        void loadSettings() {
            currentSettings = new Settings();
            std::filesystem::path configPath = getConfigFilePath();
            if (!std::filesystem::exists(configPath)) {
                saveSettings();
                return;
            }
            std::vector<char> buffer = readFile(configPath.string());
            std::string content(buffer.begin(), buffer.end());

            currentSettings->masterVolume = std::clamp(parseFloat(content, "masterVolume", 1.0f), 0.0f, 1.0f);
            currentSettings->aoMode = std::clamp(static_cast<uint32_t>(parseInt(content, "aoMode", 2)), 0u, 2u);
            currentSettings->fxaaEnabled = parseBool(content, "fxaaEnabled", true);
            currentSettings->ssrEnabled = parseBool(content, "ssrEnabled", true);
            currentSettings->showFPS = parseBool(content, "showFPS", false);
            currentSettings->fpsLimit = parseFloat(content, "fpsLimit", 0.0f);
            currentSettings->shadowQuality = static_cast<float>(static_cast<int>(std::clamp(
                parseFloat(content, "shadowQuality", 2.0f),
                0.0f,
                3.0f
            ) + 0.5f));
            tempSettings = new Settings(*currentSettings);
        }

        void saveSettings() {
            std::filesystem::path configPath = getConfigFilePath();
            std::filesystem::path configDir = configPath.parent_path();
            if (!std::filesystem::exists(configDir)) {
                std::filesystem::create_directories(configDir);
            }

            std::ofstream file(configPath);
            if (!file.is_open()) {
                return;
            }

            file << "{\n";
            file << "    \"masterVolume\": " << currentSettings->masterVolume << ",\n";
            file << "    \"aoMode\": " << currentSettings->aoMode << ",\n";
            file << "    \"fxaaEnabled\": " << (currentSettings->fxaaEnabled ? "true" : "false") << ",\n";
            file << "    \"ssrEnabled\": " << (currentSettings->ssrEnabled ? "true" : "false") << ",\n";
            file << "    \"showFPS\": " << (currentSettings->showFPS ? "true" : "false") << ",\n";
            file << "    \"fpsLimit\": " << currentSettings->fpsLimit << ",\n";
            file << "    \"shadowQuality\": " << currentSettings->shadowQuality << "\n";
            file << "}\n";

            file.close();
        }

        Settings* getSettings() { return currentSettings; }

        void showSettingsUI() {
            if (settingsUIObject) return;
            UIManager* uiManager = renderer->getUIManager();
            delete tempSettings;
            tempSettings = new Settings(*currentSettings);

            settingsUIObject = new UIObject(
                uiManager,
                glm::scale(glm::mat4(1.0f), glm::vec3(0.6f, 0.5f, 1.0f)),
                "settingsUI",
                glm::vec4(0.3f, 0.3f, 0.3f, 1.0f),
                "ui_window",
                Corner::Center
            );
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.12f, 0.12f, 1.0f)), glm::vec3(0.0f, -200.0f, 0.0f)),
                "settingsTitle",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Settings",
                "Lato",
                Corner::Top
            ));
            settingsUIObject->addChild(new ButtonObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.03f, 1.0f)), glm::vec3(-300.0f, -750.0f, 0.0f)),
                "closeSettingsButton",
                glm::vec4(0.8f, 0.2f, 0.2f, 1.0f),
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 
                "ui_window",
                "Close",
                "Lato",
                [this]() {
                    this->hideSettingsUI();
                    if (this->onCloseCallback) {
                        this->onCloseCallback();
                    }
                },
                Corner::TopRight
            ));
            // SSR
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(450.0f, -1300.0f, 0.0f)),
                "ssrLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Enable Screen Space Reflections:",
                "Lato",
                Corner::TopLeft
            ));
            settingsUIObject->addChild(new CheckboxObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(-350.0f, -1000.0f, 0.0f)),
                "ssrCheckbox",
                glm::vec4(1.0f),
                tempSettings->ssrEnabled,
                tempSettings->ssrEnabled,
                Corner::TopRight
            ));
            // FXAA
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(450.0f, -1800.0f, 0.0f)),
                "fxaaLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Enable FXAA:",
                "Lato",
                Corner::TopLeft
            ));
            settingsUIObject->addChild(new CheckboxObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(-350.0f, -1350.0f, 0.0f)),
                "fxaaCheckbox",
                glm::vec4(1.0f),
                tempSettings->fxaaEnabled,
                tempSettings->fxaaEnabled,
                Corner::TopRight
            ));
            // Ambient Occlusion
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(450.0f, -2350.0f, 0.0f)),
                "aoLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Ambient Occlusion",
                "Lato",
                Corner::TopLeft
            ));
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(-250.0f, -2200.0f, 0.0f)),
                "aoOptionsLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Disabled   SSAO   GTAO",
                "Lato",
                Corner::TopRight
            ));
            aoDisabled = (tempSettings->aoMode == 0);
            CheckboxObject* aoDisabledCheckbox = new CheckboxObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(-1350.0f, -1900.0f, 0.0f)),
                "aoDisabledCheckbox",
                glm::vec4(1.0f),
                aoDisabled,
                aoDisabled,
                Corner::TopRight
            );
            aoSSAO = (tempSettings->aoMode == 1);
            CheckboxObject* aoSSAOCheckbox = new CheckboxObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(-850.0f, -1900.0f, 0.0f)),
                "aoSSAOCheckbox",
                glm::vec4(1.0f),
                aoSSAO,
                aoSSAO,
                Corner::TopRight
            );
            aoGTAO = (tempSettings->aoMode == 2);
            CheckboxObject* aoGTAOCheckbox = new CheckboxObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(-350.0f, -1900.0f, 0.0f)),
                "aoGTAOCheckbox",
                glm::vec4(1.0f),
                aoGTAO,
                aoGTAO,
                Corner::TopRight
            );
            settingsUIObject->addChild(aoDisabledCheckbox);
            settingsUIObject->addChild(aoSSAOCheckbox);
            settingsUIObject->addChild(aoGTAOCheckbox);
            aoDisabledCheckbox->setBoundBools({ aoSSAOCheckbox, aoGTAOCheckbox });
            aoSSAOCheckbox->setBoundBools({ aoDisabledCheckbox, aoGTAOCheckbox });
            aoGTAOCheckbox->setBoundBools({ aoDisabledCheckbox, aoSSAOCheckbox });
            // Show FPS
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(450.0f, -3000.0f, 0.0f)),
                "showFPSLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Show FPS:",
                "Lato",
                Corner::TopLeft
            ));
            settingsUIObject->addChild(new CheckboxObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 1.0f)), glm::vec3(-340.0f, -2300.0f, 0.0f)),
                "showFPSCheckbox",
                glm::vec4(1.0f),
                tempSettings->showFPS,
                tempSettings->showFPS,
                Corner::TopRight
            ));
            // Master Volume
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(450.0f, -3600.0f, 0.0f)),
                "masterVolumeLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Master Volume:",
                "Lato",
                Corner::TopLeft
            ));
            settingsUIObject->addChild(new SliderObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 0.14f, 1.0f)), glm::vec3(-100.0f, -1900.0f, 0.0f)),
                "masterVolumeSlider",
                0.0f,
                1.0f,
                tempSettings->masterVolume,
                Corner::TopRight,
                "%",
                true,
                100.0f
            ));
            // FPS Limit
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(450.0f, -4200.0f, 0.0f)),
                "fpsLimitLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "FPS Limit (0 = VSync):",
                "Lato",
                Corner::TopLeft
            ));
            settingsUIObject->addChild(new SliderObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 0.14f, 1.0f)), glm::vec3(-100.0f, -2200.0f, 0.0f)),
                "fpsLimitSlider",
                0.0f,
                240.0f,
                tempSettings->fpsLimit,
                Corner::TopRight,
                " FPS",
                true,
                1.0f
            ));
            // Shadow Quality
            settingsUIObject->addChild(new TextObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f, 0.075f, 1.0f)), glm::vec3(450.0f, -4800.0f, 0.0f)),
                "shadowQualityLabel",
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "Shadow Quality:",
                "Lato",
                Corner::TopLeft
            ));
            settingsUIObject->addChild(new SliderObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 0.14f, 1.0f)), glm::vec3(-100.0f, -2500.0f, 0.0f)),
                "shadowQualitySlider",
                0.0f,
                3.0f,
                tempSettings->shadowQuality,
                Corner::TopRight,
                "",
                true,
                1.0f
            ));
            // Apply Button
            settingsUIObject->addChild(new ButtonObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.09f, 0.0375f, 1.0f)), glm::vec3(0.0f, 600.0f, 0.0f)),
                "applySettingsButton",
                glm::vec4(0.2f, 0.5f, 0.2f, 1.0f),
                glm::vec4(1.0f, 1.0f, 1.0f, 1.0f),
                "ui_window",
                "Apply",
                "Lato",
                [this]() {
                    if (aoDisabled) {
                        this->tempSettings->aoMode = 0;
                    } else if (aoSSAO) {
                        this->tempSettings->aoMode = 1;
                    } else if (aoGTAO) {
                        this->tempSettings->aoMode = 2;
                    }
                    this->tempSettings->fpsLimit = float(static_cast<int>(this->tempSettings->fpsLimit + 0.5f));
                    this->tempSettings->shadowQuality = float(static_cast<int>(std::clamp(this->tempSettings->shadowQuality, 0.0f, 3.0f) + 0.5f));
                    float previousFPSLimit = this->currentSettings->fpsLimit;
                    float previousShadowQuality = this->currentSettings->shadowQuality;
                    *(this->currentSettings) = *(this->tempSettings);
                    if (previousFPSLimit < 1e-6f && this->tempSettings->fpsLimit > 1e-6f) {
                        this->renderer->recreateSwapChain();
                    } else if (previousFPSLimit > 1e-6f && this->tempSettings->fpsLimit < 1e-6f) {
                        this->renderer->recreateSwapChain();
                    }
                    if (previousShadowQuality != this->tempSettings->shadowQuality) {
                        this->renderer->requestShadowMapRecreation();
                    }
                    this->saveSettings();
                },
                Corner::Bottom
            ));
            renderer->refreshDescriptorSets();
        }

        void hideSettingsUI() {
            if (!settingsUIObject) return;
            renderer->setHoveredObject(nullptr);
            renderer->getUIManager()->removeObject(settingsUIObject->getName());
            settingsUIObject = nullptr;
            renderer->refreshDescriptorSets();
        }

        void setUIOnClose(std::function<void()> callback) {
            onCloseCallback = callback;
        }

    private:
        Settings* currentSettings;
        Settings* tempSettings;
        Renderer* renderer;
        UIObject* settingsUIObject = nullptr;

        std::function<void()> onCloseCallback;

        bool aoDisabled;
        bool aoSSAO;
        bool aoGTAO;

        static std::filesystem::path getConfigFilePath() {
            std::filesystem::path configDir;

#if defined(_WIN32)
            // Windows: %APPDATA%\rind\config.json
            const char* appdata = std::getenv("APPDATA");
            if (appdata) {
                configDir = std::filesystem::path(appdata) / "rind";
            } else {
                configDir = std::filesystem::path(".") / "rind";
            }
#elif defined(__APPLE__)
            // macOS: ~/Library/Application Support/rind/config.json
            const char* home = std::getenv("HOME");
            if (home) {
                configDir = std::filesystem::path(home) / "Library" / "Application Support" / "rind";
            } else {
                configDir = std::filesystem::path(".") / "rind";
            }
#else
            // Linux/Unix: ~/.config/rind/config.json
            const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
            if (xdgConfig) {
                configDir = std::filesystem::path(xdgConfig) / "rind";
            } else {
                const char* home = std::getenv("HOME");
                if (home) {
                    configDir = std::filesystem::path(home) / ".config" / "rind";
                } else {
                    configDir = std::filesystem::path(".") / "rind";
                }
            }
#endif
            return configDir / "config.json";
        }

        static float parseFloat(const std::string& json, const std::string& key, float defaultValue) {
            std::string searchKey = "\"" + key + "\"";
            size_t pos = json.find(searchKey);
            if (pos == std::string::npos) return defaultValue;

            pos = json.find(':', pos);
            if (pos == std::string::npos) return defaultValue;

            size_t start = json.find_first_not_of(" \t\n\r", pos + 1);
            if (start == std::string::npos) return defaultValue;

            size_t end = json.find_first_of(",}\n", start);
            std::string value = json.substr(start, end - start);

            size_t numEnd = value.find_first_not_of("0123456789+-.eE");
            if (numEnd != std::string::npos) {
                value = value.substr(0, numEnd);
            }

            try {
                return std::stof(value);
            } catch (...) {
                return defaultValue;
            }
        }
        static int parseInt(const std::string& json, const std::string& key, int defaultValue) {
            std::string searchKey = "\"" + key + "\"";
            size_t pos = json.find(searchKey);
            if (pos == std::string::npos) return defaultValue;

            pos = json.find(':', pos);
            if (pos == std::string::npos) return defaultValue;

            size_t start = json.find_first_not_of(" \t\n\r", pos + 1);
            if (start == std::string::npos) return defaultValue;

            size_t end = json.find_first_of(",}\n", start);
            std::string value = json.substr(start, end - start);
            
            try {
                return std::stoi(value);
            } catch (...) {
                return defaultValue;
            }
        }
        static bool parseBool(const std::string& json, const std::string& key, bool defaultValue) {
            std::string searchKey = "\"" + key + "\"";
            size_t pos = json.find(searchKey);
            if (pos == std::string::npos) return defaultValue;

            pos = json.find(':', pos);
            if (pos == std::string::npos) return defaultValue;

            size_t start = json.find_first_not_of(" \t\n\r", pos + 1);
            if (start == std::string::npos) return defaultValue;

            if (json.compare(start, 4, "true") == 0) return true;
            if (json.compare(start, 5, "false") == 0) return false;
            return defaultValue;
        }
    };
}
