#pragma once

#include <engine/Renderer.h>
#include <engine/EntityManager.h>
#include <engine/Collider.h>
#include <random>

namespace engine {
    class ParticleManager;
    struct ParticleGPU {
        glm::vec3 position;
        float age;
        glm::vec3 velocity;
        float lifetime;
        glm::vec4 color;
    };
    class Particle {
    public:
        Particle(ParticleManager* particleManager, EntityManager* entityManager, const glm::mat4& transform, const glm::vec4& color, const glm::vec3& velocity, float lifetime);
        ~Particle();
        void update(float deltaTime);

        engine::Collider::Collision checkCollision(const glm::vec3& position);

        ParticleGPU getGPUData() const {
            return {
                .position = glm::vec3(transform[3]),
                .age = age,
                .velocity = velocity,
                .lifetime = lifetime,
                .color = color
            };
        }

    private:
        ParticleManager* particleManager;
        EntityManager* entityManager;
        glm::mat4 transform;
        glm::vec3 velocity;
        float gravity = 9.81f;
        float lifetime = 0.0f;
        float age = 0.0f;
        glm::vec4 color;
    };
    class ParticleManager {
    public:
        ParticleManager(engine::Renderer* renderer);
        ~ParticleManager();
        void init();

        void burstParticles(const glm::mat4& transform, const glm::vec4& color, const glm::vec3& velocity, int count, float lifetime);

        void registerParticle(Particle* particle) {
            particles.push_back(particle);
        }
        void unregisterParticle(Particle* particle) {
            particles.erase(std::remove(particles.begin(), particles.end(), particle), particles.end());
        }

        void updateParticleBuffer(uint32_t currentFrame);
        void createParticleDescriptorSets();

        void updateAll(float deltaTime);
        void renderParticles(VkCommandBuffer commandBuffer, uint32_t currentFrame);

    private:
        engine::Renderer* renderer;
        std::vector<Particle*> particles;

        std::mt19937 rng{std::random_device{}()};
        std::uniform_real_distribution<float> dist{-1.0f, 1.0f};

        std::vector<VkBuffer> particleBuffers;
        std::vector<VkDeviceMemory> particleBufferMemory;
        std::vector<VkDescriptorSet> descriptorSets;

        uint32_t maxParticles = 1000;
    };
};