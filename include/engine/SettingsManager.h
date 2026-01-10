#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>

namespace engine {
    class SettingsManager {
    public:
        struct Settings {
            float masterVolume = 1.0f;
            uint32_t aoMode = 2; // 0 = disabled, 1 = ssao, 2 = gtao
            bool fxaaEnabled = true;
            bool ssrEnabled = true;
        };

        SettingsManager(engine::Renderer* renderer) {
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
            std::ifstream file(configPath);
            if (!file.is_open()) {
                return;
            }
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            file.close();

            currentSettings->masterVolume = parseFloat(content, "masterVolume", 1.0f);
            currentSettings->aoMode = static_cast<uint32_t>(parseInt(content, "aoMode", 2));
            currentSettings->fxaaEnabled = parseBool(content, "fxaaEnabled", true);
            currentSettings->ssrEnabled = parseBool(content, "ssrEnabled", true);
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

    private:
        Settings* currentSettings;

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