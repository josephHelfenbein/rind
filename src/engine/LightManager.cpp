#include <engine/LightManager.h>

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <engine/SettingsManager.h>
#include <array>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

engine::Light::Light(
    LightManager* lightManager,
    const std::string& name,
    const glm::mat4& transform,
    const glm::vec3& color,
    float intensity,
    float radius
) : lightManager(lightManager), transform(transform), color(color), intensity(intensity), radius(radius), shadowProj(glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius)) {}

void engine::Light::setColor(const glm::vec3& color) {
    this->color = color;
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

void engine::Light::setIntensity(float intensity) {
    this->intensity = intensity;
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

void engine::Light::setRadius(float radius) {
    this->radius = radius;
    shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius);
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

void engine::Light::updateLightIdx(uint32_t newIdx) {
    lightIdx = newIdx;
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

engine::PointLight engine::Light::getPointLightData() {
    glm::vec3 worldPos = getWorldPosition();
    uint32_t shadowIdx = 0xFFFFFFFF;
    if (hasShadowMap) {
        shadowIdx = lightIdx;
    }
    PointLight pl = {
        .positionRadius = glm::vec4(worldPos, radius),
        .colorIntensity = glm::vec4(color, intensity),
        .shadowParams = glm::vec4(0.005f, radius, 0.1f, 1.0f), // bias, far, near, strength
        .shadowData = glm::uvec4(shadowIdx, hasShadowMap ? 1 : 0, 0, 0)
    };
    return pl;
}

void engine::Light::createShadowMaps(engine::Renderer* renderer, bool forceRecreate) {
    if (hasShadowMap) {
        if (!forceRecreate) return;
        destroyShadowResources(renderer->getDevice());
    }
    float settingsValue = renderer->getSettingsManager()->getSettings()->shadowQuality;
     // 256, 512, 1024, 1024
    shadowMapSize = static_cast<uint32_t>(pow(2, 8 + std::min(static_cast<int>(settingsValue), 2)));
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    
    std::tie(shadowDepthImage, shadowDepthMemory) = renderer->createImage(
        shadowMapSize, shadowMapSize,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    );
    shadowDepthImageView = renderer->createImageView(
        shadowDepthImage,
        depthFormat,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        1,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6
    );
    for (uint32_t i = 0; i < 6; ++i) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = shadowDepthImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = depthFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = i,
                .layerCount = 1
            }
        };
        vkCreateImageView(renderer->getDevice(), &viewInfo, nullptr, &shadowDepthFaceViews[i]);
    }
    
    std::tie(bakedShadowImage, bakedShadowMemory) = renderer->createImage(
        shadowMapSize, shadowMapSize,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        depthFormat,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    );
    bakedShadowImageView = renderer->createImageView(
        bakedShadowImage,
        depthFormat,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        1,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6
    );
    for (uint32_t i = 0; i < 6; ++i) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = bakedShadowImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = depthFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = i,
                .layerCount = 1
            }
        };
        vkCreateImageView(renderer->getDevice(), &viewInfo, nullptr, &bakedShadowFaceViews[i]);
    }
    
    hasShadowMap = true;
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

void engine::Light::bakeShadowMap(Renderer* renderer, VkCommandBuffer commandBuffer) {
    if (shadowBaked) return; // only bake once
    if (!hasShadowMap) {
        createShadowMaps(renderer);
    }
    
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("shadow");
    VkImageLayout previousBakedLayout = bakedImageReady
        ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    renderer->transitionImageLayoutInline(
        commandBuffer,
        bakedShadowImage,
        VK_FORMAT_D32_SFLOAT,
        previousBakedLayout,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        1,
        6
    );
    
    glm::vec3 lightPos = getWorldPosition();
    for (int i = 0; i < 6; ++i) {
        viewProjs[i] = shadowProj * glm::lookAt(lightPos, lightPos + faces[i].dir, faces[i].up);
    }
    std::vector<Entity*>& rootEntities = renderer->getEntityManager()->getRootEntities();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(shadowMapSize),
        .height = static_cast<float>(shadowMapSize),
        .minDepth = 0.0f, 
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor = {
        .offset = { 0, 0 }, 
        .extent = { shadowMapSize, shadowMapSize }
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    auto drawStaticEntity = [&](auto& self, Entity* entity, glm::mat4& viewProj) -> void {
        if (!entity->getIsMovable()
         && entity->getModel()
         && entity->getType() == Entity::EntityType::Static
         && entity->getCastShadow()) {
            Model* model = entity->getModel();
            VkBuffer vertexBuffers[] = { model->getVertexBuffer().first };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, model->getIndexBuffer().first, 0, VK_INDEX_TYPE_UINT32);
            ShadowPC pc = {
                .model = entity->getWorldTransform(),
                .viewProj = viewProj,
                .lightPos = glm::vec4(lightPos, radius)
            };
            vkCmdPushConstants(
                commandBuffer,
                shader->pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0,
                sizeof(ShadowPC),
                &pc
            );
            vkCmdDrawIndexed(commandBuffer, model->getIndexCount(), 1, 0, 0, 0);
        }
        for (Entity* child : entity->getChildren()) {
            self(self, child, viewProj);
        }
    };
    
    for (uint32_t face = 0u; face < 6u; ++face) {
        VkRenderingAttachmentInfo depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = bakedShadowFaceViews[face],
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .depthStencil = { 1.0f, 0 } }
        };
        VkRenderingInfo renderInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = { shadowMapSize, shadowMapSize }
            },
            .layerCount = 1,
            .colorAttachmentCount = 0,
            .pColorAttachments = nullptr,
            .pDepthAttachment = &depthAttachment
        };
        renderer->getFpCmdBeginRendering()(commandBuffer, &renderInfo);
        for (Entity* entity : rootEntities) {
            drawStaticEntity(drawStaticEntity, entity, viewProjs[face]);
        }
        renderer->getFpCmdEndRendering()(commandBuffer);
    }
    renderer->transitionImageLayoutInline(
        commandBuffer,
        bakedShadowImage,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        1,
        6
    );
    bakedImageReady = true;
    shadowBaked = true;
}

