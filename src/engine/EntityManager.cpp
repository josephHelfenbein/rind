#include <engine/EntityManager.h>

engine::Entity::Entity(EntityManager* entityManager, const std::string& name, std::string shader, int renderPass, glm::mat4 transform, std::vector<std::string> textures, bool isMovable) 
    : entityManager(entityManager), name(name), shader(shader), renderPass(renderPass), transform(transform), textures(textures), isMovable(isMovable) {
    entityManager->addEntity(name, this);
    // load textures into Vulkan resources (descriptor set creation, etc.)
}

engine::Entity::~Entity() {
    for (auto& child : children) {
        delete child;
    }
    children.clear();
    entityManager->unregisterEntity(name);
}

void engine::Entity::updateWorldTransform() {
    glm::mat4 transform(1.0f);
    std::vector<Entity*> hierarchy;
    for (Entity* current = this; current != nullptr; current = current->getParent()) {
        hierarchy.push_back(current);
    }
    for (int i = static_cast<int>(hierarchy.size()) - 1; i >= 0; --i) {
        Entity* current = hierarchy[i];
        transform = transform * current->transform;
    }
    worldTransform = transform;
}

void engine::Entity::addChild(Entity* child) {
    if (child->parent) {
        child->parent->removeChild(child);
    }
    entityManager->removeRootEntry(child);
    children.push_back(child);
    child->parent = this;
}

void engine::Entity::removeChild(Entity* child) {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    child->parent = nullptr;
    entityManager->addRootEntry(child);
}

void engine::Entity::setIsMovable(bool isMovable) { 
    this->isMovable = isMovable;
    if (isMovable) {
        entityManager->addMovableEntry(this);
    } else {
        entityManager->removeMovableEntry(this);
    }
}

engine::EntityManager::~EntityManager() {
    clear();
}

void engine::EntityManager::addEntity(const std::string& name, Entity* entity) {
    entities[name] = entity;

    if (entities[name]->getIsMovable()) {
        addMovableEntry(entity);
    }
    if (entities[name]->getParent() == nullptr) {
        addRootEntry(entity);
    }
}

void engine::EntityManager::removeEntity(const std::string& name) {
    auto it = entities.find(name);
    if (it != entities.end()) {
        Entity* entity = it->second;
        unregisterEntity(name);
        delete entity;
    }
}

void engine::EntityManager::unregisterEntity(const std::string& name) {
    auto it = entities.find(name);
    if (it != entities.end()) {
        Entity* entity = it->second;
        if (entity->getIsMovable()) {
            removeMovableEntry(entity);
        }
        if (entity->getParent() == nullptr) {
            removeRootEntry(entity);
        }
        entities.erase(it);
    }
}

void engine::EntityManager::clear() {
    while (!entities.empty()) {
        delete entities.begin()->second;
    }
    rootEntities.clear();
    movableEntities.clear();
}

void engine::EntityManager::updateAll(float deltaTime) {
    std::function<void(Entity*)> traverse = [&](Entity* entity) {
        entity->updateWorldTransform();
        entity->update(deltaTime);
        for (Entity* child : entity->getChildren()) {
            traverse(child);
        }
    };
    for (Entity* rootEntity : rootEntities) {
        traverse(rootEntity);
    }
}