#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <stdexcept>
#include <glm/glm.hpp>

namespace engine {
    static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file: " + filename);
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

    static inline void remapCoord(glm::vec3& coord) {
        float temp = coord.x;
        coord.x = -coord.z;
        coord.z = temp;
    }

    static inline const glm::vec3 blenderRemap(const glm::vec3& coord) {
        return glm::vec3(coord.x, coord.z, -coord.y);
    }
};