static std::unordered_set<engine::Entity::EntityType> notShadowTypes = {
    engine::Entity::EntityType::Camera,
    engine::Entity::EntityType::Collider,
    engine::Entity::EntityType::Empty
};

void engine::Light::renderShadowMap(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    if (!hasShadowMap) {
        createShadowMaps(renderer);
    }
    if (!shadowBaked) {
        bakeShadowMap(renderer, commandBuffer);
    }
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("shadow");
    EntityManager* entityManager = renderer->getEntityManager();
    VkBuffer dummySkinningBuffer = entityManager->getDummySkinningBuffer();
    
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
        void* mapped = entity->getUniformBuffersMapped()[bufferIndex];
        if (mapped != nullptr) {
            memcpy(mapped, jointMatrices.data(), jointMatrices.size() * sizeof(glm::mat4));
        }
    };
    
    VkImageLayout previousDepthLayout = shadowImageReady
        ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        : VK_IMAGE_LAYOUT_UNDEFINED;
    renderer->transitionImageLayoutInline(
        commandBuffer,
        shadowDepthImage,
        VK_FORMAT_D32_SFLOAT,
        previousDepthLayout,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        6
    );
    VkImageCopy copyRegion = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 6
        },
        .srcOffset = {0, 0, 0},
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 6
        },
        .dstOffset = {0, 0, 0},
        .extent = {shadowMapSize, shadowMapSize, 1}
    };
    vkCmdCopyImage(
        commandBuffer,
        bakedShadowImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        shadowDepthImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion
    );
    renderer->transitionImageLayoutInline(
        commandBuffer,
        shadowDepthImage,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        1,
        6
    );
    glm::vec3 lightPos = getWorldPosition();
    std::vector<Entity*>& movableEntities = renderer->getEntityManager()->getMovableEntities();
    if (!movableEntities.empty()) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
        VkViewport viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(shadowMapSize),
            .height = static_cast<float>(shadowMapSize),
            .minDepth = 0.0f, 
            .maxDepth = 1.0f
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor = {
            .offset = {0, 0}, 
            .extent = {shadowMapSize, shadowMapSize}
        };
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        auto drawMovableEntity = [&](auto& self, Entity* entity, glm::mat4& viewProj) -> void {
            if (entity->getModel() 
             && !notShadowTypes.contains(entity->getType())
             && entity->getCastShadow()) {
                Model* model = entity->getModel();
                updateJointMatricesUBO(entity);
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
                
                const auto& shadowDS = entity->getShadowDescriptorSets();
                if (!shadowDS.empty()) {
                    const uint32_t dsIndex = std::min<uint32_t>(currentFrame, static_cast<uint32_t>(shadowDS.size() - 1));
                    vkCmdBindDescriptorSets(
                        commandBuffer,
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        shader->pipelineLayout,
                        0,
                        1,
                        &shadowDS[dsIndex],
                        0,
                        nullptr
                    );
                }
                
                ShadowPC pc = {
                    .model = entity->getWorldTransform(),
                    .viewProj = viewProj,
                    .lightPos = glm::vec4(lightPos, radius),
                    .flags = model->hasSkinning() ? 1u : 0u,
                    .pad = {0, 0, 0}
                };
                vkCmdPushConstants(
                    commandBuffer,
                    shader->pipelineLayout,
                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0,
                    sizeof(ShadowPC),
                    &pc
                );
                vkCmdDrawIndexed(commandBuffer, model->getIndexCount(), 1, 0, 0, 0);
            }
            for (Entity* child : entity->getChildren()) {
                self(self, child, viewProj);
            }
        };
        for (int face = 0; face < 6; ++face) {
            VkRenderingAttachmentInfo depthAttachment = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = shadowDepthFaceViews[face],
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .depthStencil = {1.0f, 0} }
            };
            VkRenderingInfo renderInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {
                    .offset = {0, 0},
                    .extent = {shadowMapSize, shadowMapSize}
                },
                .layerCount = 1,
                .colorAttachmentCount = 0,
                .pColorAttachments = nullptr,
                .pDepthAttachment = &depthAttachment
            };
            renderer->getFpCmdBeginRendering()(commandBuffer, &renderInfo);
            for (Entity* entity : movableEntities) {
                drawMovableEntity(drawMovableEntity, entity, viewProjs[face]);
            }
            renderer->getFpCmdEndRendering()(commandBuffer);
        }
    }
    
    renderer->transitionImageLayoutInline(
        commandBuffer,
        shadowDepthImage,
        VK_FORMAT_D32_SFLOAT,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1,
        6
    );
    shadowImageReady = true;
}

