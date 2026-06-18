#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <cmath>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vector>

namespace engine {
    class Renderer;
    class VolumetricManager;
    struct VolumetricGPU {
        alignas(16) glm::mat4 model;
        alignas(16) glm::mat4 invModel;
        alignas(16) glm::vec4 color; // w = density
        alignas(4) float age;
        alignas(4) float lifetime;
        alignas(4) uint32_t pad[2]{0, 0};
    };
    class Volumetric {
    public:
        Volumetric(
            const glm::mat4& initialTransform,
            const glm::mat4& finalTransform,
            const glm::vec4& color,
            float lifetime,
            float acceleration = 2.0f
        );
        const glm::vec4& getColor() const { return color; }
        void setColor(const glm::vec4& color) { this->color = color; }
        float getLifetime() const { return lifetime; }
        void setLifetime(float lifetime) { this->lifetime = lifetime; }
        float getAge() const { return age; }
        void setAge(float age) { this->age = age; }
        VolumetricGPU getGPUData() const {
            float t = age / std::max(lifetime, 0.0001f);
            float eased = 1.0f - std::pow(1.0f - t, std::max(acceleration, 0.0001f));

            glm::vec3 scale = glm::mix(scaleA, scaleB, eased);
            glm::quat rot = glm::slerp(rotA, rotB, eased);
            glm::vec3 trans = glm::mix(transA, transB, eased);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), trans)
                * glm::toMat4(rot)
                * glm::scale(glm::mat4(1.0f), scale);

            glm::vec3 invScale(
                1.0f / (std::abs(scale.x) < 1e-6f ? std::copysign(1e-6f, scale.x == 0.0f ? 1.0f : scale.x) : scale.x),
                1.0f / (std::abs(scale.y) < 1e-6f ? std::copysign(1e-6f, scale.y == 0.0f ? 1.0f : scale.y) : scale.y),
                1.0f / (std::abs(scale.z) < 1e-6f ? std::copysign(1e-6f, scale.z == 0.0f ? 1.0f : scale.z) : scale.z)
            );
            glm::mat4 invModel = glm::scale(glm::mat4(1.0f), invScale)
                * glm::transpose(glm::toMat4(rot))
                * glm::translate(glm::mat4(1.0f), -trans);
            return {
                .model = model,
                .invModel = invModel,
                .color = color,
                .age = age,
                .lifetime = lifetime
            };
        }

        const glm::mat4& getFinalTransform() const { return finalTransform; }

        void markForDeletion() { markedForDeletion = true; }
        bool isMarkedForDeletion() const { return markedForDeletion; }
    private:
        glm::mat4 initialTransform;
        glm::mat4 finalTransform;
        glm::vec4 color;
        float lifetime;
        float acceleration = 2.0f;
        float age = 0.0f;
        bool markedForDeletion = false;
        glm::vec3 scaleA, scaleB, transA, transB;
        glm::quat rotA, rotB;
    };
    class VolumetricManager {
    public:
        VolumetricManager(engine::Renderer* renderer);
        ~VolumetricManager();
        void init();
        void clear();

        void createVolumetric(const glm::mat4& initialTransform, const glm::mat4& finalTransform, const glm::vec4& color, float lifetime, float acceleration = 2.0f) {
            if (volumetrics.size() >= hardCap) return;
            volumetrics.emplace_back(initialTransform, finalTransform, color, lifetime, acceleration);
        }

        void updateVolumetricBuffer(uint32_t currentFrame);
        void createVolumetricDescriptorSets();
        const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }
        const std::vector<Volumetric>& getVolumetrics() const { return volumetrics; }

        void updateAll(float deltaTime);
        void renderVolumetrics(VkCommandBuffer commandBuffer, uint32_t currentFrame);

        uint32_t getVisibleVolumetrics() const { return visibleVolumetrics; }

    private:
        engine::Renderer* renderer;

        std::vector<Volumetric> volumetrics;
        std::vector<VkBuffer> volumetricBuffers;
        std::vector<VkDeviceMemory> volumetricBufferMemory;
        std::vector<void*> volumetricBuffersMapped;
        std::vector<VkDescriptorSet> descriptorSets;

        uint32_t visibleVolumetrics = 0;
        uint32_t maxVolumetrics = 100;
        uint32_t hardCap = 5000;
        uint32_t frameCounter = 0;

        VkBuffer cubeVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory cubeVertexBufferMemory = VK_NULL_HANDLE;
    };
};