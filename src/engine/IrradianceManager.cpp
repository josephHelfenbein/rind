#include <engine/IrradianceManager.h>
#include <engine/ShaderManager.h>
#include <engine/ParticleManager.h>
#include <engine/Camera.h>
#include <engine/TextureManager.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

engine::IrradianceProbe::IrradianceProbe(
    IrradianceManager* irradianceManager,
    const std::string& name,
    const glm::mat4& transform,
    float radius
) : irradianceManager(irradianceManager), transform(transform), radius(radius) {}

void engine::IrradianceProbe::destroy() {
    VkDevice device = irradianceManager->getRenderer()->getDevice();
    for (uint32_t i = 0; i < 6; ++i) {
        if (bakedCubemapFaceViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, bakedCubemapFaceViews[i], nullptr);
        }
    }
    for (size_t frame = 0; frame < dynamicCubemapFaceViews.size(); ++frame) {
        for (uint32_t i = 0; i < 6; ++i) {
            if (dynamicCubemapFaceViews[frame][i] != VK_NULL_HANDLE) {
                vkDestroyImageView(device, dynamicCubemapFaceViews[frame][i], nullptr);
            }
        }
    }
    if (bakedCubemapView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, bakedCubemapView, nullptr);
    }
    if (bakedCubemapImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, bakedCubemapImage, nullptr);
    }
    if (bakedCubemapMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, bakedCubemapMemory, nullptr);
    }
    for (VkImageView dynamicView : dynamicCubemapViews) {
        if (dynamicView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, dynamicView, nullptr);
        }
    }
    for (VkImageView storageView : dynamicCubemapStorageViews) {
        if (storageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, storageView, nullptr);
        }
    }
    for (VkImage dynamicImage : dynamicCubemapImages) {
        if (dynamicImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, dynamicImage, nullptr);
        }
    }
    for (VkDeviceMemory dynamicMemory : dynamicCubemapMemories) {
        if (dynamicMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, dynamicMemory, nullptr);
        }
    }
    dynamicCubemapFaceViews.clear();
    dynamicCubemapViews.clear();
    dynamicCubemapStorageViews.clear();
    dynamicCubemapImages.clear();
    dynamicCubemapMemories.clear();
    if (cubemapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, cubemapSampler, nullptr);
    }
}

void engine::IrradianceProbe::createCubemaps(Renderer* renderer) {
    if (hasImageMap) {
        return;
    }
    
    bakedImageReady = false;
    dynamicImageReady.clear();
    dynamicCubemapDirty.clear();
    
    std::tie(bakedCubemapImage, bakedCubemapMemory) = renderer->createImage(
        cubemapSize, cubemapSize,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        6,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    );
    bakedCubemapView = renderer->createImageView(
        bakedCubemapImage,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6
    );
    for (uint32_t i = 0; i < 6; ++i) {
        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = bakedCubemapImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = i,
                .layerCount = 1
            }
        };
        vkCreateImageView(renderer->getDevice(), &viewInfo, nullptr, &bakedCubemapFaceViews[i]);
    }
    
    const uint32_t framesInFlight = std::max(1u, renderer->getFramesInFlight());
    dynamicCubemapImages.assign(framesInFlight, VK_NULL_HANDLE);
    dynamicCubemapViews.assign(framesInFlight, VK_NULL_HANDLE);
    dynamicCubemapStorageViews.assign(framesInFlight, VK_NULL_HANDLE);
    dynamicCubemapMemories.assign(framesInFlight, VK_NULL_HANDLE);
    dynamicCubemapFaceViews.assign(framesInFlight, {});
    dynamicImageReady.assign(framesInFlight, 0u);
    dynamicCubemapDirty.assign(framesInFlight, 0u);

    for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
        std::tie(dynamicCubemapImages[frame], dynamicCubemapMemories[frame]) = renderer->createImage(
            cubemapSize, cubemapSize,
            1,
            VK_SAMPLE_COUNT_1_BIT,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            6,
            VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
        );
        dynamicCubemapViews[frame] = renderer->createImageView(
            dynamicCubemapImages[frame],
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            VK_IMAGE_VIEW_TYPE_CUBE,
            6
        );
        dynamicCubemapStorageViews[frame] = renderer->createImageView(
            dynamicCubemapImages[frame],
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            VK_IMAGE_VIEW_TYPE_2D_ARRAY,
            6
        );
        for (uint32_t i = 0; i < 6; ++i) {
            VkImageViewCreateInfo dynamicViewInfo = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .image = dynamicCubemapImages[frame],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = VK_FORMAT_R16G16B16A16_SFLOAT,
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = i,
                    .layerCount = 1
                }
            };
            vkCreateImageView(renderer->getDevice(), &dynamicViewInfo, nullptr, &dynamicCubemapFaceViews[frame][i]);
        }
    }
    
    cubemapSampler = renderer->createTextureSampler(
        VK_FILTER_LINEAR,
        VK_FILTER_LINEAR,
        VK_SAMPLER_MIPMAP_MODE_LINEAR,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        0.0f,
        VK_FALSE,
        1.0f,
        VK_FALSE,
        VK_COMPARE_OP_ALWAYS,
        0.0f,
        0.0f,
        VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        VK_FALSE
    );
    
    hasImageMap = true;
}

