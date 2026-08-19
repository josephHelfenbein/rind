#pragma once

#include <vector>
#include <string>
#include <string_view>
#include <fstream>
#include <stdexcept>
#include <glm/glm.hpp>

namespace engine {
    static inline std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for reading: " + filename);
        }

        const std::streampos endPos = file.tellg();
        if (endPos <= 0) {
            throw std::runtime_error("File is empty or unreadable: " + filename);
        }

        std::vector<char> buffer(static_cast<size_t>(endPos));
        file.seekg(0);
        file.read(buffer.data(), buffer.size());
        if (!file) {
            throw std::runtime_error("Failed to read file: " + filename);
        }
        return buffer;
    }

    static inline void writeFile(const std::string& filename, std::string_view data) {
        std::ofstream file(filename, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filename);
        }
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file) {
            throw std::runtime_error("Failed to write file: " + filename);
        }
    }

    static std::filesystem::path getConfigDirectory(const std::string& location) {
        std::filesystem::path configDir;

#if defined(_WIN32)
        // Windows: %APPDATA%\{game}\config.json
        const char* appdata = std::getenv("APPDATA");
        if (appdata) {
            configDir = std::filesystem::path(appdata) / location;
        } else {
            configDir = std::filesystem::path(".") / location;
        }
#elif defined(__APPLE__)
        // macOS: ~/Library/Application Support/{game}/config.json
        const char* home = std::getenv("HOME");
        if (home) {
            configDir = std::filesystem::path(home) / "Library" / "Application Support" / location;
        } else {
            configDir = std::filesystem::path(".") / location;
        }
#else
        // Linux/Unix: ~/.config/{game}/config.json
        const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
        if (xdgConfig) {
            configDir = std::filesystem::path(xdgConfig) / location;
        } else {
            const char* home = std::getenv("HOME");
            if (home) {
                configDir = std::filesystem::path(home) / ".config" / location;
            } else {
                configDir = std::filesystem::path(".") / location;
            }
        }
#endif
        return configDir;
    }

    static inline void remapCoord(glm::vec3& coord) {
        float temp = coord.x;
        coord.x = -coord.z;
        coord.z = temp;
    }

    static inline const glm::vec3 blenderRemap(const glm::vec3& coord) {
        return glm::vec3(coord.x, coord.z, -coord.y);
    }
};
