#include <engine/LightManager.h>

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <engine/SettingsManager.h>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

engine::Light::Light(
    LightManager* lightManager,
    LightHandle handle,
    const std::string& name,
    const glm::mat4& transform,
    const glm::vec3& color,
    float intensity,
    float radius
) : color(color), intensity(intensity), radius(radius), shadowProj(glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius)), transform(transform), handle(handle), lightManager(lightManager) {}

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
    if (hasShadowMap) {
        updateShadowMatrices();
    }
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

void engine::Light::setTransform(const glm::mat4& transform) {
    this->transform = transform;
    if (hasShadowMap) {
        updateShadowMatrices();
    }
    if (lightManager) {
        lightManager->markLightsDirty();
    }
}

void engine::Light::updateShadowMatrices() {
    glm::vec3 lightPos = getWorldPosition();
    for (int i = 0; i < 6; ++i) {
        glm::mat4 faceView = glm::lookAt(lightPos, lightPos + faces[i].dir, faces[i].up);
        viewProjs[i] = shadowProj * faceView;
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
     // 256, 512, 1024, 2048
    shadowMapSize = static_cast<uint32_t>(pow(2, 8 + std::min(static_cast<int>(settingsValue), 3)));
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    const uint32_t framesInFlight = std::max(1u, renderer->getFramesInFlight());
    shadowDepthImages.assign(framesInFlight, VK_NULL_HANDLE);
    shadowDepthMemories.assign(framesInFlight, VK_NULL_HANDLE);
    shadowDepthImageViews.assign(framesInFlight, VK_NULL_HANDLE);
    shadowDepthFaceViews.assign(framesInFlight, {});
    shadowDepthArrayViews.assign(framesInFlight, VK_NULL_HANDLE);
    shadowImageReady.assign(framesInFlight, 0u);

    for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
        std::tie(shadowDepthImages[frame], shadowDepthMemories[frame]) = renderer->createImage(
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
        shadowDepthImageViews[frame] = renderer->createImageView(
            shadowDepthImages[frame],
            depthFormat,
            VK_IMAGE_ASPECT_DEPTH_BIT,
            1,
            VK_IMAGE_VIEW_TYPE_CUBE,
            6
        );
        for (uint32_t i = 0; i < 6; ++i) {
            VkImageViewCreateInfo viewInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = shadowDepthImages[frame],
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
            vkCreateImageView(renderer->getDevice(), &viewInfo, nullptr, &shadowDepthFaceViews[frame][i]);
        }
        VkImageViewCreateInfo arrayViewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = shadowDepthImages[frame],
            .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            .format = depthFormat,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 6
            }
        };
        vkCreateImageView(renderer->getDevice(), &arrayViewInfo, nullptr, &shadowDepthArrayViews[frame]);
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
    VkImageViewCreateInfo bakedArrayViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = bakedShadowImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = depthFormat,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    vkCreateImageView(renderer->getDevice(), &bakedArrayViewInfo, nullptr, &bakedShadowArrayView);

    updateShadowMatrices();

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
    
    std::vector<Entity*>& rootEntities = renderer->getEntityManager()->getRootEntities();
    VkBuffer dummySkinningBuffer = renderer->getEntityManager()->getDummySkinningBuffer();
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

    const uint32_t lightIndex = lightIdx;
    auto drawStaticEntity = [&](auto& self, Entity* entity) -> void {
        if (!entity->getIsMovable()
         && entity->getModel()
         && entity->getType() == Entity::EntityType::Static
         && entity->getCastShadow()
         && intersectsShadowRange(entity->getModel()->getAABB(), entity->getWorldTransform())) {
            Model* model = entity->getModel();
            const auto& shadowDS = entity->getShadowDescriptorSets();
            if (shadowDS.empty()) {
                for (Entity* child : entity->getChildren()) {
                    self(self, child);
                }
                return;
            }
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
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                shader->pipelineLayout,
                0,
                1,
                &shadowDS[0],
                0,
                nullptr
            );
            ShadowPC pc = {
                .model = entity->getWorldTransform(),
                .lightIndex = lightIndex,
                .flags = model->hasSkinning() ? 1u : 0u
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
            self(self, child);
        }
    };

    VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = bakedShadowArrayView,
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
        .viewMask = 0x3Fu,
        .colorAttachmentCount = 0,
        .pColorAttachments = nullptr,
        .pDepthAttachment = &depthAttachment
    };
    renderer->getFpCmdBeginRendering()(commandBuffer, &renderInfo);
    for (Entity* entity : rootEntities) {
        drawStaticEntity(drawStaticEntity, entity);
    }
    renderer->getFpCmdEndRendering()(commandBuffer);
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
    
    const uint32_t frameIdx = shadowDepthImages.empty()
        ? 0u
        : (currentFrame % static_cast<uint32_t>(shadowDepthImages.size()));
    VkImage shadowDepthImage = shadowDepthImages.empty() ? VK_NULL_HANDLE : shadowDepthImages[frameIdx];
    if (shadowDepthImage == VK_NULL_HANDLE) {
        return;
    }

    const bool frameShadowReady = frameIdx < shadowImageReady.size() && shadowImageReady[frameIdx] != 0u;
    VkImageLayout previousDepthLayout = frameShadowReady
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
        const uint32_t lightIndex = lightIdx;
        auto drawMovableEntity = [&](auto& self, Entity* entity) -> void {
            if (entity->getModel()
             && !notShadowTypes.contains(entity->getType())
             && entity->getCastShadow()
             && intersectsShadowRange(entity->getModel()->getAABB(), entity->getWorldTransform())) {
                Model* model = entity->getModel();
                const auto& shadowDS = entity->getShadowDescriptorSets();
                if (shadowDS.empty()) {
                    for (Entity* child : entity->getChildren()) {
                        self(self, child);
                    }
                    return;
                }
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

                ShadowPC pc = {
                    .model = entity->getWorldTransform(),
                    .lightIndex = lightIndex,
                    .flags = model->hasSkinning() ? 1u : 0u
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
                self(self, child);
            }
        };
        VkRenderingAttachmentInfo depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = shadowDepthArrayViews[frameIdx],
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
            .viewMask = 0x3Fu,
            .colorAttachmentCount = 0,
            .pColorAttachments = nullptr,
            .pDepthAttachment = &depthAttachment
        };
        renderer->getFpCmdBeginRendering()(commandBuffer, &renderInfo);
        for (Entity* entity : movableEntities) {
            drawMovableEntity(drawMovableEntity, entity);
        }
        renderer->getFpCmdEndRendering()(commandBuffer);
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
    if (frameIdx < shadowImageReady.size()) {
        shadowImageReady[frameIdx] = 1u;
    }
}

