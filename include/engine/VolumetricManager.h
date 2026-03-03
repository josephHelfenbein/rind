#pragma once

#include <engine/Renderer.h>

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
        Volumetric(VolumetricManager* volumetricManager, const glm::mat4& transform, const glm::vec4& color, float lifetime);
        ~Volumetric();
        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; }
        const glm::vec4& getColor() const { return color; }
        void setColor(const glm::vec4& color) { this->color = color; }
        float getLifetime() const { return lifetime; }
        void setLifetime(float lifetime) { this->lifetime = lifetime; }
        float getAge() const { return age; }
        void setAge(float age) { this->age = age; }
        VolumetricGPU getGPUData() const {
            return {
                .model = transform,
                .invModel = glm::inverse(transform),
                .color = color,
                .age = age,
                .lifetime = lifetime
            };
        }

        void detachFromManager() { volumetricManager = nullptr; }

        void markForDeletion() { markedForDeletion = true; }
        bool isMarkedForDeletion() const { return markedForDeletion; }
    private:
        VolumetricManager* volumetricManager;
        glm::mat4 transform;
        glm::vec4 color;
        float lifetime;
        float age = 0.0f;
        bool markedForDeletion = false;
    };
    class VolumetricManager {
    public:
        VolumetricManager(engine::Renderer* renderer);
        ~VolumetricManager();
        void init();
        void clear();

        void registerVolumetric(Volumetric* volumetric) { volumetrics.push_back(volumetric); }
        void unregisterVolumetric(Volumetric* volumetric) {
            volumetrics.erase(std::remove(volumetrics.begin(), volumetrics.end(), volumetric), volumetrics.end());
        }

        void updateVolumetricBuffer(uint32_t currentFrame);
        void createVolumetricDescriptorSets();
        std::vector<VkDescriptorSet> getDescriptorSets() const { return descriptorSets; }
        std::vector<Volumetric*> getVolumetrics() const { return volumetrics; }

        void updateAll(float deltaTime);
        void renderVolumetrics(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    private:
        engine::Renderer* renderer;

        std::vector<Volumetric*> volumetrics;
        std::vector<VkBuffer> volumetricBuffers;
        std::vector<VkDeviceMemory> volumetricBufferMemory;
        std::vector<void*> volumetricBuffersMapped;
        std::vector<VkDescriptorSet> descriptorSets;

        uint32_t maxVolumetrics = 100;
        uint32_t hardCap = 5000;
    };
};