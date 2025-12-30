#include <engine/EntityManager.h>
#include <engine/Camera.h>
#include <engine/Light.h>
#include <engine/Collider.h>

engine::Entity::Entity(EntityManager* entityManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures, bool isMovable) 
    : entityManager(entityManager), name(name), shader(shader), transform(transform), worldTransform(transform), textures(textures), isMovable(isMovable) {
        entityManager->addEntity(name, this);
    }

engine::Entity::~Entity() {
    destroyUniformBuffers(entityManager ? entityManager->getRenderer() : nullptr);
    for (auto& child : children) {
        delete child;
    }
    children.clear();
    entityManager->unregisterEntity(name);
}

void engine::Entity::setModel(engine::Model* model) {
    this->model = model;
}

engine::Model* engine::Entity::getModel() const {
    return model;
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

glm::vec3 engine::Entity::getWorldPosition() const {
    return glm::vec3(worldTransform[3]);
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
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
        }
    }
}

void engine::Entity::destroyUniformBuffers(Renderer* renderer) {
    VkDevice device = renderer->getDevice();
    if (device == VK_NULL_HANDLE) {
        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBufferStride = 0;
        return;
    }
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (uniformBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, uniformBuffers[i], nullptr);
            uniformBuffers[i] = VK_NULL_HANDLE;
        }
        if (i < uniformBuffersMemory.size() && uniformBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, uniformBuffersMemory[i], nullptr);
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
    VkDevice device = renderer->getDevice();
    for (auto& buffer : lightsBuffers) {
        if (buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, buffer, nullptr);
        }
    }
    for (auto& memory : lightsBuffersMemory) {
        if (memory != VK_NULL_HANDLE) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
    lightsBuffers.clear();
    lightsBuffersMemory.clear();
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
        if (Light* light = dynamic_cast<Light*>(entity)) {
            lights.erase(std::remove(lights.begin(), lights.end(), light), lights.end());
        }
        Camera* currentCamera = getCamera();
        if (currentCamera && currentCamera->getName() == entity->getName()) {
            setCamera(nullptr);
        }
        if (Collider* collider = dynamic_cast<Collider*>(entity)) {
            colliders.erase(std::remove(colliders.begin(), colliders.end(), collider), colliders.end());
        }
        entities.erase(it);
    }
}

void engine::EntityManager::clear() {
    movableEntities.clear();
    lights.clear();
    std::vector<Entity*> rootsSnapshot;
    rootsSnapshot.swap(rootEntities);
    for (Entity* root : rootsSnapshot) {
        delete root;
    }
    entities.clear();
}

void engine::EntityManager::loadTextures() {
    for (auto& [name, entity] : entities) {
        if (!entity->getDescriptorSets().empty()) continue;
        std::vector<std::string> textures = entity->getTextures();
        auto textureManager = renderer->getTextureManager();
        if (!textureManager) throw std::runtime_error("TextureManager not registered in Renderer");
        std::vector<engine::Texture*> texturePtrs;
        engine::GraphicsShader *shader = renderer->getShaderManager()->getGraphicsShader(entity->getShader());
        if (!shader) {
            if (!entity->getShader().empty()) {
                std::cout << "Warning: Shader " << entity->getShader() << " for Entity " << name << " not found.\n";
            }
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
            for (size_t i = texturePtrs.size(); i < defaultTextures.size(); ++i) {
                Texture* defaultTex = textureManager->getTexture(defaultTextures[i]);
                if (defaultTex) {
                    texturePtrs.push_back(defaultTex);
                    std::cout << std::format("Warning: Not enough textures for Entity {}. Using default texture {}.\n", name, defaultTextures[i]);
                }
            }
        }
        const int fragmentBindings = std::max(shader->config.fragmentBitBindings, 0);
        auto getFragmentType = [&](int index) {
            if (!shader->config.fragmentDescriptorTypes.empty() && static_cast<size_t>(index) < shader->config.fragmentDescriptorTypes.size()) {
                return shader->config.fragmentDescriptorTypes[static_cast<size_t>(index)];
            }
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        };
        auto getFragmentCount = [&](int index) {
            if (!shader->config.fragmentDescriptorCounts.empty() && shader->config.fragmentDescriptorCounts.size() == static_cast<size_t>(fragmentBindings)) {
                return std::max(shader->config.fragmentDescriptorCounts[static_cast<size_t>(index)], 1u);
            }
            return 1u;
        };
        size_t requiredTextures = 0;
        for (int i = 0; i < fragmentBindings; ++i) {
            VkDescriptorType type = getFragmentType(i);
            if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER || type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                requiredTextures += getFragmentCount(i);
            }
        }
        if (texturePtrs.size() < requiredTextures) {
            std::cout << std::format("Error: Not enough textures for Entity {}. Expected {} image bindings, got {}. Skipping descriptor set creation.\n", name, requiredTextures, texturePtrs.size());
            continue;
        }
        entity->ensureUniformBuffers(renderer, shader);
        entity->setDescriptorSets(shader->createDescriptorSets(renderer, texturePtrs, entity->getUniformBuffers()));
    }
}

