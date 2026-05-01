#pragma once

#define GLM_ENABLE_EXPERIMENTAL
#include <cmath>
#include <engine/Renderer.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>

namespace engine {
    class VolumetricManager;
    struct VolumetricGPU {
        glm::mat4 model;
        glm::mat4 invModel;
        glm::vec4 color; // w = density
        float age;
        float lifetime;
        float pad[2];
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

            glm::vec3 scaleA, scaleB, transA, transB, skew;
            glm::vec4 persp;
            glm::quat rotA, rotB;
            glm::decompose(initialTransform, scaleA, rotA, transA, skew, persp);
            glm::decompose(finalTransform, scaleB, rotB, transB, skew, persp);
            glm::vec3 scale = glm::mix(scaleA, scaleB, eased);
            glm::quat rot = glm::slerp(rotA, rotB, eased);
            glm::vec3 trans = glm::mix(transA, transB, eased);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), trans) 
                * glm::toMat4(rot)
                * glm::scale(glm::mat4(1.0f), scale);
            return {
                .model = model,
                .invModel = glm::inverse(model),
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
        std::vector<VkDescriptorSet> getDescriptorSets() const { return descriptorSets; }
        std::vector<Volumetric> getVolumetrics() const { return volumetrics; }

        void updateAll(float deltaTime);
        void renderVolumetrics(VkCommandBuffer commandBuffer, uint32_t currentFrame);

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