void engine::IrradianceProbe::bakeCubemap(Renderer* renderer, VkCommandBuffer commandBuffer) {
    if (bakedImageReady) return; // only bake once
    if (!hasImageMap) {
        createCubemaps(renderer);
    }
    
    GraphicsShader* irradianceBakeShader = renderer->getShaderManager()->getGraphicsShader("irradiance");
    VkImageLayout previousBakedLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    renderer->transitionImageLayoutInline(
        commandBuffer,
        bakedCubemapImage,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        previousBakedLayout,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        1,
        6
    );
    
    glm::vec3 probePos = glm::vec3(transform[3]);
    glm::mat4 cubeProj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, radius);
    for (int i = 0; i < 6; ++i) {
        viewProjs[i] = cubeProj * glm::lookAt(probePos, probePos + faces[i].dir, faces[i].up);
    }
    std::vector<Entity*>& rootEntities = renderer->getEntityManager()->getRootEntities();
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, irradianceBakeShader->pipeline);
    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = static_cast<float>(cubemapSize),
        .height = static_cast<float>(cubemapSize),
        .minDepth = 0.0f,
        .maxDepth = 1.0f
    };
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    VkRect2D scissor = {
        .offset = { 0, 0 },
        .extent = { cubemapSize, cubemapSize }
    };
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    int entitiesUsed = 0;
    auto drawStaticEntity = [&](auto& self, Entity* entity, glm::mat4& viewProj) -> void {
        if (!entity->getIsMovable()
         && entity->getModel()
         && entity->getType() == Entity::EntityType::Static
         && !entity->getDescriptorSets().empty()
        ) {
            Model* model = entity->getModel();
            VkBuffer vertexBuffers[] = { model->getVertexBuffer().first };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
            vkCmdBindIndexBuffer(commandBuffer, model->getIndexBuffer().first, 0, VK_INDEX_TYPE_UINT32);
            IrradianceBakePC pc = {
                .model = entity->getWorldTransform(),
                .viewProj = viewProj
            };
            vkCmdPushConstants(
                commandBuffer,
                irradianceBakeShader->pipelineLayout,
                VK_SHADER_STAGE_VERTEX_BIT,
                0,
                sizeof(IrradianceBakePC),
                &pc
            );
            const std::vector<VkDescriptorSet>& descriptorSets = entity->getDescriptorSets();
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                irradianceBakeShader->pipelineLayout,
                0,
                1,
                &descriptorSets[0],
                0,
                nullptr
            );
            vkCmdDrawIndexed(commandBuffer, model->getIndexCount(), 1, 0, 0, 0);
            entitiesUsed++;
        }
        for (Entity* child : entity->getChildren()) {
            self(self, child, viewProj);
        }
    };
    for (uint32_t face = 0u; face < 6u; ++face) {
        VkRenderingAttachmentInfo colorAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = bakedCubemapFaceViews[face],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = { .color = { 0.0f, 0.0f, 0.0f, 1.0f } }
        };
        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = { 0, 0 },
                .extent = { cubemapSize, cubemapSize }
            },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachment,
        };
        renderer->getFpCmdBeginRendering()(commandBuffer, &renderingInfo);
        for (Entity* rootEntity : rootEntities) {
            drawStaticEntity(drawStaticEntity, rootEntity, viewProjs[face]);
        }
        renderer->getFpCmdEndRendering()(commandBuffer);
    }
    
    VkImageMemoryBarrier imageBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = bakedCubemapImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &imageBarrier
    );
    
    bakedImageReady = true;
}

void engine::IrradianceProbe::copyBakedToDynamic(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (!bakedImageReady) return;

    if (dynamicCubemapImages.empty()) {
        return;
    }
    const uint32_t frameIdx = frameIndex % static_cast<uint32_t>(dynamicCubemapImages.size());
    VkImage dynamicCubemapImage = dynamicCubemapImages[frameIdx];
    if (dynamicCubemapImage == VK_NULL_HANDLE) {
        return;
    }
    const bool frameDynamicReady = frameIdx < dynamicImageReady.size() && dynamicImageReady[frameIdx] != 0u;
    
    VkImageLayout dynamicOldLayout = frameDynamicReady ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags dynamicSrcAccess = frameDynamicReady ? VK_ACCESS_SHADER_READ_BIT : 0;
    VkPipelineStageFlags srcStage = frameDynamicReady
        ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT
        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    
    VkImageMemoryBarrier bakedToTransferSrc = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = bakedCubemapImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    
    VkImageMemoryBarrier dynamicToTransferDst = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = dynamicSrcAccess,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = dynamicOldLayout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dynamicCubemapImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    
    std::array<VkImageMemoryBarrier, 2> preBarriers = { bakedToTransferSrc, dynamicToTransferDst };
    vkCmdPipelineBarrier(
        commandBuffer,
        srcStage,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(preBarriers.size()), preBarriers.data()
    );
    
    VkImageCopy copyRegion = {
        .srcSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 6
        },
        .srcOffset = { 0, 0, 0 },
        .dstSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 6
        },
        .dstOffset = { 0, 0, 0 },
        .extent = { cubemapSize, cubemapSize, 1 }
    };
    
    vkCmdCopyImage(
        commandBuffer,
        bakedCubemapImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        dynamicCubemapImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &copyRegion
    );
    
    VkImageMemoryBarrier bakedBackToShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = bakedCubemapImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    
    VkImageMemoryBarrier dynamicToShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dynamicCubemapImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    
    std::array<VkImageMemoryBarrier, 2> postBarriers = { bakedBackToShaderRead, dynamicToShaderRead };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(postBarriers.size()), postBarriers.data()
    );
    
    if (frameIdx < dynamicImageReady.size()) {
        dynamicImageReady[frameIdx] = 1u;
    }
}