void engine::Light::destroyShadowResources(VkDevice device) {
    if (shadowDepthImageView) {
        vkDestroyImageView(device, shadowDepthImageView, nullptr);
        shadowDepthImageView = VK_NULL_HANDLE;
    }
    for(int i=0; i<6; i++) {
        if (shadowDepthFaceViews[i]) {
            vkDestroyImageView(device, shadowDepthFaceViews[i], nullptr);
            shadowDepthFaceViews[i] = VK_NULL_HANDLE;
        }
    }
    if (shadowDepthImage) {
        vkDestroyImage(device, shadowDepthImage, nullptr);
        shadowDepthImage = VK_NULL_HANDLE;
    }
    if (shadowDepthMemory) {
        vkFreeMemory(device, shadowDepthMemory, nullptr);
        shadowDepthMemory = VK_NULL_HANDLE;
    }
    if (bakedShadowImageView) {
        vkDestroyImageView(device, bakedShadowImageView, nullptr);
        bakedShadowImageView = VK_NULL_HANDLE;
    }
    for(int i=0; i<6; i++) {
        if (bakedShadowFaceViews[i]) {
            vkDestroyImageView(device, bakedShadowFaceViews[i], nullptr);
            bakedShadowFaceViews[i] = VK_NULL_HANDLE;
        }
    }
    if (bakedShadowImage) {
        vkDestroyImage(device, bakedShadowImage, nullptr);
        bakedShadowImage = VK_NULL_HANDLE;
    }
    if (bakedShadowMemory) {
        vkFreeMemory(device, bakedShadowMemory, nullptr);
        bakedShadowMemory = VK_NULL_HANDLE;
    }
    
    hasShadowMap = false;
    shadowImageReady = false;
    shadowBaked = false;
    bakedImageReady = false;
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

engine::LightManager::LightManager(engine::Renderer* renderer) : renderer(renderer) {
    renderer->registerLightManager(this);
}

engine::LightManager::~LightManager() {
    clear();
    VkDevice device = renderer->getDevice();
    for (size_t i = 0; i < lightBuffersMapped.size(); ++i) {
        if (lightBuffersMapped[i] != nullptr && i < lightsBuffersMemory.size() && lightsBuffersMemory[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, lightsBuffersMemory[i]);
            lightBuffersMapped[i] = nullptr;
        }
    }
    for (size_t i = 0; i < lightsBuffers.size(); ++i) {
        if (lightsBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, lightsBuffers[i], nullptr);
        }
        if (i < lightsBuffersMemory.size() && lightsBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, lightsBuffersMemory[i], nullptr);
        }
    }
    lightsBuffers.clear();
    lightsBuffersMemory.clear();
    lightBuffersMapped.clear();
}

void engine::LightManager::addLight(const std::string& name, const glm::mat4& transform, const glm::vec3& color, float intensity, float radius) {
    lights.emplace_back(this, name, transform, color, intensity, radius);
    Light& newLight = lights.back();
    newLight.updateLightIdx(lights.size() - 1);
    newLight.createShadowMaps(renderer);
    renderer->createComputeDescriptorSets();
    markLightsDirty();
}

