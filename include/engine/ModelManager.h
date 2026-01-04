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
        struct Joint {
            std::string name;
            int parentIndex = -1;
            glm::mat4 inverseBindMatrix;
            glm::mat4 localTransform;
        };
        struct AnimationSampler {
            std::vector<float> inputTimes;
            std::vector<glm::vec4> outputValues; // vec3 for translation/scale, vec4 for rotation
            enum class Interpolation {
                LINEAR,
                STEP,
                CUBICSPLINE
            } interpolation = Interpolation::LINEAR;
        };
        struct AnimationChannel {
            size_t samplerIndex;
            size_t targetNode;
            enum class Path {
                TRANSLATION,
                ROTATION,
                SCALE
            } path;
        };
        struct AnimationClip {
            std::string name;
            float duration = 0.0f;
            std::vector<AnimationSampler> samplers;
            std::vector<AnimationChannel> channels;
        };
        Model(std::string name, std::string filepath, Renderer* renderer);
        ~Model();
        void loadFromFile();
        std::pair<std::vector<glm::vec3>, std::vector<uint32_t>> loadVertsForModel();
        std::pair<VkBuffer, VkDeviceMemory> getVertexBuffer() const { return {vertexBuffer, vertexBufferMemory}; }
        std::pair<VkBuffer, VkDeviceMemory> getIndexBuffer() const { return {indexBuffer, indexBufferMemory}; }
        std::pair<VkBuffer, VkDeviceMemory> getSkinningBuffer() const { return {skinningBuffer, skinningBufferMemory}; }
        uint32_t getIndexCount() const { return indexCount; }
        AABB& getAABB() { return aabb; }
        bool hasSkinning() const { return skinningBuffer != VK_NULL_HANDLE; }
        bool hasAnimations() const { return !animationsMap.empty(); }
        const std::vector<Joint>& getSkeleton() const { return skeleton; }
        const std::map<std::string, AnimationClip>& getAnimations() const { return animationsMap; }
        const AnimationClip* getAnimation(const std::string& name) const {
            auto it = animationsMap.find(name);
            return it != animationsMap.end() ? &it->second : nullptr;
        }
    private:
        std::string name;
        std::string filepath;
        Renderer* renderer;
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory vertexBufferMemory = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory indexBufferMemory = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
        AABB aabb; // min, max
        std::map<std::string, AnimationClip> animationsMap;
        std::vector<Joint> skeleton;
        VkBuffer skinningBuffer = VK_NULL_HANDLE;
        VkDeviceMemory skinningBufferMemory = VK_NULL_HANDLE;
    };
    class ModelManager {
    public:
        ModelManager(Renderer* renderer, std::string modelDirectory);
        ~ModelManager();

        void init();

        Model* getModel(const std::string& name) {
            auto it = models.find(name);
            if (it != models.end()) return it->second;
            return nullptr;
        }
    private:
        Renderer* renderer;
        std::string modelDirectory;
        std::map<std::string, Model*> models;
    };
};