bool engine::IrradianceProbe::prepareDynamicCubemapForParticleCompute(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (dynamicCubemapImages.empty() || dynamicCubemapFaceViews.empty()) {
        return false;
    }
    const uint32_t frameIdx = frameIndex % static_cast<uint32_t>(dynamicCubemapImages.size());
    VkImage dynamicCubemapImage = dynamicCubemapImages[frameIdx];
    if (dynamicCubemapImage == VK_NULL_HANDLE || frameIdx >= dynamicCubemapFaceViews.size()) {
        return false;
    }
    if (frameIdx >= dynamicImageReady.size() || dynamicImageReady[frameIdx] == 0u) {
        return false;
    }

    ParticleManager* particleManager = renderer->getParticleManager();
    if (!particleManager) {
        return false;
    }

    const uint32_t particleCount = particleManager->getParticleCount();
    size_t currentParticleCount = static_cast<size_t>(particleCount);
    
    // update if there are particles in the scene, or all particles disappeared
    bool particlesChanged = (currentParticleCount > 0) || (currentParticleCount != lastParticleCount);
    lastParticleCount = currentParticleCount;
    
    if (!particlesChanged) {
        if (frameIdx < dynamicCubemapDirty.size()) {
            dynamicCubemapDirty[frameIdx] = 0u;
        }
        return false;
    }
    
    if (frameIdx < dynamicCubemapDirty.size()) {
        dynamicCubemapDirty[frameIdx] = 1u;
    }

    const bool particlesNowEmpty = (particleCount == 0u);

    VkImageMemoryBarrier toGeneral = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dynamicCubemapImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toGeneral
    );

    if (particlesNowEmpty) {
        // Run one compute/SH update on the frame particles disappear so stale SH does not linger.
        return true;
    }

    return true;
}

void engine::IrradianceProbe::finalizeDynamicCubemapAfterParticleCompute(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (dynamicCubemapImages.empty()) {
        return;
    }
    const uint32_t frameIdx = frameIndex % static_cast<uint32_t>(dynamicCubemapImages.size());
    if (frameIdx >= dynamicCubemapDirty.size() || dynamicCubemapDirty[frameIdx] == 0u) {
        return;
    }
    VkImage dynamicCubemapImage = dynamicCubemapImages[frameIdx];
    if (dynamicCubemapImage == VK_NULL_HANDLE) {
        return;
    }

    VkImageMemoryBarrier toShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = dynamicCubemapImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 6
        }
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toShaderRead
    );
}

VkImageView engine::IrradianceProbe::getDynamicCubemapStorageView(uint32_t frameIndex) const {
    if (dynamicCubemapStorageViews.empty()) {
        return VK_NULL_HANDLE;
    }
    const uint32_t frameIdx = frameIndex % static_cast<uint32_t>(dynamicCubemapStorageViews.size());
    return dynamicCubemapStorageViews[frameIdx];
}

VkImageView engine::IrradianceProbe::getDynamicCubemapView(uint32_t frameIndex) const {
    if (dynamicCubemapViews.empty()) {
        return VK_NULL_HANDLE;
    }
    const uint32_t frameIdx = frameIndex % static_cast<uint32_t>(dynamicCubemapViews.size());
    return dynamicCubemapViews[frameIdx];
}

void engine::IrradianceProbe::renderDynamicCubemap(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame, uint32_t activeProbeLocalIndex, uint32_t activeProbeCount) {
    if (!prepareDynamicCubemapForParticleCompute(renderer, commandBuffer, currentFrame)) {
        return;
    }

    ComputeShader* particleCompute = renderer->getShaderManager()->getComputeShader("particlesimple");
    if (!particleCompute || particleCompute->descriptorSets.empty()) {
        finalizeDynamicCubemapAfterParticleCompute(commandBuffer, currentFrame);
        return;
    }

    const uint32_t dsIndex = std::min(currentFrame, static_cast<uint32_t>(particleCompute->descriptorSets.size() - 1));
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, particleCompute->pipeline);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        particleCompute->pipelineLayout,
        0,
        1,
        &particleCompute->descriptorSets[dsIndex],
        0,
        nullptr
    );

    SimpleParticlePC particlePC = {
        .probePosition = glm::vec4(0.0f),
        .particleSize = 0.1f,
        .particleCount = renderer->getParticleManager() ? renderer->getParticleManager()->getParticleCount() : 0u,
        .cubemapSize = cubemapSize,
        .activeProbeCount = std::max(1u, activeProbeCount),
        .layerBase = activeProbeLocalIndex * 6u,
        .mappingOffset = 0u,
        .pad = 0u
    };
    vkCmdPushConstants(
        commandBuffer,
        particleCompute->pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(SimpleParticlePC),
        &particlePC
    );

    const uint32_t groupX = (cubemapSize + 7u) / 8u;
    const uint32_t groupY = (cubemapSize + 7u) / 8u;
    vkCmdDispatch(commandBuffer, groupX, groupY, 6u);

    finalizeDynamicCubemapAfterParticleCompute(commandBuffer, currentFrame);
}

engine::IrradianceProbeData engine::IrradianceProbe::getProbeData() const {
    IrradianceProbeData data;
    data.position = glm::vec4(glm::vec3(transform[3]), radius);
    for (int i = 0; i < 9; ++i) {
        data.shCoeffs[i] = glm::vec4(shCoeffs[i], 0.0f);
    }
    return data;
}

engine::IrradianceManager::IrradianceManager(Renderer* renderer) : renderer(renderer) {
    renderer->registerIrradianceManager(this);
}

engine::IrradianceManager::~IrradianceManager() {
    for (IrradianceProbe& probe : irradianceProbes) {
        probe.destroy();
    }
    VkDevice device = renderer->getDevice();
    for (size_t i = 0; i < irradianceBuffers.size(); ++i) {
        if (irradianceBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, irradianceBuffers[i], nullptr);
        }
        if (i < irradianceBuffersMemory.size() && irradianceBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, irradianceBuffersMemory[i], nullptr);
        }
    }
    for (size_t i = 0; i < activeProbeIndexBuffers.size(); ++i) {
        if (i < activeProbeIndexBuffersMapped.size() && activeProbeIndexBuffersMapped[i] != nullptr && i < activeProbeIndexBuffersMemory.size() && activeProbeIndexBuffersMemory[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, activeProbeIndexBuffersMemory[i]);
            activeProbeIndexBuffersMapped[i] = nullptr;
        }
        if (activeProbeIndexBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, activeProbeIndexBuffers[i], nullptr);
        }
        if (i < activeProbeIndexBuffersMemory.size() && activeProbeIndexBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, activeProbeIndexBuffersMemory[i], nullptr);
        }
    }
    for (size_t i = 0; i < dynamicSHOutputBuffers.size(); ++i) {
        if (dynamicSHOutputBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, dynamicSHOutputBuffers[i], nullptr);
        }
        if (i < dynamicSHOutputBuffersMemory.size() && dynamicSHOutputBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, dynamicSHOutputBuffersMemory[i], nullptr);
        }
    }
    for (size_t i = 0; i < dynamicSHPartialBuffers.size(); ++i) {
        if (dynamicSHPartialBuffers[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, dynamicSHPartialBuffers[i], nullptr);
        }
        if (i < dynamicSHPartialBuffersMemory.size() && dynamicSHPartialBuffersMemory[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, dynamicSHPartialBuffersMemory[i], nullptr);
        }
    }
    irradianceBuffers.clear();
    irradianceBuffersMemory.clear();
    irradianceBuffersMapped.clear();
    activeProbeIndexBuffers.clear();
    activeProbeIndexBuffersMemory.clear();
    activeProbeIndexBuffersMapped.clear();
    dynamicSHOutputBuffers.clear();
    dynamicSHOutputBuffersMemory.clear();
    dynamicSHPartialBuffers.clear();
    dynamicSHPartialBuffersMemory.clear();
    destroyDummyProbeStorageImage();
    irradianceProbes.clear();
}

