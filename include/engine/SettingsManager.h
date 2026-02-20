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
            uint32_t aaMode = 1; // 0 = none, 1 = FXAA, 2 = SMAA
            float fpsLimit = 0.0f;
            float shadowQuality = 2.0f; // 0=512 4 samples, 1=1024 8 samples, 2=2048 16 samples, 3=2048 32 samples
            float masterVolume = 1.0f;
            bool ssrEnabled = true;
            bool showFPS = false;
        };

        struct SettingsDefinition {
            enum Type { Bool, Enum, Slider };
            Type type;
            std::string label;
            std::string key;

            bool Settings::* boolPtr = nullptr;

            uint32_t Settings::* enumPtr = nullptr;
            std::vector<std::string> enumOptions;

            float Settings::* floatPtr = nullptr;
            float minVal = 0.0f, maxVal = 1.0f;
            std::string textSuffix;
            bool isInt = false;
            float textMultiplier = 1.0f;
            bool roundOnApply = false;
            float clampMin = 0.0f, clampMax = 0.0f; // 0,0 = no clamp
            
            std::function<void(Settings* prev, Settings* curr, Renderer*)> onChange = nullptr;
        };

        SettingsManager(engine::Renderer* renderer) : renderer(renderer) {
            renderer->registerSettingsManager(this);
            loadSettings();
        };

        ~SettingsManager() {
            for (auto& es : enumStates) {
                for (auto* f : es.flags) delete f;
            }
            enumStates.clear();
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
            currentSettings->aaMode = std::clamp(static_cast<uint32_t>(parseInt(content, "aaMode", 1)), 0u, 2u);
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
            file << "    \"aaMode\": " << currentSettings->aaMode << ",\n";
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
            
            const std::vector<SettingsDefinition> settingsDefs = {
                { SettingsDefinition::Bool, "Show FPS Counter", "showFPS", &Settings::showFPS },
                { SettingsDefinition::Bool, "Enable Screen Space Reflections", "ssrEnabled", &Settings::ssrEnabled },
                { SettingsDefinition::Enum, "Ambient Occlusion Mode", "aoMode", nullptr, &Settings::aoMode, {"Disabled", "SSAO", "GTAO"} },
                { SettingsDefinition::Enum, "Anti-Aliasing Mode", "aaMode", nullptr, &Settings::aaMode, {"Disabled", "FXAA", "SMAA"} },
                { SettingsDefinition::Slider, "Master Volume", "masterVolume", nullptr, nullptr, {}, &Settings::masterVolume, 0.0f, 1.0f, "%", true, 100.0f, false, 0.0f, 0.0f },
                { SettingsDefinition::Slider, "FPS Limit", "fpsLimit", nullptr, nullptr, {}, &Settings::fpsLimit, 0.0f, 240.0f, " FPS", true, 1.0f, true, 0.0f, 240.0f,
                    [](Settings* prev, Settings* curr, Renderer* renderer) {
                        if ((prev->fpsLimit < 1e-6f) != (curr->fpsLimit < 1e-6f)) {
                            renderer->recreateSwapChain();
                        }
                    }
                },
                { SettingsDefinition::Slider, "Shadow Quality", "shadowQuality", nullptr, nullptr, {}, &Settings::shadowQuality, 0.0f, 3.0f, "", true, 1.0f, true, 0.0f, 3.0f,
                    [](Settings* prev, Settings* curr, Renderer* renderer) {
                        if (prev->shadowQuality != curr->shadowQuality) {
                            renderer->requestShadowMapRecreation();
                        }
                    }
                }
            };

            float labelY = -1300.0f;
            for (auto& es : enumStates) {
                for (auto* f : es.flags) delete f;
            }
            enumStates.clear();
            for (const SettingsDefinition& def : settingsDefs) {
                settingsUIObject->addChild(new TextObject(
                    uiManager,
                    glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.075f)), glm::vec3(450.0f, labelY, 0.0f)),
                    def.key + "Label",
                    glm::vec4(1.0f),
                    def.label,
                    "Lato",
                    Corner::TopLeft
                ));
                switch (def.type) {
                    case SettingsDefinition::Bool: {
                        settingsUIObject->addChild(new CheckboxObject(
                            uiManager,
                            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f)), glm::vec3(-350.0f, labelY * 0.75f, 0.0f)),
                            def.key + "Checkbox",
                            glm::vec4(1.0f),
                            tempSettings->*(def.boolPtr),
                            tempSettings->*(def.boolPtr),
                            Corner::TopRight
                        ));
                        break;
                    }
                    case SettingsDefinition::Enum: {
                        float amountIn = def.enumOptions.size() * -500.0f - 50.0f;
                        std::string enumOptionsLabel = "";
                        for (const std::string& option : def.enumOptions) {
                            enumOptionsLabel += "   " + option;
                        }
                        settingsUIObject->addChild(new TextObject(
                            uiManager,
                            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.06f)), glm::vec3(-600, labelY * 1.25 - 350.0f, 0.0f)),
                            def.key + "EnumLabel",
                            glm::vec4(1.0f),
                            enumOptionsLabel,
                            "Lato",
                            Corner::TopRight
                        ));
                        std::vector<CheckboxObject*> checkboxes;
                        EnumState state;
                        state.field = def.enumPtr;
                        for (size_t i = 0; i < def.enumOptions.size(); i++) {
                            bool* flag = new bool(tempSettings->*(def.enumPtr) == i);
                            CheckboxObject* checkbox = new CheckboxObject(
                                uiManager,
                                glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.1f)), glm::vec3(amountIn, labelY * 0.75f, 0.0f)),
                                def.key + "Option" + std::to_string(i),
                                glm::vec4(1.0f),
                                *flag,
                                *flag,
                                Corner::TopRight
                            );
                            settingsUIObject->addChild(checkbox);
                            checkboxes.push_back(checkbox);
                            state.flags.push_back(flag);
                            amountIn += 500.0f;
                        }
                        for (size_t i = 0; i < checkboxes.size(); i++) {
                            std::vector<CheckboxObject*> otherCheckboxes = checkboxes;
                            otherCheckboxes.erase(otherCheckboxes.begin() + i);
                            checkboxes[i]->setBoundBools(otherCheckboxes);
                        }
                        enumStates.push_back(std::move(state));
                        break;
                    }
                    case SettingsDefinition::Slider: {
                        SliderObject* slider = new SliderObject(
                            uiManager,
                            glm::translate(glm::scale(glm::mat4(1.0f), glm::vec3(0.4f, 0.14f, 1.0f)), glm::vec3(-100.0f, labelY * 0.55f, 0.0f)),
                            def.key + "Slider",
                            def.minVal,
                            def.maxVal,
                            tempSettings->*(def.floatPtr),
                            Corner::TopRight,
                            def.textSuffix,
                            def.isInt,
                            def.textMultiplier
                        );
                        settingsUIObject->addChild(slider);
                        break;
                    }
                };
                labelY -= (def.type == SettingsDefinition::Enum) ? 700.0f : 600.0f;
            }

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
                [this, settingsDefs]() {
                    for (const auto& enumState : this->enumStates) {
                        for (uint32_t i = 0; i < enumState.flags.size(); ++i) {
                            if (*enumState.flags[i]) {
                                tempSettings->*(enumState.field) = i;
                                break;
                            }
                        }
                    }
                    for (const auto& def : settingsDefs) {
                        if (def.type == SettingsDefinition::Slider) {
                            float& value = tempSettings->*(def.floatPtr);
                            if (def.clampMax > def.clampMin) {
                                value = std::clamp(value, def.clampMin, def.clampMax);
                            }
                            if (def.roundOnApply) {
                                value = float(static_cast<int>(value + 0.5f));
                            }
                        }
                    }
                    Settings prev = *currentSettings;
                    *currentSettings = *tempSettings;
                    for (const auto& def : settingsDefs) {
                        if (def.onChange) {
                            def.onChange(&prev, currentSettings, renderer);
                        }
                    }
                    saveSettings();
                },
                Corner::Bottom
            ));
            renderer->refreshDescriptorSets();
        }

        void hideSettingsUI() {
            if (!settingsUIObject) return;
            for (auto& es : enumStates) {
                for (auto* f : es.flags) delete f;
            }
            enumStates.clear();
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

        struct EnumState {
            std::vector<bool*> flags;
            uint32_t Settings::* field;
        };
        std::vector<EnumState> enumStates;

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