engine::Light::ShadowResources engine::Light::takeShadowResources() {
    ShadowResources res;
    for (size_t frame = 0; frame < shadowDepthImageViews.size(); ++frame) {
        if (shadowDepthImageViews[frame]) {
            res.views.push_back(shadowDepthImageViews[frame]);
        }
        if (frame < shadowDepthFaceViews.size()) {
            for (int i = 0; i < 6; ++i) {
                if (shadowDepthFaceViews[frame][i]) {
                    res.views.push_back(shadowDepthFaceViews[frame][i]);
                }
            }
        }
        if (frame < shadowDepthArrayViews.size() && shadowDepthArrayViews[frame]) {
            res.views.push_back(shadowDepthArrayViews[frame]);
        }
        if (frame < shadowDepthImages.size() && shadowDepthImages[frame]) {
            res.images.push_back(shadowDepthImages[frame]);
        }
        if (frame < shadowDepthMemories.size() && shadowDepthMemories[frame]) {
            res.memories.push_back(shadowDepthMemories[frame]);
        }
    }
    shadowDepthImageViews.clear();
    shadowDepthFaceViews.clear();
    shadowDepthArrayViews.clear();
    shadowDepthImages.clear();
    shadowDepthMemories.clear();

    if (bakedShadowImageView) {
        res.views.push_back(bakedShadowImageView);
        bakedShadowImageView = VK_NULL_HANDLE;
    }
    for (int i = 0; i < 6; ++i) {
        if (bakedShadowFaceViews[i]) {
            res.views.push_back(bakedShadowFaceViews[i]);
            bakedShadowFaceViews[i] = VK_NULL_HANDLE;
        }
    }
    if (bakedShadowArrayView) {
        res.views.push_back(bakedShadowArrayView);
        bakedShadowArrayView = VK_NULL_HANDLE;
    }
    if (bakedShadowImage) {
        res.images.push_back(bakedShadowImage);
        bakedShadowImage = VK_NULL_HANDLE;
    }
    if (bakedShadowMemory) {
        res.memories.push_back(bakedShadowMemory);
        bakedShadowMemory = VK_NULL_HANDLE;
    }

    hasShadowMap = false;
    shadowImageReady.clear();
    shadowBaked = false;
    bakedImageReady = false;
    if (lightManager) {
        lightManager->markLightsDirty();
    }
    return res;
}

