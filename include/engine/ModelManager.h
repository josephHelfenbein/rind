#pragma once
#include <engine/Renderer.h>
#include <glm/glm.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <vulkan/vulkan.h>
#include <vector>
#include <engine/io.h>
#include <map>
#include <string>

namespace engine {
    struct AABB {
        glm::vec3 min = glm::vec3(FLT_MAX);
        glm::vec3 max = glm::vec3(-FLT_MAX);
    };
    class Model {
    public:
        Model(std::string name, Renderer* renderer);
        ~Model();
        void loadFromFile(std::string filepath);
        std::pair<VkBuffer, VkDeviceMemory> getVertexBuffer() const { return {vertexBuffer, vertexBufferMemory}; }
        std::pair<VkBuffer, VkDeviceMemory> getIndexBuffer() const { return {indexBuffer, indexBufferMemory}; }
        uint32_t getIndexCount() const { return indexCount; }
        AABB& getAABB() { return aabb; }
    private:
        std::string name;
        Renderer* renderer;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
        AABB aabb; // min, max
    };
    class ModelManager {
    public:
        ModelManager(Renderer* renderer, std::string modelDirectory);
        ~ModelManager();

        void init();
    private:
        Renderer* renderer;
        std::string modelDirectory;
        std::map<std::string, Model*> models;
    };
};