void engine::IrradianceManager::clear() {
    for (IrradianceProbe& probe : irradianceProbes) {
        probe.destroy();
    }
    irradianceProbes.clear();
    for (ActiveProbeFrame& frameData : activeProbeFrames) {
        frameData.count = 0u;
        frameData.exitIndex = 0u;
        frameData.computeCount = 0u;
        frameData.totalProbes = 0u;
        frameData.culledProbes = 0u;
    }
    std::fill(activeProbeFrameBuilt.begin(), activeProbeFrameBuilt.end(), 0u);
}

void engine::IrradianceManager::ensureDummyProbeStorageImage() {
    if (dummyProbeStorageView != VK_NULL_HANDLE && dummyProbeStorageImage != VK_NULL_HANDLE && dummyProbeStorageMemory != VK_NULL_HANDLE) {
        return;
    }

    std::tie(dummyProbeStorageImage, dummyProbeStorageMemory) = renderer->createImage(
        1,
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        6,
        0
    );

    dummyProbeStorageView = renderer->createImageView(
        dummyProbeStorageImage,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        6
    );
}

void engine::IrradianceManager::destroyDummyProbeStorageImage() {
    VkDevice device = renderer->getDevice();
    if (dummyProbeStorageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, dummyProbeStorageView, nullptr);
        dummyProbeStorageView = VK_NULL_HANDLE;
    }
    if (dummyProbeStorageImage != VK_NULL_HANDLE) {
        vkDestroyImage(device, dummyProbeStorageImage, nullptr);
        dummyProbeStorageImage = VK_NULL_HANDLE;
    }
    if (dummyProbeStorageMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, dummyProbeStorageMemory, nullptr);
        dummyProbeStorageMemory = VK_NULL_HANDLE;
    }
}

void engine::IrradianceManager::buildActiveProbeFrame(uint32_t frameIndex) {
    const uint32_t framesInFlight = std::max(1u, renderer->getFramesInFlight());
    if (activeProbeFrames.size() != static_cast<size_t>(framesInFlight)) {
        activeProbeFrames.assign(framesInFlight, ActiveProbeFrame{});
    }
    if (activeProbeFrameBuilt.size() != static_cast<size_t>(framesInFlight)) {
        activeProbeFrameBuilt.assign(framesInFlight, 0u);
    }

    const uint32_t frameSlot = frameIndex % framesInFlight;
    ActiveProbeFrame& frameData = activeProbeFrames[frameSlot];
    frameData.count = 0u;
    frameData.exitIndex = 0u;
    frameData.computeCount = 0u;

    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    const uint32_t probeCount = static_cast<uint32_t>(probes.size());
    frameData.totalProbes = probeCount;
    frameData.culledProbes = probeCount;
    if (frameData.indices.size() < static_cast<size_t>(probeCount)) {
        frameData.indices.resize(probeCount);
    }
    if (frameData.computeIndices.size() < static_cast<size_t>(probeCount)) {
        frameData.computeIndices.resize(probeCount);
    }
    if (frameData.distanceSq.size() < static_cast<size_t>(probeCount)) {
        frameData.distanceSq.resize(probeCount, std::numeric_limits<float>::max());
    }
    activeProbeFrameBuilt[frameSlot] = 1u;

    engine::Camera* camera = renderer->getEntityManager()->getCamera();
    if (!camera || probeCount == 0u) {
        return;
    }

    const glm::vec3 cameraPos = camera->getWorldPosition();
    uint32_t visibleCount = 0u;

    for (uint32_t probeIndex = 0u; probeIndex < probeCount; ++probeIndex) {
        IrradianceProbe& probe = probes[probeIndex];
        if (!camera->isSphereInFrustum(probe.getWorldPosition(), probe.getRadius())) {
            frameData.distanceSq[probeIndex] = std::numeric_limits<float>::max();
            continue;
        }
        const glm::vec3 toProbe = probe.getWorldPosition() - cameraPos;
        frameData.distanceSq[probeIndex] = glm::dot(toProbe, toProbe);
        frameData.indices[visibleCount] = probeIndex;
        ++visibleCount;
    }

    frameData.culledProbes = probeCount > visibleCount ? (probeCount - visibleCount) : 0u;

    std::sort(frameData.indices.begin(), frameData.indices.begin() + visibleCount, [&frameData](uint32_t a, uint32_t b) {
        if (frameData.distanceSq[a] == frameData.distanceSq[b]) {
            return a < b;
        }
        return frameData.distanceSq[a] < frameData.distanceSq[b];
    });

    const uint32_t activeCount = std::min(visibleCount, maxActiveProbesPerFrame);
    frameData.count = activeCount;
    frameData.exitIndex = activeCount;
}