void engine::Light::freeShadowResources(VkDevice device, ShadowResources& resources) {
    for (VkImageView view : resources.views) {
        if (view) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    for (VkImage image : resources.images) {
        if (image) {
            vkDestroyImage(device, image, nullptr);
        }
    }
    for (VkDeviceMemory memory : resources.memories) {
        if (memory) {
            vkFreeMemory(device, memory, nullptr);
        }
    }
    resources.views.clear();
    resources.images.clear();
    resources.memories.clear();
}

void engine::Light::destroyShadowResources(VkDevice device) {
    ShadowResources res = takeShadowResources();
    freeShadowResources(device, res);
}

void engine::Light::fillShadowLightEntry(ShadowLightEntry& entry) const {
    for (uint32_t i = 0; i < 6; ++i) {
        entry.viewProjs[i] = viewProjs[i];
    }
    entry.lightPosRadius = glm::vec4(getWorldPosition(), radius);
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
    for (size_t i = 0; i < shadowLightsMapped.size(); ++i) {
        if (shadowLightsMapped[i] != nullptr && i < shadowLightsMemories.size() && shadowLightsMemories[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, shadowLightsMemories[i]);
            shadowLightsMapped[i] = nullptr;
        }
    }
    for (size_t i = 0; i < shadowLightsBuffers.size(); ++i) {
        if (shadowLightsBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, shadowLightsBuffers[i], nullptr);
        }
        if (i < shadowLightsMemories.size() && shadowLightsMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, shadowLightsMemories[i], nullptr);
        }
    }
    shadowLightsBuffers.clear();
    shadowLightsMemories.clear();
    shadowLightsMapped.clear();
}

engine::LightHandle engine::LightManager::addLight(const std::string& name, const glm::mat4& transform, const glm::vec3& color, float intensity, float radius) {
    const LightHandle handle = nextHandle++;
    lights.push_back(std::make_unique<Light>(this, handle, name, transform, color, intensity, radius));
    Light* light = lights.back().get();
    lightLookup[handle] = light;
    light->createShadowMaps(renderer);
    reorderLights();
    vkDeviceWaitIdle(renderer->getDevice());
    renderer->createComputeDescriptorSets();
    markLightsDirty();
    return handle;
}

void engine::LightManager::unregisterLight(LightHandle handle) {
    auto lookupIt = lightLookup.find(handle);
    if (lookupIt == lightLookup.end()) {
        return;
    }
    Light* light = lookupIt->second;
    auto storageIt = std::find_if(lights.begin(), lights.end(),
        [light](const std::unique_ptr<Light>& l) { return l.get() == light; });
    if (storageIt == lights.end()) {
        lightLookup.erase(lookupIt);
        return;
    }
    if (light->shadowMapReady()) {
        scheduleShadowResourceDestroy(light->takeShadowResources());
    }
    lightLookup.erase(lookupIt);
    lights.erase(storageIt);
    reorderLights();
    vkDeviceWaitIdle(renderer->getDevice());
    renderer->createComputeDescriptorSets();
    markLightsDirty();
}

engine::Light* engine::LightManager::getLight(LightHandle handle) {
    auto it = lightLookup.find(handle);
    return it == lightLookup.end() ? nullptr : it->second;
}

void engine::LightManager::clear() {
    flushDeferredDestroys();
    for (auto& light : lights) {
        if (light->shadowMapReady()) {
            light->destroyShadowResources(renderer->getDevice());
        }
    }
    lights.clear();
    lightLookup.clear();
    markLightsDirty();
}

void engine::LightManager::scheduleShadowResourceDestroy(Light::ShadowResources&& resources) {
    if (resources.views.empty() && resources.images.empty() && resources.memories.empty()) {
        return;
    }
    const uint32_t lifetime = renderer->getFramesInFlight() + 1u;
    deferredDestroys.push_back({ std::move(resources), lifetime });
}

