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

namespace engine {
    class SettingsManager {
    public:
        struct Settings {
            float masterVolume = 1.0f;
            uint32_t aoMode = 2; // 0 = disabled, 1 = ssao, 2 = gtao
            bool fxaaEnabled = true;
            bool ssrEnabled = true;
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

            currentSettings->masterVolume = parseFloat(content, "masterVolume", 1.0f);
            currentSettings->aoMode = static_cast<uint32_t>(parseInt(content, "aoMode", 2));
            currentSettings->fxaaEnabled = parseBool(content, "fxaaEnabled", true);
            currentSettings->ssrEnabled = parseBool(content, "ssrEnabled", true);

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
            file << "    \"ssrEnabled\": " << (currentSettings->ssrEnabled ? "true" : "false") << "\n";
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
                glm::scale(glm::mat4(1.0f), glm::vec3(0.6f, 0.4f, 1.0f)),
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
            // Apply Button
            settingsUIObject->addChild(new ButtonObject(
                uiManager,
                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.09f, 0.0375f, 1.0f)), glm::vec3(0.0f, 600.0f, 0.0f)),
                "applySettingsButton",
                glm::vec4(0.2f, 0.8f, 0.2f, 1.0f),
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
                    *(this->currentSettings) = *(this->tempSettings);
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
