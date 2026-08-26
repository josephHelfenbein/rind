#pragma once

#include <engine/ModelManager.h>
#include <engine/SpatialGrid.h>
#include <vulkan/vulkan.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <utility>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace engine {
    class Renderer;
    class EntityManager;
    struct GraphicsShader;
    class Camera;
    class Collider;
    class Entity {
    public:
        enum class EntityType {
            Generic,
            Camera,
            Static,
            Collider,
            Trigger,
            Empty,
            Model,
            Character,
            Player,
            Enemy
        };
        struct AnimationState {
            std::string currentAnimation = "";
            float currentTime = 0.0f;
            bool looping = true;
            float playbackSpeed = 1.0f;
            std::string prevAnimation = "";
            float blendFactor = 1.0f; // 0.0 - 1.0
        };
        struct EntityVkObjects {
            std::vector<VkDescriptorSet> descriptorSets;
            std::vector<VkDescriptorSet> shadowDescriptorSets;
            std::vector<VkBuffer> uniformBuffers;
            std::vector<VkDeviceMemory> uniformBuffersMemory;
            std::vector<void*> uniformBuffersMapped;
            size_t uniformBufferStride = 0;
            std::string shader;
        };
        Entity(
            EntityManager* entityManager,
            const std::string& name,
            const std::string& shader,
            const glm::mat4& transform,
            std::vector<std::string> textures = {},
            bool isMovable = false,
            const EntityType& type = EntityType::Generic
        );

        virtual ~Entity();

        virtual void update(float deltaTime) {}

        bool updateWorldTransform(const glm::mat4& parentWorld);

        void addChild(Entity* child);
        void removeChild(Entity* child);

        void setModel(Model* model);
        Model* getModel() const;

        const EntityType& getType() const { return type; }
        void setEntityType(const EntityType& newType) { type = newType; }

        bool getIsMovable() const { return isMovable; }
        void setIsMovable(bool isMovable);

        const std::string& getName() const { return name; }
        Entity* getParent() const { return parent; }
        void setParent(Entity* parent) { this->parent = parent; }
        const glm::mat4& getTransform() const { return transform; }
        void setTransform(const glm::mat4& transform) { this->transform = transform; ++transformGeneration; }
        const glm::mat4& getWorldTransform() const { return worldTransform; }
        uint32_t getTransformGeneration() const { return transformGeneration; }
        glm::vec3 getWorldPosition() const;
        const std::string& getShader() const { return vkObjects.shader; }

        const std::vector<std::string>& getTextures() const { return textures; }
        void setTextures(const std::vector<std::string>& textures);
        const std::vector<VkDescriptorSet>& getDescriptorSets() const { return vkObjects.descriptorSets; }
        void setDescriptorSets(const std::vector<VkDescriptorSet>& sets) { vkObjects.descriptorSets = sets; }
        const std::vector<VkDescriptorSet>& getShadowDescriptorSets() const { return vkObjects.shadowDescriptorSets; }
        void setShadowDescriptorSets(const std::vector<VkDescriptorSet>& sets) { vkObjects.shadowDescriptorSets = sets; }

        std::vector<VkBuffer>& getUniformBuffers() { return vkObjects.uniformBuffers; }
        std::vector<VkDeviceMemory>& getUniformBuffersMemory() { return vkObjects.uniformBuffersMemory; }
        std::vector<void*>& getUniformBuffersMapped() { return vkObjects.uniformBuffersMapped; }
        void destroyUniformBuffers();
        void clearUniformBuffers() {
            for (size_t i{}; i < vkObjects.uniformBuffers.size(); ++i) {
                if (vkObjects.uniformBuffers[i] != nullptr) {
                    vkObjects.uniformBuffers[i] = nullptr;
                }
                if (i < vkObjects.uniformBuffersMapped.size() && vkObjects.uniformBuffersMapped[i] != nullptr) {
                    vkObjects.uniformBuffersMapped[i] = nullptr;
                }
                if (i < vkObjects.uniformBuffersMemory.size() && vkObjects.uniformBuffersMemory[i] != nullptr) {
                    vkObjects.uniformBuffersMemory[i] = nullptr;
                }
            }
            vkObjects.uniformBuffers.clear();
            vkObjects.uniformBuffersMemory.clear();
            vkObjects.uniformBuffersMapped.clear();
            vkObjects.uniformBufferStride = 0;
        }
        void clearDescriptorSets() {
            clearUniformBuffers();
            for (size_t i{}; i < vkObjects.descriptorSets.size(); ++i) {
                if (vkObjects.descriptorSets[i] != nullptr) {
                    vkObjects.descriptorSets[i] = nullptr;
                }
            }
            for (size_t i{}; i < vkObjects.shadowDescriptorSets.size(); ++i) {
                if (vkObjects.shadowDescriptorSets[i] != nullptr) {
                    vkObjects.shadowDescriptorSets[i] = nullptr;
                }
            }
            vkObjects.descriptorSets.clear();
            vkObjects.shadowDescriptorSets.clear();
        }
        void ensureUniformBuffers(Renderer* renderer, GraphicsShader* shader);

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
        bool isVisible() const { return visible; }
        void setVisible(bool visible) { this->visible = visible; }

        bool operator==(const Entity& other) const { return this == &other; }

    private:
        std::string name;
        EntityType type;
        glm::mat4 transform;
        glm::mat4 worldTransform;
        std::vector<std::string> textures;
        bool isMovable;

        EntityVkObjects vkObjects;

        EntityManager* entityManager;

        Model* model = nullptr;
        AnimationState animState;
        std::vector<glm::mat4> jointMatrices;
        std::vector<glm::vec3> localTranslations;
        std::vector<glm::quat> localRotations;
        std::vector<glm::vec3> localScales;
        std::vector<glm::vec3> prevTranslations;
        std::vector<glm::quat> prevRotations;
        std::vector<glm::vec3> prevScales;
        std::vector<glm::mat4> globalTransforms;
        std::vector<size_t> samplerKeyCache;
        std::vector<size_t> prevSamplerKeyCache;
        const void* cachedClipPtr = nullptr;
        const void* cachedPrevClipPtr = nullptr;

        bool castShadow = true;
        bool visible = true;

        std::vector<Entity*> children;
        Entity* parent = nullptr;
        uint32_t transformGeneration = 0;
    };
};

