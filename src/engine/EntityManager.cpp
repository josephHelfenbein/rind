#include <engine/EntityManager.h>
#include <engine/Camera.h>
#include <engine/Light.h>
#include <engine/Collider.h>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

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
    
    constexpr size_t MAX_JOINTS = 128;
    constexpr size_t JOINT_MATRIX_UBO_SIZE = MAX_JOINTS * sizeof(glm::mat4);
    
    std::vector<glm::mat4> identityMatrices(MAX_JOINTS, glm::mat4(1.0f));
    
    for (size_t frame = 0; frame < frames; ++frame) {
        for (size_t binding = 0; binding < requiredStride; ++binding) {
            const size_t index = frame * requiredStride + binding;
            std::tie(uniformBuffers[index], uniformBuffersMemory[index]) = renderer->createBuffer(
                JOINT_MATRIX_UBO_SIZE,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
            void* data;
            if (vkMapMemory(renderer->getDevice(), uniformBuffersMemory[index], 0, JOINT_MATRIX_UBO_SIZE, 0, &data) == VK_SUCCESS) {
                memcpy(data, identityMatrices.data(), JOINT_MATRIX_UBO_SIZE);
                vkUnmapMemory(renderer->getDevice(), uniformBuffersMemory[index]);
            }
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

void engine::Entity::playAnimation(const std::string& animationName, bool loop, float speed) {
    if (!model || !model->hasAnimations()) return;
    const auto* clip = model->getAnimation(animationName);
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
    const auto& skeleton = model->getSkeleton();
    if (jointMatrices.size() != skeleton.size()) {
        jointMatrices.resize(skeleton.size(), glm::mat4(1.0f));
    }
}

void engine::Entity::updateAnimation(float deltaTime) {
    if (!model || !model->hasAnimations() || animState.currentAnimation.empty()) return;
    const auto* clip = model->getAnimation(animState.currentAnimation);
    if (!clip) return;
    const auto& skeleton = model->getSkeleton();
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
    std::vector<glm::vec3> localTranslations(skeleton.size());
    std::vector<glm::quat> localRotations(skeleton.size());
    std::vector<glm::vec3> localScales(skeleton.size());
    
    for (size_t i = 0; i < skeleton.size(); ++i) {
        const auto& joint = skeleton[i];
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::decompose(joint.localTransform, localScales[i], localRotations[i], localTranslations[i], skew, perspective);
    }
    std::vector<glm::vec3> prevTranslations;
    std::vector<glm::quat> prevRotations;
    std::vector<glm::vec3> prevScales;
    const Model::AnimationClip* prevClip = nullptr;
    
    if (animState.blendFactor < 1.0f && !animState.prevAnimation.empty()) {
        prevClip = model->getAnimation(animState.prevAnimation);
        if (prevClip) {
            prevTranslations = localTranslations;
            prevRotations = localRotations;
            prevScales = localScales;
            float prevTime = fmod(animState.currentTime, prevClip->duration);
            
            for (const auto& channel : prevClip->channels) {
                if (channel.targetNode >= skeleton.size()) continue;        
                const auto& sampler = prevClip->samplers[channel.samplerIndex];
                if (sampler.inputTimes.empty()) continue;
                float t = prevTime;
                size_t keyIndex = 0;
                for (size_t i = 0; i < sampler.inputTimes.size() - 1; ++i) {
                    if (t >= sampler.inputTimes[i] && t < sampler.inputTimes[i + 1]) {
                        keyIndex = i;
                        break;
                    }
                    if (i == sampler.inputTimes.size() - 2) {
                        keyIndex = i;
                    }
                }
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
    for (const auto& channel : clip->channels) {
        if (channel.targetNode >= skeleton.size()) continue;        
        const auto& sampler = clip->samplers[channel.samplerIndex];
        if (sampler.inputTimes.empty()) continue;
        float t = animState.currentTime;
        size_t keyIndex = 0;
        for (size_t i = 0; i < sampler.inputTimes.size() - 1; ++i) {
            if (t >= sampler.inputTimes[i] && t < sampler.inputTimes[i + 1]) {
                keyIndex = i;
                break;
            }
            if (i == sampler.inputTimes.size() - 2) {
                keyIndex = i;
            }
        }
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
    std::vector<glm::mat4> globalTransforms(skeleton.size());
    for (size_t i = 0; i < skeleton.size(); ++i) {
        glm::mat4 T = glm::translate(glm::mat4(1.0f), localTranslations[i]);
        glm::mat4 R = glm::mat4_cast(localRotations[i]);
        glm::mat4 S = glm::scale(glm::mat4(1.0f), localScales[i]);
        glm::mat4 localTransform = T * R * S;
        int parentIdx = skeleton[i].parentIndex;
        if (parentIdx >= 0 && parentIdx < static_cast<int>(i)) {
            globalTransforms[i] = globalTransforms[parentIdx] * localTransform;
        } else {
            globalTransforms[i] = localTransform;
        }
        jointMatrices[i] = globalTransforms[i] * skeleton[i].inverseBindMatrix;
    }
}

engine::EntityManager::EntityManager(engine::Renderer* renderer) : renderer(renderer) {
    renderer->registerEntityManager(this);
}

engine::EntityManager::~EntityManager() {
    clear();
    destroyDummySkinningBuffer();
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
    if (dummySkinningBuffer == VK_NULL_HANDLE) {
        createDummySkinningBuffer();
    }
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
        if (entity->getCastShadow() && !entity->getUniformBuffers().empty()) {
            GraphicsShader* shadowShader = renderer->getShaderManager()->getGraphicsShader("shadow");
            if (shadowShader && entity->getShadowDescriptorSets().empty()) {
                std::vector<Texture*> noTextures;
                entity->setShadowDescriptorSets(shadowShader->createDescriptorSets(renderer, noTextures, entity->getUniformBuffers()));
            }
        }
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

void engine::EntityManager::renderShadows(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    auto& lights = getLights();
    for (auto& light : lights) {
        light->renderShadowMap(renderer, commandBuffer, currentFrame);
    }
}

void engine::EntityManager::updateAll(float deltaTime) {
    std::function<void(Entity*)> traverse = [&](Entity* entity) {
        entity->update(deltaTime);
        entity->updateAnimation(deltaTime);
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
    if (pendingDeletions.empty()) return;
    vkDeviceWaitIdle(renderer->getDevice());
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
        if (vkMapMemory(renderer->getDevice(), memory, 0, jointMatrices.size() * sizeof(glm::mat4), 0, &data) == VK_SUCCESS) {
            memcpy(data, jointMatrices.data(), jointMatrices.size() * sizeof(glm::mat4));
            vkUnmapMemory(renderer->getDevice(), memory);
        }
    };

    std::function<void(Entity*)> drawEntity = [&](Entity* entity) {
        ShaderManager* shaderManager = renderer->getShaderManager();
        Model* model = entity->getModel();
        GraphicsShader* shader = shaderManager->getGraphicsShader(entity->getShader());
        if (model && shader && shaders.find(shader) != shaders.end()) {
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
            
            std::type_index type = shader->config.pushConstantType;
            if (type == std::type_index(typeid(GBufferPC))) {
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
                    .tint = glm::vec4(1.0f),
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