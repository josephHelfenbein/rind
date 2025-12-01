#include <engine/EntityManager.h>

engine::Entity::Entity(EntityManager* entityManager, const std::string& name, std::string shader, int renderPass, glm::mat4 transform, std::vector<std::string> textures, bool isMovable) 
    : entityManager(entityManager), name(name), shader(shader), renderPass(renderPass), transform(transform), textures(textures), isMovable(isMovable) {
    entityManager->addEntity(name, this);
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
    if (child->getParent()) {
        child->getParent()->removeChild(child);
    }
    entityManager->removeRootEntry(child);
    children.push_back(child);
    child->setParent(this);
}

void engine::Entity::removeChild(Entity* child) {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    child->setParent(nullptr);
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

void engine::Entity::ensureUniformBuffers(Renderer* renderer, GraphicsShader* shader) {
    if (shader->config.vertexBitBindings <= 0) {
        destroyUniformBuffers(renderer);
        return;
    }
    const size_t frames = static_cast<size_t>(renderer->getFramesInFlight());
    const size_t requiredStride = static_cast<size_t>(shader->config.vertexBitBindings);
    if (requiredStride == 0 || frames == 0) {
        destroyUniformBuffers(renderer);
        return;
    }
    if (uniformBufferStride == requiredStride && uniformBuffers.size() == frames * requiredStride) {
        return;
    }
    destroyUniformBuffers(renderer);
    uniformBufferStride = requiredStride;
    uniformBuffers.resize(frames * requiredStride, VK_NULL_HANDLE);
    uniformBuffersMemory.resize(frames * requiredStride, VK_NULL_HANDLE);
    for (size_t frame = 0; frame < frames; ++frame) {
        for (size_t binding = 0; binding < requiredStride; ++binding) {
            const size_t index = frame * requiredStride + binding;
            std::tie(uniformBuffers[index], uniformBuffersMemory[index]) = renderer->createBuffer(
                sizeof(shader->config.pushConstantRange.size),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
        }
    }
}

void engine::Entity::destroyUniformBuffers(Renderer* renderer) {
    if (!renderer || renderer->getDevice() == VK_NULL_HANDLE) {
        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBufferStride = 0;
        return;
    }
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(renderer->getDevice(), uniformBuffers[i], nullptr);
            uniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (i < uniformBuffersMemory.size() && uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(renderer->getDevice(), uniformBuffersMemory[i], nullptr);
            uniformBuffersMemory[i] = VK_NULL_HANDLE;
        }
    }
    uniformBuffers.clear();
    uniformBuffersMemory.clear();
    uniformBufferStride = 0;
}

engine::EntityManager::EntityManager(engine::Renderer* renderer) : renderer(renderer) {
    renderer->registerEntityManager(this);
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

void engine::EntityManager::loadTextures() {
    for (auto& [name, entity] : entities) {
        if (entity->getTextures().empty() || !entity->getDescriptorSets().empty()) continue;
        std::vector<std::string> textures = entity->getTextures();
        auto textureManager = renderer->getTextureManager();
        if (!textureManager) throw std::runtime_error("TextureManager not registered in Renderer");
        std::vector<engine::Texture*> texturePtrs;
        engine::GraphicsShader *shader = renderer->getShaderManager()->getGraphicsShader(entity->getShader());
        if (!shader) {
            std::cout << std::format("Warning: Shader {} for Entity {} not found.\n", entity->getShader(), name);
            continue;
        }
        std::vector<std::string> defaultTextures;
        if (shader->name == "gbuffer") {
            defaultTextures = {
                "materials_default_albedo", "materials_default_metallic", "materials_default_roughness", "materials_default_normal"
            };
        } else if (shader->name == "ui") {
            defaultTextures = { "ui_window" };
        }
        for (int i = 0; i < textures.size(); ++i) {
            Texture* found = textureManager->getTexture(textures[i]);
            if (!found && i < defaultTextures.size()) {
                found = textureManager->getTexture(defaultTextures[i]);
                if (found) {
                    std::cout << std::format("Warning: Texture {} for Entity {} not found. Using default texture {} instead.\n", textures[i], name, defaultTextures[i]);
                } else {
                    std::cout << std::format("Warning: Texture {} for Entity {} not found. No default texture available.\n", textures[i], name);
                }
            } else if (!found) {
                std::cout << std::format("Warning: Texture {} for Entity {} not found.\n", textures[i], name);
                continue;
            }
            texturePtrs.push_back(found);
        }
        if (texturePtrs.size() < defaultTextures.size()) {
            for (int i = texturePtrs.size(); i < defaultTextures.size(); ++i) {
                Texture* defaultTex = textureManager->getTexture(defaultTextures[i]);
                if (defaultTex) {
                    texturePtrs.push_back(defaultTex);
                    std::cout << std::format("Warning: Not enough textures for Entity {}. Using default texture {}.\n", name, defaultTextures[i]);
                }
            }
        }
        if (texturePtrs.size() < shader->config.fragmentBitBindings) {
            std::cout << std::format("Error: Not enough textures for Entity {}. Expected {}, got {}. Skipping descriptor set creation.\n", name, shader->config.fragmentBitBindings, texturePtrs.size());
            continue;
        }
        entity->ensureUniformBuffers(renderer, shader);
        entity->setDescriptorSets(renderer->createDescriptorSets(shader, texturePtrs, entity->getUniformBuffers()));
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
    loadTextures();
}