// add hash specialization for Entity to allow usage in unordered_set for entities
template <>
struct std::hash<engine::Entity> {
    std::size_t operator()(const engine::Entity& entity) const {
        return std::hash<const engine::Entity*>()(&entity);
    }
};

namespace engine {
    class EntityManager {
    public:
        EntityManager(
            engine::Renderer* renderer,
            float spatialGridCellSize = 2.0f,
            glm::vec3 spatialGridMapSize = glm::vec3(100.0f)
        );
        ~EntityManager();
        
        std::unordered_map<std::string, Entity*>& getEntities() { return entities; }

        void addEntity(const std::string& name, Entity* entity);
        void unregisterEntity(const std::string& name);
        void clear();

        void loadTextures();

        std::vector<Entity*>& getRootEntities() { return rootEntities; }
        std::vector<Entity*>& getMovableEntities() { return movableEntities; }

        void addMovableEntry(Entity* entity) {
            movableEntities.push_back(entity);
        }
        void removeMovableEntry(Entity* entity) {
            std::erase(movableEntities, entity);
        }

        void addRootEntry(Entity* entity) {
            rootEntities.push_back(entity);
        }
        void removeRootEntry(Entity* entity) {
            std::erase(rootEntities, entity);
        }

        Entity* getEntity(const std::string& name) const {
            auto it = entities.find(name);
            if (it != entities.end()) {
                return it->second;
            }
            return nullptr;
        }

        void setCamera(Camera* camera) { this->camera = camera; }
        Camera* getCamera() const { return camera; }

        void addCollider(Collider* collider);
        void removeCollider(Collider* collider);
        void addDynamicCollider(Collider* collider);
        void removeDynamicCollider(Collider* collider);
        
        std::vector<Collider*>& getColliders() { return colliders; }
        std::vector<Collider*>& getDynamicColliders() { return dynamicColliders; }
        SpatialGrid& getSpatialGrid() { return spatialGrid; }
        void rebuildSpatialGrid();
        void updateDynamicColliders();
        void markTexturesDirty() { textureLoadDirty = true; }
        VkBuffer getDummySkinningBuffer() const { return dummySkinningBuffer; }

        Renderer* getRenderer() const { return renderer; }

        void updateAll(float deltaTime);
        void renderEntities(VkCommandBuffer commandBuffer, uint32_t currentFrame, bool DEBUG_RENDER_LOGS = false);

        bool hasRenderable3D();

        void markForDeletion(Entity* entity) {
            pendingDeletions.push_back(entity);
        }
        void processPendingDeletions();
        void processPendingAdditions();
        void appendToVkObjectDeletions(Entity::EntityVkObjects&& objects) {
            pendingVkObjectDeletions.push_back(std::move(objects));
        }
        void destroyUniformBuffers(VkDevice device, const Entity::EntityVkObjects& vkObjects) {
            if (device == VK_NULL_HANDLE) return;
            for (size_t i = 0; i < vkObjects.uniformBuffers.size(); ++i) {
                if (i < vkObjects.uniformBuffersMapped.size() && vkObjects.uniformBuffersMapped[i] != nullptr) {
                    if (i < vkObjects.uniformBuffersMemory.size() && vkObjects.uniformBuffersMemory[i] != VK_NULL_HANDLE) {
                        vkUnmapMemory(device, vkObjects.uniformBuffersMemory[i]);
                    }
                }
                if (vkObjects.uniformBuffers[i] != VK_NULL_HANDLE) {
                    vkDestroyBuffer(device, vkObjects.uniformBuffers[i], nullptr);
                }
                if (i < vkObjects.uniformBuffersMemory.size() && vkObjects.uniformBuffersMemory[i] != VK_NULL_HANDLE) {
                    vkFreeMemory(device, vkObjects.uniformBuffersMemory[i], nullptr);
                }
            }
        }

        void deletePendingVkObjects();

    private:
        engine::Renderer* renderer;

        std::unordered_map<std::string, Entity*> entities;
        std::vector<Entity*> rootEntities;
        std::vector<Entity*> movableEntities;
        std::vector<Collider*> colliders;
        std::vector<Collider*> dynamicColliders;
        std::vector<Entity*> pendingDeletions;
        std::vector<std::pair<std::string, Entity*>> pendingAdditions;
        std::vector<Entity::EntityVkObjects> pendingVkObjectDeletions;
        SpatialGrid spatialGrid;
        bool spatialGridDirty = true;
        bool textureLoadDirty = false;

        bool renderable3DCacheDirty = true;
        bool renderable3DCache = false;
        bool computeHasRenderable3D() const;

        std::vector<Entity*> animatedToUpdate;
        std::vector<Collider*> collidersToWarm;

        Camera* camera = nullptr;

        VkBuffer dummySkinningBuffer = VK_NULL_HANDLE;
        VkDeviceMemory dummySkinningBufferMemory = VK_NULL_HANDLE;
        void createDummySkinningBuffer();
        void destroyDummySkinningBuffer();
    };
};
