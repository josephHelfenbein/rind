#pragma once

#include <engine/Renderer.h>
#include <engine/ShaderManager.h>
#include <engine/TextureManager.h>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace engine {
    class Entity {
    public:
        Entity(EntityManager* entityManager, const std::string& name, std::string shader, int renderPass, glm::mat4 transform, std::vector<std::string> textures = {}, bool isMovable = false);

        ~Entity();

        virtual void update(float deltaTime) {}

        void updateWorldTransform();

        void addChild(Entity* child);
        void removeChild(Entity* child);

        bool getIsMovable() const { return isMovable; }
        void setIsMovable(bool isMovable);

        std::string getName() const { return name; }
        Entity* getParent() const { return parent; }
        void setParent(Entity* parent) { this->parent = parent; }
        glm::mat4 getTransform() const { return transform; }
        glm::mat4 getWorldTransform() const { return worldTransform; }
        std::string getShader() const { return shader; }
        int getRenderPass() const { return renderPass; }

        const std::vector<std::string>& getTextures() const { return textures; }
        const std::vector<VkDescriptorSet>& getDescriptorSets() const { return descriptorSets; }
        void setDescriptorSets(const std::vector<VkDescriptorSet>& sets) { descriptorSets = sets; }

        std::vector<VkBuffer>& getUniformBuffers() { return uniformBuffers; }
        void ensureUniformBuffers(Renderer* renderer, GraphicsShader* shader);
        void destroyUniformBuffers(Renderer* renderer);

        const std::vector<Entity*>& getChildren() const { return children; }

    private:
        std::string name;
        std::string shader;
        int renderPass;
        glm::mat4 transform;
        glm::mat4 worldTransform;
        std::vector<std::string> textures;
        bool isMovable;

        std::vector<VkDescriptorSet> descriptorSets;
        std::vector<VkBuffer> uniformBuffers;
        std::vector<VkDeviceMemory> uniformBuffersMemory;
        size_t uniformBufferStride = 0;

        EntityManager* entityManager;

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

        void updateAll(float deltaTime);

    private:
        engine::Renderer* renderer;

        std::map<std::string, Entity*> entities;
        std::vector<Entity*> rootEntities;
        std::vector<Entity*> movableEntities;
    };
};