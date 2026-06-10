#pragma once

#include <engine/Collider.h>
#include <engine/SpatialGrid.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <random>
#include <vector>

namespace engine {
    class Renderer;
    struct ParticleGPU {
        alignas(16) glm::vec4 position; // w = age
        alignas(16) glm::vec4 prevPosition; // w = lifetime
        alignas(16) glm::vec4 prevPrevPosition; // w = type
        alignas(16) glm::vec4 color; // w = size
    };
    class ParticleManager {
    public:
        ParticleManager(engine::Renderer* renderer);
        ~ParticleManager();
        void init();
        void clear();

        void burstParticles(const glm::vec3& position, const glm::vec3& color, const glm::vec3& velocity, int count, float lifetime, float spread, float size = 1.0f);
        void spawnTrail(const glm::vec3& start, const glm::vec3& dir, const glm::vec3& color, float lifetime, float fakeAge = 0.0f);

        void updateParticleBuffer(uint32_t currentFrame);
        void createParticleDescriptorSets();
        std::vector<VkDescriptorSet> getDescriptorSets() const { return descriptorSets; }
        const std::vector<VkBuffer>& getParticleBuffers() const { return particleBuffers; }
        uint32_t getParticleCount() const { return static_cast<uint32_t>(particles.count()); }

        void updateAll(float deltaTime);
        void renderParticles(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    private:
        static constexpr float kGravity = 9.81f;

        struct ParticleSoA {
            std::vector<float> posX, posY, posZ;
            std::vector<float> velX, velY, velZ;
            std::vector<float> prevPosX, prevPosY, prevPosZ;
            std::vector<float> prevPrevPosX, prevPrevPosY, prevPrevPosZ;
            std::vector<float> age;
            std::vector<float> lifetime;
            std::vector<float> type; // 0 = physics, 1 = trail
            std::vector<uint8_t> dead;
            std::vector<float> size;
            std::vector<float> colorR, colorG, colorB;

            size_t count() const { return posX.size(); }
            bool empty() const { return posX.empty(); }
            void clearAll();
            size_t push(const glm::vec3& pos, const glm::vec3& col, const glm::vec3& vel,
                        float life, float typ, float sz);
            void truncateFront(size_t n);
            void compactDead();
        };

        void collideOne(size_t i, float deltaTime);
        Collider::Collision checkCollision(const glm::vec3& position);
        Collider::Collision narrowPhaseCollision(const glm::vec3& position, const engine::SpatialGrid::Candidates& candidates);
        ParticleGPU makeGPU(size_t i) const;
        glm::vec3 positionAt(size_t i) const {
            return glm::vec3(particles.posX[i], particles.posY[i], particles.posZ[i]);
        }

        engine::Renderer* renderer;
        ParticleSoA particles;

        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

        std::vector<VkBuffer> particleBuffers;
        std::vector<VkDeviceMemory> particleBufferMemory;
        std::vector<void*> particleBuffersMapped;
        std::vector<VkDescriptorSet> descriptorSets;

        uint32_t visibleCount = 0;
        uint32_t maxParticles = 5000;
        uint32_t hardCap = 100000;
    };
};
