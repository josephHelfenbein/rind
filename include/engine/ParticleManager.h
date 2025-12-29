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
        glm::vec3 prevPosition;
        float lifetime;
        glm::vec3 prevPrevPosition;
        float type;
        glm::vec4 color;
    };
    class Particle {
    public:
        Particle(ParticleManager* particleManager, EntityManager* entityManager, const glm::mat4& transform, const glm::vec4& color, const glm::vec3& velocity, float lifetime, float type = 0.0f);
        ~Particle();
        void update(float deltaTime);

        engine::Collider::Collision checkCollision(const glm::vec3& position);

        ParticleGPU getGPUData() const {
            return {
                .position = glm::vec3(transform[3]),
                .age = age,
                .prevPosition = prevPosition,
                .lifetime = lifetime,
                .prevPrevPosition = prevPrevPosition,
                .type = type,
                .color = color
            };
        }

        void detachFromManager() { particleManager = nullptr; }

        void setPrevPosition(const glm::vec3& pos) { prevPosition = pos; }
        void setPrevPrevPosition(const glm::vec3& pos) { prevPrevPosition = pos; }

        void markForDeletion() { markedForDeletion = true; }
        bool isMarkedForDeletion() const { return markedForDeletion; }

    private:
        ParticleManager* particleManager;
        EntityManager* entityManager;
        glm::mat4 transform;
        glm::vec3 prevPosition{0.0f};
        glm::vec3 prevPrevPosition{0.0f};
        glm::vec3 velocity;
        float gravity = 9.81f;
        float lifetime = 0.0f;
        float age = 0.0f;
        float type = 0.0f;
        glm::vec4 color;
        bool markedForDeletion = false;
    };
    class ParticleManager {
    public:
        ParticleManager(engine::Renderer* renderer);
        ~ParticleManager();
        void init();

        void burstParticles(const glm::mat4& transform, const glm::vec4& color, const glm::vec3& velocity, int count, float lifetime, float spread);
        void spawnTrail(const glm::vec3& start, const glm::vec3& dir, const glm::vec4& color, float lifetime);

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

        uint32_t maxParticles = 5000;
        uint32_t hardCap = 100000;
    };
};