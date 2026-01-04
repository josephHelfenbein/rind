#pragma once

#include <engine/Renderer.h>
#include <engine/ShaderManager.h>
#include <engine/TextureManager.h>
#include <engine/ModelManager.h>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace engine {
    class Camera;
    class Light;
    class Collider;
    class Entity {
    public:
        struct AnimationState {
            std::string currentAnimation = "";
            float currentTime = 0.0f;
            bool looping = true;
            float playbackSpeed = 1.0f;
            std::string prevAnimation = "";
            float blendFactor = 1.0f; // 0.0 - 1.0
        };
        Entity(EntityManager* entityManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures = {}, bool isMovable = false);

        virtual ~Entity();

        virtual void update(float deltaTime) {}

        void updateWorldTransform();

        void addChild(Entity* child);
        void removeChild(Entity* child);

        void setModel(Model* model);
        Model* getModel() const;

        bool getIsMovable() const { return isMovable; }
        void setIsMovable(bool isMovable);

        std::string getName() const { return name; }
        Entity* getParent() const { return parent; }
        void setParent(Entity* parent) { this->parent = parent; }
        glm::mat4 getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; }
        glm::mat4 getWorldTransform() const { return worldTransform; }
        glm::vec3 getWorldPosition() const;
        std::string getShader() const { return shader; }

        const std::vector<std::string>& getTextures() const { return textures; }
        const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }
        void setDescriptorSets(const std::vector<VkDescriptorSet>& sets) { descriptorSets = sets; }
        const std::vector<VkDescriptorSet>& getShadowDescriptorSets() const { return shadowDescriptorSets; }
        void setShadowDescriptorSets(const std::vector<VkDescriptorSet>& sets) { shadowDescriptorSets = sets; }

        std::vector<VkBuffer>& getUniformBuffers() { return uniformBuffers; }
        std::vector<VkDeviceMemory>& getUniformBuffersMemory() { return uniformBuffersMemory; }
        void ensureUniformBuffers(Renderer* renderer, GraphicsShader* shader);
        void destroyUniformBuffers(Renderer* renderer);

        EntityManager* getEntityManager() const { return entityManager; }

        const std::vector<Entity*>& getChildren() const { return children; }
        Entity* getChildByName(const std::string& name) const {
            for (const auto& child : children) {
                if (child->getName() == name) {
                    return child;
                }
            }
            return nullptr;
        }

        bool getCastShadow() const { return castShadow; }
        void setCastShadow(bool cast) { castShadow = cast; }

        void playAnimation(const std::string& animationName, bool loop = true, float speed = 1.0f);
        void updateAnimation(float deltaTime);
        const std::vector<glm::mat4>& getJointMatrices() const { return jointMatrices; }
        bool isAnimated() const { return model && model->hasAnimations() && !animState.currentAnimation.empty(); }
        AnimationState& getAnimationState() { return animState; }

    private:
        std::string name;
        std::string shader;
        glm::mat4 transform;
        glm::mat4 worldTransform;
        std::vector<std::string> textures;
        bool isMovable;

        std::vector<VkDescriptorSet> descriptorSets;
        std::vector<VkDescriptorSet> shadowDescriptorSets;
        std::vector<VkBuffer> uniformBuffers;
        std::vector<VkDeviceMemory> uniformBuffersMemory;
        size_t uniformBufferStride = 0;

        EntityManager* entityManager;

        Model* model = nullptr;
        AnimationState animState;
        std::vector<glm::mat4> jointMatrices;

        bool castShadow = true;

        std::vector<Entity*> children;
        Entity* parent = nullptr;
    };

    class EntityManager {
    public:
        EntityManager(engine::Renderer* renderer);
        ~EntityManager();
        
        std::map<std::string, Entity*>& getEntities() { return entities; }

        void addEntity(const std::string& name, Entity* entity);
        void removeEntity(const std::string& name);
        void unregisterEntity(const std::string& name);
        void clear();

        void loadTextures();
        
        std::vector<Entity*>& getRootEntities() { return rootEntities; }
        std::vector<Entity*>& getMovableEntities() { return movableEntities; }

        void addMovableEntry(Entity* entity) {
            movableEntities.push_back(entity);
        }
        void removeMovableEntry(Entity* entity) {
            movableEntities.erase(std::remove(movableEntities.begin(), movableEntities.end(), entity), movableEntities.end());
        }

        void addRootEntry(Entity* entity) {
            rootEntities.push_back(entity);
        }
        void removeRootEntry(Entity* entity) {
            rootEntities.erase(std::remove(rootEntities.begin(), rootEntities.end(), entity), rootEntities.end());
        }

        Entity* getEntity(const std::string& name) {
            auto it = entities.find(name);
            if (it != entities.end()) {
                return it->second;
            }
            return nullptr;
        }

        void setCamera(Camera* camera) { this->camera = camera; }
        Camera* getCamera() const { return camera; }

        void addLight(Light* light) {
            lights.push_back(light);
        }
        void addCollider(Collider* collider) {
            colliders.push_back(collider);
        }
        void removeCollider(Collider* collider) {
            colliders.erase(std::remove(colliders.begin(), colliders.end(), collider), colliders.end());
        }
        const std::vector<Light*>& getLights() const { return lights; }
        void createLightsUBO();
        void updateLightsUBO(uint32_t frameIndex);
        std::vector<VkBuffer>& getLightsBuffers() { return lightsBuffers; }
        std::vector<Collider*>& getColliders() { return colliders; }
        void createAllShadowMaps();
        void renderShadows(VkCommandBuffer commandBuffer, uint32_t currentFrame);
        VkBuffer getDummySkinningBuffer() const { return dummySkinningBuffer; }

        Renderer* getRenderer() const { return renderer; }

        void updateAll(float deltaTime);
        void renderEntities(VkCommandBuffer commandBuffer, RenderNode& node, uint32_t currentFrame, bool DEBUG_RENDER_LOGS = false);

        void markForDeletion(Entity* entity) {
            pendingDeletions.push_back(entity);
        }
        void processPendingDeletions();

    private:
        engine::Renderer* renderer;

        std::map<std::string, Entity*> entities;
        std::vector<Entity*> rootEntities;
        std::vector<Entity*> movableEntities;
        std::vector<Collider*> colliders;
        std::vector<Light*> lights;
        std::vector<Entity*> pendingDeletions;

        std::vector<VkBuffer> lightsBuffers;
        std::vector<VkDeviceMemory> lightsBuffersMemory;
        Camera* camera = nullptr;

        VkBuffer dummySkinningBuffer = VK_NULL_HANDLE;
        VkDeviceMemory dummySkinningBufferMemory = VK_NULL_HANDLE;
        void createDummySkinningBuffer();
        void destroyDummySkinningBuffer();
    };
};