void engine::LightManager::unregisterLight(uint32_t lightIdx) {
    if (lightIdx >= lights.size()) {
        return;
    }
    Light& light = lights[lightIdx];
    if (light.shadowMapReady()) {
        light.destroyShadowResources(renderer->getDevice());
    }
    lights.erase(lights.begin() + lightIdx);
    for (size_t i = lightIdx; i < lights.size(); ++i) {
        lights[i].updateLightIdx(static_cast<uint32_t>(i));
    }
    renderer->createComputeDescriptorSets();
    markLightsDirty();
}

void engine::LightManager::clear() {
    for (Light& light : lights) {
        if (light.shadowMapReady()) {
            light.destroyShadowResources(renderer->getDevice());
        }
    }
    lights.clear();
    markLightsDirty();
}

void engine::LightManager::markLightsDirty() {
    const size_t frames = static_cast<size_t>(renderer->getFramesInFlight());
    if (lightsDirty.size() != frames) {
        lightsDirty.assign(frames, 1u);
        return;
    }
    for (size_t i = 0; i < frames; ++i) {
        lightsDirty[i] = 1u;
    }
}

void engine::LightManager::createLightsUBO() {
    const size_t frames = static_cast<size_t>(renderer->getFramesInFlight());
    lightsBuffers.resize(frames, VK_NULL_HANDLE);
    lightsBuffersMemory.resize(frames, VK_NULL_HANDLE);
    lightBuffersMapped.resize(frames, nullptr);
    lightsDirty.assign(frames, 1u);
    for (size_t frame = 0; frame < frames; ++frame) {
        std::tie(lightsBuffers[frame], lightsBuffersMemory[frame]) = renderer->createBuffer(
            sizeof(LightsUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vkMapMemory(renderer->getDevice(), lightsBuffersMemory[frame], 0, sizeof(LightsUBO), 0, &lightBuffersMapped[frame]);
    }
}

void engine::LightManager::updateLightsUBO(uint32_t frameIndex) {
    if (lightsBuffers.size() < static_cast<size_t>(renderer->getFramesInFlight())) {
        createLightsUBO();
    }
    if (frameIndex >= lightsBuffers.size() || lightsBuffers[frameIndex] == VK_NULL_HANDLE) {
        std::cout << std::format("Warning: Lights UBO buffer unavailable for frame {}. Skipping lights update.\n", frameIndex);
        return;
    }
    if (lightsDirty.size() < lightsBuffers.size()) {
        lightsDirty.assign(lightsBuffers.size(), 1u);
    }
    if (lightsDirty[frameIndex] == 0u) {
        return;
    }
    LightsUBO* gpuData = static_cast<LightsUBO*>(lightBuffersMapped[frameIndex]);
    std::vector<Light>& lights = getLights();
    size_t count = std::min(lights.size(), static_cast<size_t>(64));

    PointLight defaultLight = {};
    defaultLight.positionRadius.w = 1.0f;
    defaultLight.shadowData = glm::uvec4(0xFFFFFFFFu, 0u, 0u, 0u);
    for (size_t i = 0; i < 64; ++i) {
        gpuData->pointLights[i] = defaultLight;
    }

    std::array<PointLight, 64> deferredLights{};
    size_t deferredCount = 0;
    uint32_t shadowLayerCount = 0;

    for (size_t i = 0; i < count; ++i) {
        PointLight pointLight = lights[i].getPointLightData();
        const uint32_t hasShadow = pointLight.shadowData.y;

        if (hasShadow != 0 && shadowLayerCount < 64u) {
            pointLight.shadowData.x = shadowLayerCount;
            gpuData->pointLights[shadowLayerCount] = pointLight;
            ++shadowLayerCount;
        } else {
            if (hasShadow != 0) {
                pointLight.shadowData = glm::uvec4(0xFFFFFFFFu, 0u, 0u, 0u);
            }
            deferredLights[deferredCount++] = pointLight;
        }
    }

    uint32_t writeIndex = shadowLayerCount;
    for (size_t i = 0; i < deferredCount; ++i) {
        if (writeIndex >= 64u) {
            break;
        }
        gpuData->pointLights[writeIndex++] = deferredLights[i];
    }

    gpuData->numPointLights = glm::uvec4(writeIndex, static_cast<uint32_t>(count), 0, 0);
    lightsDirty[frameIndex] = 0u;
}

void engine::LightManager::createAllShadowMaps() {
    vkDeviceWaitIdle(renderer->getDevice());
    std::vector<Light>& lights = getLights();
    for (auto& light : lights) {
        light.createShadowMaps(renderer, true);
    }
}

void engine::LightManager::renderShadows(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    std::vector<Light>& lights = getLights();
    for (auto& light : lights) {
        light.renderShadowMap(renderer, commandBuffer, currentFrame);
    }
}