void engine::LightManager::processDeferredDestroys() {
    if (deferredDestroys.empty()) {
        return;
    }
    VkDevice device = renderer->getDevice();
    for (auto it = deferredDestroys.begin(); it != deferredDestroys.end();) {
        if (it->framesRemaining > 0u) {
            --it->framesRemaining;
        }
        if (it->framesRemaining == 0u) {
            Light::freeShadowResources(device, it->resources);
            it = deferredDestroys.erase(it);
        } else {
            ++it;
        }
    }
}

void engine::LightManager::flushDeferredDestroys() {
    VkDevice device = renderer->getDevice();
    for (auto& pending : deferredDestroys) {
        Light::freeShadowResources(device, pending.resources);
    }
    deferredDestroys.clear();
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

void engine::LightManager::createShadowLightsBuffers() {
    const size_t frames = static_cast<size_t>(renderer->getFramesInFlight());
    if (shadowLightsBuffers.size() == frames) {
        return;
    }
    VkDevice device = renderer->getDevice();
    for (size_t i = 0; i < shadowLightsMapped.size(); ++i) {
        if (shadowLightsMapped[i] != nullptr && i < shadowLightsMemories.size() && shadowLightsMemories[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, shadowLightsMemories[i]);
            shadowLightsMapped[i] = nullptr;
        }
    }
    for (size_t i = 0; i < shadowLightsBuffers.size(); ++i) {
        if (shadowLightsBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, shadowLightsBuffers[i], nullptr);
        }
        if (i < shadowLightsMemories.size() && shadowLightsMemories[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, shadowLightsMemories[i], nullptr);
        }
    }
    shadowLightsBuffers.assign(frames, VK_NULL_HANDLE);
    shadowLightsMemories.assign(frames, VK_NULL_HANDLE);
    shadowLightsMapped.assign(frames, nullptr);
    for (size_t frame = 0; frame < frames; ++frame) {
        std::tie(shadowLightsBuffers[frame], shadowLightsMemories[frame]) = renderer->createBuffer(
            sizeof(ShadowLightsSSBO),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vkMapMemory(device, shadowLightsMemories[frame], 0, sizeof(ShadowLightsSSBO), 0, &shadowLightsMapped[frame]);
    }
}

void engine::LightManager::updateShadowLightsBuffer(uint32_t frameIndex) {
    if (shadowLightsBuffers.size() < static_cast<size_t>(renderer->getFramesInFlight())) {
        createShadowLightsBuffers();
    }
    if (frameIndex >= shadowLightsBuffers.size() || shadowLightsMapped[frameIndex] == nullptr) {
        return;
    }
    ShadowLightsSSBO* gpuData = static_cast<ShadowLightsSSBO*>(shadowLightsMapped[frameIndex]);
    const size_t count = std::min(lights.size(), static_cast<size_t>(kMaxPointLights));
    for (size_t i = 0; i < count; ++i) {
        lights[i]->fillShadowLightEntry(gpuData->lights[i]);
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
    auto& lights = getLights();
    size_t count = std::min(lights.size(), static_cast<size_t>(kMaxPointLights));

    for (size_t i = 0; i < count; ++i) {
        gpuData->pointLights[i] = lights[i]->getPointLightData();
    }

    gpuData->numPointLights = glm::uvec4(static_cast<uint32_t>(count), 0, 0, 0);
    lightsDirty[frameIndex] = 0u;
}

void engine::LightManager::reorderLights() {
    std::stable_partition(lights.begin(), lights.end(), [](const std::unique_ptr<Light>& l) {
        return l->shadowMapReady();
    });
    for (size_t i = 0; i < lights.size(); ++i) {
        lights[i]->updateLightIdx(static_cast<uint32_t>(i));
    }
}

void engine::LightManager::createAllShadowMaps() {
    vkDeviceWaitIdle(renderer->getDevice());
    for (auto& light : lights) {
        light->createShadowMaps(renderer, true);
    }
}

void engine::LightManager::renderShadows(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    processDeferredDestroys();
    createShadowLightsBuffers();
    updateShadowLightsBuffer(currentFrame);
    for (auto& light : lights) {
        light->renderShadowMap(renderer, commandBuffer, currentFrame);
    }
}