const engine::IrradianceManager::ActiveProbeFrame* engine::IrradianceManager::getActiveProbeFrame(uint32_t frameIndex) const {
    if (activeProbeFrames.empty()) {
        return nullptr;
    }
    const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(activeProbeFrames.size());
    if (frameSlot >= activeProbeFrameBuilt.size() || activeProbeFrameBuilt[frameSlot] == 0u) {
        return nullptr;
    }
    return &activeProbeFrames[frameSlot];
}

uint32_t engine::IrradianceManager::getActiveProbeCount(uint32_t frameIndex) const {
    const ActiveProbeFrame* frameData = getActiveProbeFrame(frameIndex);
    if (!frameData) {
        return 0u;
    }
    const uint32_t boundedCount = std::min(frameData->count, static_cast<uint32_t>(frameData->indices.size()));
    return std::min(boundedCount, frameData->exitIndex);
}

void engine::IrradianceManager::createIrradianceProbesUBO() {
    const size_t frames = static_cast<size_t>(renderer->getFramesInFlight());
    irradianceBuffers.resize(frames, VK_NULL_HANDLE);
    irradianceBuffersMemory.resize(frames, VK_NULL_HANDLE);
    irradianceBuffersMapped.resize(frames, nullptr);
    for (size_t frame = 0; frame < frames; ++frame) {
        std::tie(irradianceBuffers[frame], irradianceBuffersMemory[frame]) = renderer->createBuffer(
            sizeof(IrradianceProbesUBO),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vkMapMemory(renderer->getDevice(), irradianceBuffersMemory[frame], 0, sizeof(IrradianceProbesUBO), 0, &irradianceBuffersMapped[frame]);
    }
}

void engine::IrradianceManager::createActiveProbeIndexBuffers() {
    const size_t frames = static_cast<size_t>(std::max(1u, renderer->getFramesInFlight()));
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(maxActiveProbesPerFrame * sizeof(uint32_t));

    if (activeProbeIndexBuffers.size() == frames && activeProbeIndexBuffersMemory.size() == frames && activeProbeIndexBuffersMapped.size() == frames) {
        bool allValid = true;
        for (size_t frame = 0; frame < frames; ++frame) {
            if (activeProbeIndexBuffers[frame] == VK_NULL_HANDLE || activeProbeIndexBuffersMemory[frame] == VK_NULL_HANDLE || activeProbeIndexBuffersMapped[frame] == nullptr) {
                allValid = false;
                break;
            }
        }
        if (allValid) {
            return;
        }
    }

    VkDevice device = renderer->getDevice();
    for (size_t frame = 0; frame < activeProbeIndexBuffers.size(); ++frame) {
        if (frame < activeProbeIndexBuffersMapped.size() && activeProbeIndexBuffersMapped[frame] != nullptr && frame < activeProbeIndexBuffersMemory.size() && activeProbeIndexBuffersMemory[frame] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, activeProbeIndexBuffersMemory[frame]);
            activeProbeIndexBuffersMapped[frame] = nullptr;
        }
        if (activeProbeIndexBuffers[frame] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, activeProbeIndexBuffers[frame], nullptr);
        }
        if (frame < activeProbeIndexBuffersMemory.size() && activeProbeIndexBuffersMemory[frame] != VK_NULL_HANDLE) {
            vkFreeMemory(device, activeProbeIndexBuffersMemory[frame], nullptr);
        }
    }

    activeProbeIndexBuffers.assign(frames, VK_NULL_HANDLE);
    activeProbeIndexBuffersMemory.assign(frames, VK_NULL_HANDLE);
    activeProbeIndexBuffersMapped.assign(frames, nullptr);

    for (size_t frame = 0; frame < frames; ++frame) {
        std::tie(activeProbeIndexBuffers[frame], activeProbeIndexBuffersMemory[frame]) = renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vkMapMemory(device, activeProbeIndexBuffersMemory[frame], 0, bufferSize, 0, &activeProbeIndexBuffersMapped[frame]);
        uint32_t* mapped = static_cast<uint32_t*>(activeProbeIndexBuffersMapped[frame]);
        for (uint32_t i = 0; i < maxActiveProbesPerFrame; ++i) {
            mapped[i] = 0u;
        }
    }
}

VkBuffer engine::IrradianceManager::getActiveProbeIndexBuffer(uint32_t frameIndex) const {
    if (activeProbeIndexBuffers.empty()) {
        return VK_NULL_HANDLE;
    }
    const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(activeProbeIndexBuffers.size());
    return activeProbeIndexBuffers[frameSlot];
}

void engine::IrradianceManager::createDynamicSHOutputBuffers() {
    const size_t frames = static_cast<size_t>(std::max(1u, renderer->getFramesInFlight()));
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(maxActiveProbesPerFrame) * sizeof(ProbeSHData);

    if (dynamicSHOutputBuffers.size() == frames && dynamicSHOutputBuffersMemory.size() == frames) {
        bool allValid = true;
        for (size_t frame = 0; frame < frames; ++frame) {
            if (dynamicSHOutputBuffers[frame] == VK_NULL_HANDLE || dynamicSHOutputBuffersMemory[frame] == VK_NULL_HANDLE) {
                allValid = false;
                break;
            }
        }
        if (allValid) {
            return;
        }
    }

    VkDevice device = renderer->getDevice();
    for (size_t frame = 0; frame < dynamicSHOutputBuffers.size(); ++frame) {
        if (dynamicSHOutputBuffers[frame] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, dynamicSHOutputBuffers[frame], nullptr);
        }
        if (frame < dynamicSHOutputBuffersMemory.size() && dynamicSHOutputBuffersMemory[frame] != VK_NULL_HANDLE) {
            vkFreeMemory(device, dynamicSHOutputBuffersMemory[frame], nullptr);
        }
    }

    dynamicSHOutputBuffers.assign(frames, VK_NULL_HANDLE);
    dynamicSHOutputBuffersMemory.assign(frames, VK_NULL_HANDLE);

    for (size_t frame = 0; frame < frames; ++frame) {
        std::tie(dynamicSHOutputBuffers[frame], dynamicSHOutputBuffersMemory[frame]) = renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
    }
}

