#include <engine/EntityManager.h>

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
    for (auto& entityPtr : entityList) {
        delete entityPtr.get();
    }
    entityList.clear();
}

void engine::EntityManager::addEntity(const std::string& name, std::unique_ptr<Entity> entity) {
    entityList.push_back(std::move(entity));
    entities[name] = entityList.back().get();

    if (entities[name]->getIsMovable()) {
        addMovableEntry(entities[name]);
    }
    if (entities[name]->getParent() == nullptr) {
        addRootEntry(entities[name]);
    }
}

void engine::EntityManager::removeEntity(const std::string& name) {
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
        entityList.erase(std::remove_if(entityList.begin(), entityList.end(),
            [&](const std::unique_ptr<Entity>& e) { return e.get() == entity; }), entityList.end());
    }
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