void engine::EntityManager::createLightsUBO() {
    const size_t frames = static_cast<size_t>(renderer->getFramesInFlight());
    lightsBuffers.resize(frames, VK_NULL_HANDLE);
    lightsBuffersMemory.resize(frames, VK_NULL_HANDLE);
    for (size_t frame = 0; frame < frames; ++frame) {
        std::tie(lightsBuffers[frame], lightsBuffersMemory[frame]) = renderer->createBuffer(
            sizeof(engine::LightsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
}

void engine::EntityManager::updateLightsUBO(uint32_t frameIndex) {
    if (lightsBuffers.size() < static_cast<size_t>(renderer->getFramesInFlight())) {
        createLightsUBO();
    }
    if (frameIndex >= lightsBuffers.size() || lightsBuffers[frameIndex] == VK_NULL_HANDLE) {
        std::cout << std::format("Warning: Lights UBO buffer unavailable for frame {}. Skipping lights update.\n", frameIndex);
        return;
    }
    engine::LightsUBO lightsUBO{};
    auto lights = getLights();
    size_t count = std::min(lights.size(), static_cast<size_t>(64));
    for (size_t i = 0; i < count; ++i) {
        lightsUBO.pointLights[i] = lights[i]->getPointLightData();
    }
    lightsUBO.numPointLights = glm::uvec4(count, 0, 0, 0);
    renderer->copyDataToBuffer(&lightsUBO, sizeof(lightsUBO), lightsBuffers[frameIndex], lightsBuffersMemory[frameIndex]);
}

void engine::EntityManager::createAllShadowMaps() {
    auto& lights = getLights();
    for (auto& light : lights) {
        light->createShadowMaps(renderer);
    }
}

void engine::EntityManager::renderShadows(VkCommandBuffer commandBuffer) {
    auto& lights = getLights();
    for (auto& light : lights) {
        light->renderShadowMap(renderer, commandBuffer);
    }
}

void engine::EntityManager::updateAll(float deltaTime) {
    std::function<void(Entity*)> traverse = [&](Entity* entity) {
        entity->update(deltaTime);
        entity->updateWorldTransform();
        for (Entity* child : entity->getChildren()) {
            traverse(child);
        }
    };
    for (Entity* rootEntity : rootEntities) {
        traverse(rootEntity);
    }
    loadTextures();
}

void engine::EntityManager::processPendingDeletions() {
    for (Entity* entity : pendingDeletions) {
        if (entity) {
            removeEntity(entity->getName());
        }
    }
    pendingDeletions.clear();
}

void engine::EntityManager::renderEntities(VkCommandBuffer commandBuffer, RenderNode& node, uint32_t currentFrame, bool DEBUG_RENDER_LOGS) {
    std::vector<Entity*> rootEntities = getRootEntities();
    std::set<GraphicsShader*>& shaders = node.shaders;
    Camera* camera = getCamera();

    std::function<void(Entity*)> drawEntity = [&](Entity* entity) {
        ShaderManager* shaderManager = renderer->getShaderManager();
        Model* model = entity->getModel();
        GraphicsShader* shader = shaderManager->getGraphicsShader(entity->getShader());
        if (model && shader && shaders.find(shader) != shaders.end()) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
            VkBuffer vertexBuffers[] = { model->getVertexBuffer().first };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, model->getIndexBuffer().first, 0, VK_INDEX_TYPE_UINT32);
            
            std::type_index type = shader->config.pushConstantType;
            if (type == std::type_index(typeid(GBufferPC))) {
                if (camera) {
                    GBufferPC pc = {
                        .model = entity->getWorldTransform(),
                        .view = camera->getViewMatrix(),
                        .projection = camera->getProjectionMatrix(),
                        .camPos = camera->getWorldPosition()
                    };
                    vkCmdPushConstants(commandBuffer, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(GBufferPC), &pc);
                }
            } else if (type == std::type_index(typeid(LightingPC))) {
                if (camera) {
                    LightingPC pc = {
                        .invView = glm::inverse(camera->getViewMatrix()),
                        .invProj = glm::inverse(camera->getProjectionMatrix()),
                        .camPos = camera->getWorldPosition()
                    };
                    vkCmdPushConstants(commandBuffer, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(LightingPC), &pc);
                }
            } else if (type == std::type_index(typeid(UIPC))) {
                UIPC pc = {
                    .tint = glm::vec3(1.0f),
                    .model = entity->getWorldTransform()
                };
                vkCmdPushConstants(commandBuffer, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(UIPC), &pc);
            }
            std::vector<VkDescriptorSet> descriptorSets = entity->getDescriptorSets();
            if (!descriptorSets.empty()) {
                const uint32_t dsIndex = std::min<uint32_t>(currentFrame, static_cast<uint32_t>(descriptorSets.size() - 1));
                if (DEBUG_RENDER_LOGS) {
                    std::cout << "[drawEntities] shader=" << shader->name << " bind DS idx=" << dsIndex
                              << " handle=" << descriptorSets[dsIndex] << std::endl;
                }
                vkCmdBindDescriptorSets(
                    commandBuffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    shader->pipelineLayout,
                    0,
                    1,
                    &descriptorSets[dsIndex],
                    0,
                    nullptr
                );
            } else if (DEBUG_RENDER_LOGS) {
                std::cout << "[drawEntities] shader=" << shader->name << " has NO descriptor sets" << std::endl;
            }

            vkCmdDrawIndexed(commandBuffer, model->getIndexCount(), 1, 0, 0, 0);
        }
        for (Entity* child : entity->getChildren()) {
            drawEntity(child);
        }
    };
    for (Entity* entity : rootEntities) {
        drawEntity(entity);
    }
}