void engine::IrradianceManager::createDynamicSHPartialBuffers() {
    const size_t frames = static_cast<size_t>(std::max(1u, renderer->getFramesInFlight()));
    constexpr uint32_t kCubemapSize = 16u;
    constexpr uint32_t kWorkgroupSize = 8u;
    const uint32_t numGroupsX = (kCubemapSize + kWorkgroupSize - 1u) / kWorkgroupSize;
    const uint32_t numGroupsY = (kCubemapSize + kWorkgroupSize - 1u) / kWorkgroupSize;
    const uint32_t workgroupsPerProbe = numGroupsX * numGroupsY * 6u;
    const VkDeviceSize bufferSize = static_cast<VkDeviceSize>(maxActiveProbesPerFrame) * workgroupsPerProbe * sizeof(ProbeSHData);

    if (dynamicSHPartialBuffers.size() == frames && dynamicSHPartialBuffersMemory.size() == frames) {
        bool allValid = true;
        for (size_t frame = 0; frame < frames; ++frame) {
            if (dynamicSHPartialBuffers[frame] == VK_NULL_HANDLE || dynamicSHPartialBuffersMemory[frame] == VK_NULL_HANDLE) {
                allValid = false;
                break;
            }
        }
        if (allValid) {
            return;
        }
    }

    VkDevice device = renderer->getDevice();
    for (size_t frame = 0; frame < dynamicSHPartialBuffers.size(); ++frame) {
        if (dynamicSHPartialBuffers[frame] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, dynamicSHPartialBuffers[frame], nullptr);
        }
        if (frame < dynamicSHPartialBuffersMemory.size() && dynamicSHPartialBuffersMemory[frame] != VK_NULL_HANDLE) {
            vkFreeMemory(device, dynamicSHPartialBuffersMemory[frame], nullptr);
        }
    }

    dynamicSHPartialBuffers.assign(frames, VK_NULL_HANDLE);
    dynamicSHPartialBuffersMemory.assign(frames, VK_NULL_HANDLE);

    for (size_t frame = 0; frame < frames; ++frame) {
        std::tie(dynamicSHPartialBuffers[frame], dynamicSHPartialBuffersMemory[frame]) = renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
    }
}

VkBuffer engine::IrradianceManager::getDynamicSHPartialBuffer(uint32_t frameIndex) const {
    if (dynamicSHPartialBuffers.empty()) {
        return VK_NULL_HANDLE;
    }
    const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(dynamicSHPartialBuffers.size());
    return dynamicSHPartialBuffers[frameSlot];
}

VkBuffer engine::IrradianceManager::getDynamicSHOutputBuffer(uint32_t frameIndex) const {
    if (dynamicSHOutputBuffers.empty()) {
        return VK_NULL_HANDLE;
    }
    const uint32_t frameSlot = frameIndex % static_cast<uint32_t>(dynamicSHOutputBuffers.size());
    return dynamicSHOutputBuffers[frameSlot];
}

uint32_t engine::IrradianceManager::getDynamicComputeProbeCount(uint32_t frameIndex) const {
    const ActiveProbeFrame* frameData = getActiveProbeFrame(frameIndex);
    if (!frameData) {
        return 0u;
    }
    const uint32_t boundedCount = std::min(frameData->computeCount, static_cast<uint32_t>(frameData->computeIndices.size()));
    return std::min(boundedCount, maxActiveProbesPerFrame);
}

void engine::IrradianceManager::fillBakedProbeCubemapImageInfos(uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) const {
    const size_t startIndex = imageInfos.size();
    if (count == 0u) {
        return;
    }

    VkImageView fallbackView = VK_NULL_HANDLE;
    if (TextureManager* textureManager = renderer->getTextureManager()) {
        Texture* fallbackTex = textureManager->getTexture("fallback_shadow_cube");
        if (fallbackTex) {
            fallbackView = fallbackTex->imageView;
        }
    }
    for (const IrradianceProbe& probe : irradianceProbes) {
        fallbackView = probe.getBakedCubemapView();
        if (fallbackView != VK_NULL_HANDLE) {
            break;
        }
    }

    imageInfos.resize(startIndex + count, {
        .sampler = VK_NULL_HANDLE,
        .imageView = fallbackView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });

    const uint32_t writeCount = std::min(count, static_cast<uint32_t>(irradianceProbes.size()));
    for (uint32_t i = 0u; i < writeCount; ++i) {
        VkImageView probeView = irradianceProbes[i].getBakedCubemapView();
        if (probeView != VK_NULL_HANDLE) {
            imageInfos[startIndex + i].imageView = probeView;
        }
    }
}

void engine::IrradianceManager::fillDynamicProbeCubemapImageInfos(uint32_t frameIndex, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) const {
    const size_t startIndex = imageInfos.size();
    if (count == 0u) {
        return;
    }

    VkImageView fallbackView = VK_NULL_HANDLE;
    if (TextureManager* textureManager = renderer->getTextureManager()) {
        Texture* fallbackTex = textureManager->getTexture("fallback_shadow_cube");
        if (fallbackTex) {
            fallbackView = fallbackTex->imageView;
        }
    }
    for (const IrradianceProbe& probe : irradianceProbes) {
        fallbackView = probe.getDynamicCubemapView(frameIndex);
        if (fallbackView != VK_NULL_HANDLE) {
            break;
        }
    }

    imageInfos.resize(startIndex + count, {
        .sampler = VK_NULL_HANDLE,
        .imageView = fallbackView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    });

    const uint32_t writeCount = std::min(count, static_cast<uint32_t>(irradianceProbes.size()));
    for (uint32_t i = 0u; i < writeCount; ++i) {
        VkImageView probeView = irradianceProbes[i].getDynamicCubemapView(frameIndex);
        if (probeView != VK_NULL_HANDLE) {
            imageInfos[startIndex + i].imageView = probeView;
        }
    }
}

