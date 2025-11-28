#pragma once

#include <engine/Renderer.h>
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <glm/glm.hpp>

namespace engine {
    class Entity {
    public:
        Entity(EntityManager* entityManager, const std::string& name, std::string shader, int renderPass, glm::mat4 transform, std::vector<std::string> textures = {}, bool isMovable = false) : entityManager(entityManager), name(name), shader(shader), renderPass(renderPass), transform(transform), textures(textures), isMovable(isMovable) {
            entityManager->addEntity(name, std::make_unique<Entity>(std::move(*this)));
            // load textures into Vulkan resources (descriptor set creation, etc.)
        }

        ~Entity() {
            for (auto& child : children) {
                delete child;
            }
            children.clear();
        }

        virtual void update(float deltaTime) {}

        void updateWorldTransform();

        void addChild(Entity* child);
        void removeChild(Entity* child);

        bool getIsMovable() const { return isMovable; }
        void setIsMovable(bool isMovable);

        std::string getName() const { return name; }
        Entity* getParent() const { return parent; }

        const std::vector<Entity*>& getChildren() const { return children; }

    private:
        std::string name;
        std::string shader;
        int renderPass;
        glm::mat4 transform;
        glm::mat4 worldTransform;
        std::vector<std::string> textures;
        bool isMovable;

        EntityManager* entityManager;

        std::vector<Entity*> children;
        Entity* parent = nullptr;
    };

    class EntityManager {
    public:
        EntityManager(engine::Renderer* renderer) : renderer(renderer) {}
        ~EntityManager();
        
        std::map<std::string, Entity*>& getEntities() { return entities; }

        void addEntity(const std::string& name, std::unique_ptr<Entity> entity);
        void removeEntity(const std::string& name);
        
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
        std::vector<std::unique_ptr<Entity>> entityList;

        std::map<std::string, Entity*> entities;
        std::vector<Entity*> rootEntities;
        std::vector<Entity*> movableEntities;
    };
};