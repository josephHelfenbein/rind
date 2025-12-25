#include <engine/Light.h>

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

engine::PointLight engine::Light::getPointLightData() {
    glm::vec3 worldPos = getWorldPosition();
    uint32_t shadowIdx = 0xFFFFFFFF;
    if (hasShadowMap) {
        auto& lights = getEntityManager()->getLights();
        for (uint32_t i = 0; i < lights.size(); ++i) {
            if (lights[i] == this) {
                shadowIdx = i;
                break;
            }
        }
    }
    PointLight pl = {
        .positionRadius = glm::vec4(worldPos, radius),
        .colorIntensity = glm::vec4(color, intensity),
        .lightViewProj = {},
        .shadowParams = glm::vec4(0.005f, radius, 0.1f, 1.0f), // bias, far, near, strength
        .shadowData = glm::uvec4(shadowIdx, hasShadowMap ? 1 : 0, 0, 0)
    };
    return pl;
}

void engine::Light::createShadowMaps(engine::Renderer* renderer) {
    if (hasShadowMap) return;
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
    glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius);
    struct CubeFace {
        glm::vec3 dir;
        glm::vec3 up;
    };
    CubeFace faces[6] = {
        { glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // +X
        { glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // -X
        { glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f) }, // +Y
        { glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f) }, // -Y
        { glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // +Z
        { glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f) }  // -Z
    };
    glm::mat4 viewProjs[6];
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
        .offset = {0, 0}, 
        .extent = {shadowMapSize, shadowMapSize}
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    std::function<void(Entity*, glm::mat4&)> drawStaticEntity = [&](Entity* entity, glm::mat4& viewProj) {
        if (!entity->getIsMovable() && entity->getModel() && entity->getShader() == "gbuffer") {
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
            drawStaticEntity(child, viewProj);
        }
    };
    
    for (int face = 0; face < 6; ++face) {
        VkRenderingAttachmentInfo depthAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = bakedShadowFaceViews[face],
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
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
        for (Entity* entity : rootEntities) {
            drawStaticEntity(entity, viewProjs[face]);
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

void engine::Light::renderShadowMap(Renderer* renderer, VkCommandBuffer commandBuffer) {
    if (!hasShadowMap) {
        createShadowMaps(renderer);
    }
    if (!shadowBaked) {
        bakeShadowMap(renderer, commandBuffer);
    }
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("shadow");
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
    glm::mat4 shadowProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius);
    struct CubeFace {
        glm::vec3 dir;
        glm::vec3 up;
    };
    CubeFace faces[6] = {
        { glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // +X
        { glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // -X
        { glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f) }, // +Y
        { glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f) }, // -Y
        { glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f) }, // +Z
        { glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f) }  // -Z
    };
    glm::mat4 viewProjs[6];
    for (int i = 0; i < 6; ++i) {
        viewProjs[i] = shadowProj * glm::lookAt(lightPos, lightPos + faces[i].dir, faces[i].up);
    }
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
        std::function<void(Entity*, glm::mat4&)> drawMovableEntity = [&](Entity* entity, glm::mat4& viewProj) {
            if (entity->getModel() && entity->getShader() == "gbuffer") {
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
                drawMovableEntity(child, viewProj);
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
                drawMovableEntity(entity, viewProjs[face]);
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
    if (shadowDepthImageView) vkDestroyImageView(device, shadowDepthImageView, nullptr);
    for(int i=0; i<6; i++) {
        if (shadowDepthFaceViews[i]) vkDestroyImageView(device, shadowDepthFaceViews[i], nullptr);
    }
    if (shadowDepthImage) vkDestroyImage(device, shadowDepthImage, nullptr);
    if (shadowDepthMemory) vkFreeMemory(device, shadowDepthMemory, nullptr);
    if (bakedShadowImageView) vkDestroyImageView(device, bakedShadowImageView, nullptr);
    for(int i=0; i<6; i++) {
        if (bakedShadowFaceViews[i]) vkDestroyImageView(device, bakedShadowFaceViews[i], nullptr);
    }
    if (bakedShadowImage) vkDestroyImage(device, bakedShadowImage, nullptr);
    if (bakedShadowMemory) vkFreeMemory(device, bakedShadowMemory, nullptr);
    
    hasShadowMap = false;
    shadowImageReady = false;
    shadowBaked = false;
    bakedImageReady = false;
}

void engine::Light::setShadowMapSize(uint32_t size) {
    if (size == shadowMapSize) return;
    shadowMapSize = size;
    
    Renderer* renderer = getEntityManager()->getRenderer();
    if (hasShadowMap) {
        destroyShadowResources(renderer->getDevice());
    }
    createShadowMaps(renderer);
}