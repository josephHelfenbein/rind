#include <engine/EntityManager.h>
#include <engine/Camera.h>
#include <engine/IrradianceManager.h>
#include <engine/Collider.h>
#include <engine/SettingsManager.h>
#include <engine/LightManager.h>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

void engine::EntityManager::addCollider(Collider* collider) {
    colliders.push_back(collider);
    spatialGridDirty = true;
}

void engine::EntityManager::removeCollider(Collider* collider) {
    spatialGrid.remove(collider);
    std::erase(colliders, collider);
    std::erase(dynamicColliders, collider);
}

void engine::EntityManager::addDynamicCollider(Collider* collider) {
    dynamicColliders.push_back(collider);
}

void engine::EntityManager::removeDynamicCollider(Collider* collider) {
    std::erase(dynamicColliders, collider);
}

void engine::EntityManager::rebuildSpatialGrid() {
    spatialGrid.rebuild(colliders);
}

void engine::EntityManager::updateDynamicColliders() {
    for (Collider* c : dynamicColliders) {
        spatialGrid.update(c, c->getWorldAABB());
    }
}

engine::Entity::Entity(
    EntityManager* entityManager,
    const std::string& name,
    const std::string& shader,
    const glm::mat4& transform,
    std::vector<std::string> textures,
    bool isMovable,
    const EntityType& type
) : entityManager(entityManager), name(name), shader(shader), transform(transform), worldTransform(transform), textures(textures), isMovable(isMovable), type(type) {
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

void engine::Entity::updateWorldTransform(const glm::mat4& parentWorld) {
    glm::mat4 newWorldTransform = parentWorld * transform;
    if (newWorldTransform != worldTransform) {
        worldTransform = newWorldTransform;
        ++transformGeneration;
    }
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
    std::erase(children, child);
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
    uniformBuffersMapped.resize(frames * requiredStride, nullptr);
    constexpr size_t MAX_JOINTS = 128;
    constexpr size_t JOINT_MATRIX_UBO_SIZE = MAX_JOINTS * sizeof(glm::mat4);
    
    static const thread_local std::vector<glm::mat4> identityMatrices(MAX_JOINTS, glm::mat4(1.0f));
    
    for (size_t frame = 0; frame < frames; ++frame) {
        for (size_t binding = 0; binding < requiredStride; ++binding) {
            const size_t index = frame * requiredStride + binding;
            std::tie(uniformBuffers[index], uniformBuffersMemory[index]) = renderer->createBuffer(
                JOINT_MATRIX_UBO_SIZE,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
            if (vkMapMemory(renderer->getDevice(), uniformBuffersMemory[index], 0, JOINT_MATRIX_UBO_SIZE, 0, &uniformBuffersMapped[index]) == VK_SUCCESS) {
                memcpy(uniformBuffersMapped[index], identityMatrices.data(), JOINT_MATRIX_UBO_SIZE);
            }
        }
    }
}

void engine::Entity::destroyUniformBuffers(Renderer* renderer) {
    VkDevice device = renderer->getDevice();
    if (device == VK_NULL_HANDLE) {
        uniformBuffers.clear();
        uniformBuffersMemory.clear();
        uniformBuffersMapped.clear();
        uniformBufferStride = 0;
        return;
    }
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (i < uniformBuffersMapped.size() && uniformBuffersMapped[i] != nullptr) {
            if (i < uniformBuffersMemory.size() && uniformBuffersMemory[i] != VK_NULL_HANDLE) {
                vkUnmapMemory(device, uniformBuffersMemory[i]);
            }
            uniformBuffersMapped[i] = nullptr;
        }
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
    uniformBuffersMapped.clear();
    uniformBufferStride = 0;
}

void engine::Entity::playAnimation(const std::string& animationName, bool loop, float speed) {
    if (!model || !model->hasAnimations()) return;
    const engine::Model::AnimationClip* clip = model->getAnimation(animationName);
    if (!clip) {
        std::cerr << "Animation '" << animationName << "' not found on model\n";
        return;
    }
    if (!animState.currentAnimation.empty() && animState.currentAnimation != animationName) {
        animState.prevAnimation = animState.currentAnimation;
        animState.blendFactor = 0.0f;
    }
    animState.currentAnimation = animationName;
    animState.currentTime = 0.0f;
    animState.looping = loop;
    animState.playbackSpeed = speed;
    const std::vector<engine::Model::Joint>& skeleton = model->getSkeleton();
    if (jointMatrices.size() != skeleton.size()) {
        jointMatrices.resize(skeleton.size(), glm::mat4(1.0f));
    }
}

void engine::Entity::updateAnimation(float deltaTime) {
    if (!model || !model->hasAnimations() || animState.currentAnimation.empty()) return;
    const engine::Model::AnimationClip* clip = model->getAnimation(animState.currentAnimation);
    if (!clip) return;
    const std::vector<engine::Model::Joint>& skeleton = model->getSkeleton();
    if (skeleton.empty()) return;
    
    const float blendSpeed = 8.0f;
    if (animState.blendFactor < 1.0f) {
        animState.blendFactor = glm::min(1.0f, animState.blendFactor + deltaTime * blendSpeed);
    }
    
    animState.currentTime += deltaTime * animState.playbackSpeed;
    if (animState.currentTime > clip->duration) {
        if (animState.looping) {
            animState.currentTime = fmod(animState.currentTime, clip->duration);
        } else {
            animState.currentTime = clip->duration;
        }
    }
    if (jointMatrices.size() != skeleton.size()) {
        jointMatrices.resize(skeleton.size(), glm::mat4(1.0f));
    }
    localTranslations.resize(skeleton.size());
    localRotations.resize(skeleton.size());
    localScales.resize(skeleton.size());
    
    for (size_t i = 0; i < skeleton.size(); ++i) {
        const engine::Model::Joint& joint = skeleton[i];
        localTranslations[i] = joint.localTranslation;
        localRotations[i] = joint.localRotation;
        localScales[i] = joint.localScale;
    }
    const Model::AnimationClip* prevClip = nullptr;
    
    if (animState.blendFactor < 1.0f && !animState.prevAnimation.empty()) {
        prevClip = model->getAnimation(animState.prevAnimation);
        if (prevClip) {
            prevTranslations = localTranslations;
            prevRotations = localRotations;
            prevScales = localScales;
            float prevTime = fmod(animState.currentTime, prevClip->duration);
            
            for (const engine::Model::AnimationChannel& channel : prevClip->channels) {
                if (channel.targetNode >= skeleton.size()) continue;        
                const engine::Model::AnimationSampler& sampler = prevClip->samplers[channel.samplerIndex];
                if (sampler.inputTimes.empty()) continue;
                float t = prevTime;
                auto it = std::lower_bound(sampler.inputTimes.begin(), sampler.inputTimes.end(), t);
                if (it != sampler.inputTimes.begin()) --it;
                size_t keyIndex = std::distance(sampler.inputTimes.begin(), it);
                float t0 = sampler.inputTimes[keyIndex];
                float t1 = sampler.inputTimes[std::min(keyIndex + 1, sampler.inputTimes.size() - 1)];
                float factor = (t1 > t0) ? glm::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
                const glm::vec4& v0 = sampler.outputValues[keyIndex];
                const glm::vec4& v1 = sampler.outputValues[std::min(keyIndex + 1, sampler.outputValues.size() - 1)];
                size_t nodeIdx = channel.targetNode;
                switch (channel.path) {
                    case Model::AnimationChannel::Path::TRANSLATION: {
                        if (sampler.interpolation == Model::AnimationSampler::Interpolation::STEP) {
                            prevTranslations[nodeIdx] = glm::vec3(v0);
                        } else {
                            prevTranslations[nodeIdx] = glm::mix(glm::vec3(v0), glm::vec3(v1), factor);
                        }
                        break;
                    }
                    case Model::AnimationChannel::Path::ROTATION: {
                        glm::quat q0(v0.w, v0.x, v0.y, v0.z);
                        glm::quat q1(v1.w, v1.x, v1.y, v1.z);
                        if (sampler.interpolation == Model::AnimationSampler::Interpolation::STEP) {
                            prevRotations[nodeIdx] = q0;
                        } else {
                            prevRotations[nodeIdx] = glm::slerp(q0, q1, factor);
                        }
                        break;
                    }
                    case Model::AnimationChannel::Path::SCALE: {
                        if (sampler.interpolation == Model::AnimationSampler::Interpolation::STEP) {
                            prevScales[nodeIdx] = glm::vec3(v0);
                        } else {
                            prevScales[nodeIdx] = glm::mix(glm::vec3(v0), glm::vec3(v1), factor);
                        }
                        break;
                    }
                }
            }
        }
    }
    for (const engine::Model::AnimationChannel& channel : clip->channels) {
        if (channel.targetNode >= skeleton.size()) continue;
        const engine::Model::AnimationSampler& sampler = clip->samplers[channel.samplerIndex];
        if (sampler.inputTimes.empty()) continue;
        float t = animState.currentTime;
        auto it = std::lower_bound(sampler.inputTimes.begin(), sampler.inputTimes.end(), t);
        if (it != sampler.inputTimes.begin()) --it;
        size_t keyIndex = std::distance(sampler.inputTimes.begin(), it);
        float t0 = sampler.inputTimes[keyIndex];
        float t1 = sampler.inputTimes[std::min(keyIndex + 1, sampler.inputTimes.size() - 1)];
        float factor = (t1 > t0) ? glm::clamp((t - t0) / (t1 - t0), 0.0f, 1.0f) : 0.0f;
        const glm::vec4& v0 = sampler.outputValues[keyIndex];
        const glm::vec4& v1 = sampler.outputValues[std::min(keyIndex + 1, sampler.outputValues.size() - 1)];
        size_t nodeIdx = channel.targetNode;
        switch (channel.path) {
            case Model::AnimationChannel::Path::TRANSLATION: {
                if (sampler.interpolation == Model::AnimationSampler::Interpolation::STEP) {
                    localTranslations[nodeIdx] = glm::vec3(v0);
                } else {
                    localTranslations[nodeIdx] = glm::mix(glm::vec3(v0), glm::vec3(v1), factor);
                }
                break;
            }
            case Model::AnimationChannel::Path::ROTATION: {
                glm::quat q0(v0.w, v0.x, v0.y, v0.z);
                glm::quat q1(v1.w, v1.x, v1.y, v1.z);
                if (sampler.interpolation == Model::AnimationSampler::Interpolation::STEP) {
                    localRotations[nodeIdx] = q0;
                } else {
                    localRotations[nodeIdx] = glm::slerp(q0, q1, factor);
                }
                break;
            }
            case Model::AnimationChannel::Path::SCALE: {
                if (sampler.interpolation == Model::AnimationSampler::Interpolation::STEP) {
                    localScales[nodeIdx] = glm::vec3(v0);
                } else {
                    localScales[nodeIdx] = glm::mix(glm::vec3(v0), glm::vec3(v1), factor);
                }
                break;
            }
        }
    }
    if (prevClip && animState.blendFactor < 1.0f) {
        float blend = animState.blendFactor;
        for (size_t i = 0; i < skeleton.size(); ++i) {
            localTranslations[i] = glm::mix(prevTranslations[i], localTranslations[i], blend);
            localRotations[i] = glm::slerp(prevRotations[i], localRotations[i], blend);
            localScales[i] = glm::mix(prevScales[i], localScales[i], blend);
        }
    }
    globalTransforms.resize(skeleton.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < skeleton.size(); ++i) {
        glm::mat4 localTransform = glm::mat4_cast(localRotations[i]);
        localTransform[0] *= localScales[i].x;
        localTransform[1] *= localScales[i].y;
        localTransform[2] *= localScales[i].z;
        localTransform[3] = glm::vec4(localTranslations[i], 1.0f);
        int parentIdx = skeleton[i].parentIndex;
        if (parentIdx >= 0 && parentIdx < static_cast<int>(i)) {
            globalTransforms[i] = globalTransforms[parentIdx] * localTransform;
        } else {
            globalTransforms[i] = localTransform;
        }
        jointMatrices[i] = globalTransforms[i] * skeleton[i].inverseBindMatrix;
    }
}

void engine::Entity::setTextures(const std::vector<std::string>& textures) {
    this->textures = textures;
    getEntityManager()->markTexturesDirty();
}

engine::EntityManager::EntityManager(engine::Renderer* renderer) : renderer(renderer) {
    renderer->registerEntityManager(this);
}

engine::EntityManager::~EntityManager() {
    clear();
    destroyDummySkinningBuffer();
}

void engine::EntityManager::createDummySkinningBuffer() {
    struct SkinnedVertex {
        glm::vec4 joints{0.0f};
        glm::vec4 weights{0.0f};
    };
    constexpr size_t MAX_DUMMY_VERTICES = 65536;
    constexpr size_t BUFFER_SIZE = MAX_DUMMY_VERTICES * sizeof(SkinnedVertex);
    
    std::tie(dummySkinningBuffer, dummySkinningBufferMemory) = renderer->createBuffer(
        BUFFER_SIZE,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    VkDevice device = renderer->getDevice();
    void* data;
    if (vkMapMemory(device, dummySkinningBufferMemory, 0, BUFFER_SIZE, 0, &data) == VK_SUCCESS) {
        memset(data, 0, BUFFER_SIZE);
        vkUnmapMemory(device, dummySkinningBufferMemory);
    }
}

void engine::EntityManager::destroyDummySkinningBuffer() {
    VkDevice device = renderer->getDevice();
    if (dummySkinningBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, dummySkinningBuffer, nullptr);
        dummySkinningBuffer = VK_NULL_HANDLE;
    }
    if (dummySkinningBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, dummySkinningBufferMemory, nullptr);
        dummySkinningBufferMemory = VK_NULL_HANDLE;
    }
}

void engine::EntityManager::addEntity(const std::string& name, Entity* entity) {
    pendingAdditions.push_back(std::make_pair(name, entity));
}

static std::unordered_set<engine::Entity::EntityType> wontResetShadows = {
    engine::Entity::EntityType::Camera,
    engine::Entity::EntityType::Collider,
    engine::Entity::EntityType::Trigger,
    engine::Entity::EntityType::Empty
};

void engine::EntityManager::processPendingAdditions() {
    bool resetShadows = false;
    if (!pendingAdditions.empty()) {
        textureLoadDirty = true;
    }
    for (const auto& [name, entity] : pendingAdditions) {
        entities[name] = entity;
        if (entities[name]->getIsMovable()) {
            addMovableEntry(entity);
        }
        if (entities[name]->getParent() == nullptr) {
            addRootEntry(entity);
        }
        if (!entity->getIsMovable() && !wontResetShadows.contains(entity->getType())) {
            bool hasMovableParent = false;
            Entity* current = entity->getParent();
            while (current) {
                if (current->getIsMovable()) {
                    hasMovableParent = true;
                    break;
                }
                current = current->getParent();
            }
            resetShadows = !hasMovableParent;
        }
    }
    pendingAdditions.clear();
    if (resetShadows) {
        getRenderer()->getLightManager()->createAllShadowMaps();
        vkDeviceWaitIdle(renderer->getDevice());
        renderer->createPostProcessDescriptorSets();
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
        Camera* currentCamera = getCamera();
        if (currentCamera && currentCamera->getName() == entity->getName()) {
            setCamera(nullptr);
        }
        if (entity->getType() == Entity::EntityType::Collider || entity->getType() == Entity::EntityType::Trigger) {
            Collider* collider = static_cast<Collider*>(entity);
            spatialGrid.remove(collider);
            std::erase(colliders, collider);
        }
        entities.erase(it);
    }
}

void engine::EntityManager::clear() {
    movableEntities.clear();
    colliders.clear();
    spatialGrid.clear();
    entities.clear();
    auto roots = std::move(rootEntities);
    rootEntities.clear();
    for (Entity* root : roots) {
        delete root;
    }
}

void engine::EntityManager::loadTextures() {
    if (dummySkinningBuffer == VK_NULL_HANDLE) {
        createDummySkinningBuffer();
    }
    for (auto& [name, entity] : entities) {
        if (!entity->getDescriptorSets().empty()) continue;
        const std::vector<std::string>& textures = entity->getTextures();
        TextureManager* textureManager = renderer->getTextureManager();
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
        for (size_t i = 0; i < textures.size(); ++i) {
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
        for (size_t i = 0; i < static_cast<size_t>(fragmentBindings); ++i) {
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
        if (entity->getCastShadow() && !entity->getUniformBuffers().empty()) {
            GraphicsShader* shadowShader = renderer->getShaderManager()->getGraphicsShader("shadow");
            LightManager* lightManager = renderer->getLightManager();
            if (shadowShader && lightManager && entity->getShadowDescriptorSets().empty()) {
                lightManager->createShadowLightsBuffers();
                auto& entityBuffers = entity->getUniformBuffers();
                auto& shadowLightsBuffers = lightManager->getShadowLightsBuffers();
                const size_t framesInFlight = std::min(entityBuffers.size(), shadowLightsBuffers.size());
                if (framesInFlight > 0) {
                    std::vector<VkBuffer> interleavedBuffers;
                    interleavedBuffers.reserve(framesInFlight * 2);
                    for (size_t frame = 0; frame < framesInFlight; ++frame) {
                        interleavedBuffers.push_back(entityBuffers[frame]);
                        interleavedBuffers.push_back(shadowLightsBuffers[frame]);
                    }
                    std::vector<Texture*> noTextures;
                    entity->setShadowDescriptorSets(shadowShader->createDescriptorSets(renderer, noTextures, interleavedBuffers));
                }
            }
        }
    }
}

void engine::EntityManager::updateAll(float deltaTime) {
    if (spatialGridDirty) {
        auto updateTransforms = [&](auto& self, Entity* entity, const glm::mat4& parentWorld) -> void {
            entity->updateWorldTransform(parentWorld);
            for (Entity* child : entity->getChildren()) {
                self(self, child, entity->getWorldTransform());
            }
        };
        for (Entity* rootEntity : rootEntities) {
            updateTransforms(updateTransforms, rootEntity, glm::mat4(1.0f));
        }
        rebuildSpatialGrid();
        spatialGridDirty = false;
    } else {
        updateDynamicColliders();
    }
    
    auto traverse = [&](auto& self, Entity* entity, const glm::mat4& parentWorld) -> void {
        entity->updateWorldTransform(parentWorld);
        entity->update(deltaTime);
        entity->updateAnimation(deltaTime);
        for (Entity* child : entity->getChildren()) {
            self(self, child, entity->getWorldTransform());
        }
    };
    std::vector<Entity*> rootsCopy = rootEntities;
    for (Entity* rootEntity : rootsCopy) {
        traverse(traverse, rootEntity, glm::mat4(1.0f));
    }
    if (textureLoadDirty) {
        loadTextures();
        textureLoadDirty = false;
    }
}

void engine::EntityManager::processPendingDeletions() {
    if (pendingDeletions.empty()) return;
    vkDeviceWaitIdle(renderer->getDevice());
    static thread_local std::vector<Entity*> rootsTraversalBuffer;
    rootsTraversalBuffer.clear();
    std::swap(rootsTraversalBuffer, pendingDeletions);
    for (Entity* entity : rootsTraversalBuffer) {
        if (entity) {
            removeEntity(entity->getName());
        }
    }
}

void engine::EntityManager::renderEntities(VkCommandBuffer commandBuffer, uint32_t currentFrame, bool DEBUG_RENDER_LOGS) {
    std::vector<Entity*>& rootEntities = getRootEntities();
    Camera* camera = getCamera();
    if (!camera) {
        std::cout << "Warning: No camera set in EntityManager. Skipping entity rendering.\n";
        return;
    }

    auto updateJointMatricesUBO = [&](Entity* entity) {
        if (!entity->isAnimated()) return;
        const auto& jointMatrices = entity->getJointMatrices();
        if (jointMatrices.empty()) return;
        auto& uniformBuffers = entity->getUniformBuffers();
        if (uniformBuffers.empty()) return;
        const size_t stride = 1;
        const size_t bufferIndex = currentFrame * stride + 0;
        if (bufferIndex >= uniformBuffers.size()) return;
        VkBuffer buffer = uniformBuffers[bufferIndex];
        if (buffer == VK_NULL_HANDLE) return;
        void* data;
        VkDeviceMemory memory = entity->getUniformBuffersMemory()[bufferIndex];
        memcpy(entity->getUniformBuffersMapped()[bufferIndex], jointMatrices.data(), jointMatrices.size() * sizeof(glm::mat4));
    };

    auto drawEntity = [&](auto& self, Entity* entity) -> void {
        ShaderManager* shaderManager = renderer->getShaderManager();
        Model* model = entity->getModel();
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("gbuffer");
        if (model && shader && entity->isVisible()
         && (camera->isAABBInFrustum(model->getAABB(), entity->getWorldTransform())
             || entity->isAnimated()
            )
        ) {
            updateJointMatricesUBO(entity);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
            VkBuffer vertexBuffers[] = { model->getVertexBuffer().first };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, model->getIndexBuffer().first, 0, VK_INDEX_TYPE_UINT32);
            if (model->hasSkinning()) {
                VkBuffer skinBuffers[] = { model->getSkinningBuffer().first };
                vkCmdBindVertexBuffers(commandBuffer, 1, 1, skinBuffers, offsets);
            } else {
                VkBuffer dummyBuffers[] = { dummySkinningBuffer };
                vkCmdBindVertexBuffers(commandBuffer, 1, 1, dummyBuffers, offsets);
            }
            
            if (shader->config.fillPushConstants) {
                shader->config.fillPushConstants(renderer, shader, commandBuffer);
            }
            if (camera) {
                GBufferPC pc = {
                    .model = entity->getWorldTransform(),
                    .view = camera->getViewMatrix(),
                    .projection = camera->getProjectionMatrix(),
                    .camPos = camera->getWorldPosition(),
                    .flags = model->hasSkinning() ? 1u : 0u
                };
                vkCmdPushConstants(commandBuffer, shader->pipelineLayout, shader->config.pushConstantRange.stageFlags, 0, sizeof(GBufferPC), &pc);
            }
            const std::vector<VkDescriptorSet>& descriptorSets = entity->getDescriptorSets();
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
            self(self, child);
        }
    };
    for (Entity* entity : rootEntities) {
        drawEntity(drawEntity, entity);
    }
}