void engine::IrradianceManager::fillDynamicProbeStorageImageInfos(uint32_t frameIndex, uint32_t count, std::vector<VkDescriptorImageInfo>& imageInfos) {
    const size_t startIndex = imageInfos.size();
    if (count == 0u) {
        return;
    }

    VkImageView fallbackView = VK_NULL_HANDLE;
    for (const IrradianceProbe& probe : irradianceProbes) {
        fallbackView = probe.getDynamicCubemapStorageView(frameIndex);
        if (fallbackView != VK_NULL_HANDLE) {
            break;
        }
    }
    if (fallbackView == VK_NULL_HANDLE) {
        ensureDummyProbeStorageImage();
        fallbackView = dummyProbeStorageView;
    }

    imageInfos.resize(startIndex + count, {
        .sampler = VK_NULL_HANDLE,
        .imageView = fallbackView,
        .imageLayout = VK_IMAGE_LAYOUT_GENERAL
    });

    const uint32_t writeCount = std::min(count, static_cast<uint32_t>(irradianceProbes.size()));
    for (uint32_t i = 0u; i < writeCount; ++i) {
        VkImageView probeView = irradianceProbes[i].getDynamicCubemapStorageView(frameIndex);
        if (probeView != VK_NULL_HANDLE) {
            imageInfos[startIndex + i].imageView = probeView;
        }
    }
}

void engine::IrradianceManager::updateIrradianceProbesUBO(uint32_t frameIndex) {
    if (irradianceBuffers.size() < static_cast<size_t>(renderer->getFramesInFlight())) {
        createIrradianceProbesUBO();
    }
    if (frameIndex >= irradianceBuffers.size() || irradianceBuffers[frameIndex] == VK_NULL_HANDLE) {
        std::cout << std::format("Warning: Irradiance Probes UBO buffer unavailable for frame {}. Skipping irradiance probes update.\n", frameIndex);
        return;
    }
    IrradianceProbesUBO* irradianceProbesUBO = static_cast<IrradianceProbesUBO*>(irradianceBuffersMapped[frameIndex]);
    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    size_t count = std::min(probes.size(), static_cast<size_t>(kMaxIrradianceProbes));
    for (size_t i = 0; i < count; ++i) {
        irradianceProbesUBO->probes[i] = probes[i].getProbeData();
    }
    irradianceProbesUBO->numProbes = glm::uvec4(count, 0, 0, 0);
}

void engine::IrradianceManager::createAllIrradianceMaps() {
    vkDeviceWaitIdle(renderer->getDevice());
    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    for (auto& probe : probes) {
        probe.createCubemaps(renderer);
    }
}

void engine::IrradianceManager::bakeIrradianceMaps(VkCommandBuffer commandBuffer) {
    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    for (auto& probe : probes) {
        probe.bakeCubemap(renderer, commandBuffer);
    }
}

void engine::IrradianceManager::recordIrradianceReadback(VkCommandBuffer commandBuffer) {
    const uint32_t framesInFlight = std::max(1u, renderer->getFramesInFlight());
    createActiveProbeIndexBuffers();
    createDynamicSHPartialBuffers();
    createDynamicSHOutputBuffers();

    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    const uint32_t bakeProbeCount = std::min(static_cast<uint32_t>(probes.size()), maxActiveProbesPerFrame);

    if (activeProbeFrames.size() != static_cast<size_t>(framesInFlight)) {
        activeProbeFrames.assign(framesInFlight, ActiveProbeFrame{});
    }
    if (activeProbeFrameBuilt.size() != static_cast<size_t>(framesInFlight)) {
        activeProbeFrameBuilt.assign(framesInFlight, 0u);
    }

    for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
        for (auto& probe : probes) {
            probe.copyBakedToDynamic(renderer, commandBuffer, frame);
        }

        const uint32_t frameSlot = frame % framesInFlight;
        ActiveProbeFrame& frameData = activeProbeFrames[frameSlot];
        if (frameData.computeIndices.size() < static_cast<size_t>(maxActiveProbesPerFrame)) {
            frameData.computeIndices.resize(maxActiveProbesPerFrame, 0u);
        }
        frameData.computeCount = bakeProbeCount;
        for (uint32_t i = 0u; i < bakeProbeCount; ++i) {
            frameData.computeIndices[i] = i;
        }
        for (uint32_t i = bakeProbeCount; i < maxActiveProbesPerFrame; ++i) {
            frameData.computeIndices[i] = 0u;
        }
        activeProbeFrameBuilt[frameSlot] = 1u;

        if (frameSlot < activeProbeIndexBuffersMapped.size() && activeProbeIndexBuffersMapped[frameSlot] != nullptr) {
            uint32_t* mappedIndices = static_cast<uint32_t*>(activeProbeIndexBuffersMapped[frameSlot]);
            for (uint32_t i = 0; i < maxActiveProbesPerFrame; ++i) {
                mappedIndices[i] = frameData.computeIndices[i];
            }
        }

        dispatchDynamicIrradianceSH(commandBuffer, frame);
        dispatchDynamicIrradianceSHReduce(commandBuffer, frame);
    }
}

