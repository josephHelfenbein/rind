#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <filesystem>

namespace engine {
    static std::vector<char> readFile(const std::string& filename) {
        std::ifstream file(filename, std::ios::ate | std::ios::binary);
        std::vector<char> buffer(file.tellg());
        file.seekg(0);
        file.read(buffer.data(), buffer.size());
        return buffer;
    }

    static std::vector<std::string> scanDirectory(const std::string& directoryPath) {
        std::vector<std::string> fileList;
        for (const auto& entry : std::filesystem::directory_iterator(directoryPath)) {
            fileList.push_back(entry.path().string());
        }
        return fileList;
    }
};