void engine::IrradianceManager::prepareDynamicIrradianceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    buildActiveProbeFrame(currentFrame);
    createActiveProbeIndexBuffers();
    if (irradianceBuffers.size() < static_cast<size_t>(renderer->getFramesInFlight())) {
        createIrradianceProbesUBO();
    }
    updateIrradianceProbesUBO(currentFrame);

    const ActiveProbeFrame* frameDataPtr = getActiveProbeFrame(currentFrame);
    if (!frameDataPtr) {
        return;
    }

    const uint32_t framesInFlight = std::max(1u, renderer->getFramesInFlight());
    const uint32_t frameSlot = currentFrame % framesInFlight;
    if (frameSlot >= activeProbeFrames.size()) {
        return;
    }

    ActiveProbeFrame& frameData = activeProbeFrames[frameSlot];
    frameData.computeCount = 0u;

    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    const uint32_t activeEnd = getActiveProbeCount(currentFrame);
    const uint32_t computeCapacity = std::min(maxActiveProbesPerFrame, static_cast<uint32_t>(frameData.computeIndices.size()));
    for (uint32_t i = 0u; i < activeEnd && frameData.computeCount < computeCapacity; ++i) {
        const uint32_t probeIndex = frameData.indices[i];
        if (probeIndex >= probes.size()) {
            continue;
        }
        if (!probes[probeIndex].prepareDynamicCubemapForParticleCompute(renderer, commandBuffer, currentFrame)) {
            continue;
        }
        frameData.computeIndices[frameData.computeCount] = probeIndex;
        ++frameData.computeCount;
    }

    if (frameSlot >= activeProbeIndexBuffersMapped.size() || activeProbeIndexBuffersMapped[frameSlot] == nullptr) {
        return;
    }
    uint32_t* mappedIndices = static_cast<uint32_t*>(activeProbeIndexBuffersMapped[frameSlot]);
    for (uint32_t i = 0; i < maxActiveProbesPerFrame; ++i) {
        mappedIndices[i] = (i < frameData.computeCount) ? frameData.computeIndices[i] : 0u;
    }
}

void engine::IrradianceManager::finalizeDynamicIrradianceCompute(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    const ActiveProbeFrame* frameData = getActiveProbeFrame(currentFrame);
    if (!frameData) {
        return;
    }
    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    const uint32_t computeEnd = getDynamicComputeProbeCount(currentFrame);
    for (uint32_t i = 0u; i < computeEnd; ++i) {
        const uint32_t probeIndex = frameData->computeIndices[i];
        if (probeIndex >= probes.size()) {
            continue;
        }
        probes[probeIndex].finalizeDynamicCubemapAfterParticleCompute(commandBuffer, currentFrame);
    }
}

void engine::IrradianceManager::renderDynamicIrradianceGraphics(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    buildActiveProbeFrame(currentFrame);

    const ActiveProbeFrame* frameData = getActiveProbeFrame(currentFrame);
    if (!frameData) {
        return;
    }

    std::vector<IrradianceProbe>& probes = getIrradianceProbes();
    const uint32_t activeEnd = getActiveProbeCount(currentFrame);
    for (uint32_t i = 0u; i < activeEnd; ++i) {
        const uint32_t probeIndex = frameData->indices[i];
        if (probeIndex >= probes.size()) {
            continue;
        }
        probes[probeIndex].renderDynamicCubemap(renderer, commandBuffer, currentFrame, i, activeEnd);
    }
}

void engine::IrradianceManager::dispatchDynamicIrradianceSH(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    const uint32_t activeProbeCount = getDynamicComputeProbeCount(currentFrame);
    if (activeProbeCount == 0u) {
        return;
    }

    ComputeShader* shader = renderer->getShaderManager()->getComputeShader("sh");
    if (!shader || shader->descriptorSets.empty()) {
        return;
    }

    const uint32_t dsIndex = std::min(currentFrame, static_cast<uint32_t>(shader->descriptorSets.size() - 1));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->pipeline);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        shader->pipelineLayout,
        0,
        1,
        &shader->descriptorSets[dsIndex],
        0,
        nullptr
    );

    SHPC pc = {
        .cubemapSize = 16u,
        .activeProbeCount = activeProbeCount,
        .pad0 = 0u,
        .pad1 = 0u
    };
    vkCmdPushConstants(
        commandBuffer,
        shader->pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(SHPC),
        &pc
    );

    const uint32_t groupX = (pc.cubemapSize + 7u) / 8u;
    const uint32_t groupY = (pc.cubemapSize + 7u) / 8u;
    const uint32_t groupZ = activeProbeCount * 6u;
    vkCmdDispatch(commandBuffer, groupX, groupY, groupZ);
}

void engine::IrradianceManager::dispatchDynamicIrradianceSHReduce(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    const uint32_t activeProbeCount = getDynamicComputeProbeCount(currentFrame);
    if (activeProbeCount == 0u) {
        return;
    }

    const VkBuffer partialBuffer = getDynamicSHPartialBuffer(currentFrame);
    if (partialBuffer == VK_NULL_HANDLE) {
        return;
    }
    VkBufferMemoryBarrier partialToRead = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = partialBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        1, &partialToRead,
        0, nullptr
    );

    ComputeShader* shader = renderer->getShaderManager()->getComputeShader("shreduce");
    if (!shader || shader->descriptorSets.empty()) {
        return;
    }

    const uint32_t dsIndex = std::min(currentFrame, static_cast<uint32_t>(shader->descriptorSets.size() - 1));

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->pipeline);
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        shader->pipelineLayout,
        0,
        1,
        &shader->descriptorSets[dsIndex],
        0,
        nullptr
    );

    SHPC pc = {
        .cubemapSize = 16u,
        .activeProbeCount = activeProbeCount,
        .pad0 = 0u,
        .pad1 = 0u
    };
    vkCmdPushConstants(
        commandBuffer,
        shader->pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(SHPC),
        &pc
    );

    const uint32_t groupX = activeProbeCount;
    vkCmdDispatch(commandBuffer, groupX, 1u, 1u);

    const VkBuffer outputBuffer = getDynamicSHOutputBuffer(currentFrame);
    if (outputBuffer == VK_NULL_HANDLE) {
        return;
    }
    VkBufferMemoryBarrier outputToRead = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = outputBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,
        0, nullptr,
        1, &outputToRead,
        0, nullptr
    );
}

void engine::IrradianceManager::renderDynamicIrradiance(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    renderDynamicIrradianceGraphics(commandBuffer, currentFrame);
    dispatchDynamicIrradianceSH(commandBuffer, currentFrame);
    dispatchDynamicIrradianceSHReduce(commandBuffer, currentFrame);
}

void engine::IrradianceManager::processIrradianceSH() {
    irradianceBakingPending = false;
}