#include <engine/IrradianceManager.h>
#include <engine/ShaderManager.h>
#include <engine/ParticleManager.h>
#include <engine/Camera.h>
#include <algorithm>
#include <cmath>
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
    dynamicCubemapImages.clear();
    dynamicCubemapMemories.clear();
    if (cubemapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, cubemapSampler, nullptr);
    }
    cleanupComputeResources(irradianceManager->getRenderer());
}

void engine::IrradianceProbe::createCubemaps(Renderer* renderer) {
    if (hasImageMap) {
        return;
    }
    
    bakedImageReady = false;
    dynamicImageReady.clear();
    dynamicCubemapDirty.clear();
    shComputePending.clear();
    initialSHComputed.clear();
    
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
    dynamicCubemapMemories.assign(framesInFlight, VK_NULL_HANDLE);
    dynamicCubemapFaceViews.assign(framesInFlight, {});
    dynamicImageReady.assign(framesInFlight, 0u);
    dynamicCubemapDirty.assign(framesInFlight, 0u);
    shComputePending.assign(framesInFlight, 0u);
    initialSHComputed.assign(framesInFlight, 0u);

    for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
        std::tie(dynamicCubemapImages[frame], dynamicCubemapMemories[frame]) = renderer->createImage(
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
        dynamicCubemapViews[frame] = renderer->createImageView(
            dynamicCubemapImages[frame],
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_ASPECT_COLOR_BIT,
            1,
            VK_IMAGE_VIEW_TYPE_CUBE,
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
    
    createComputeResources(renderer);
}

void engine::IrradianceProbe::createComputeResources(Renderer* renderer) {
    if (computeResourcesCreated) return;
    
    VkDevice device = renderer->getDevice();
    
    numWorkgroupsX = (cubemapSize + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    numWorkgroupsY = (cubemapSize + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    totalWorkgroups = numWorkgroupsX * numWorkgroupsY * 6;
    
    const uint32_t framesInFlight = std::max(1u, renderer->getFramesInFlight());
    const size_t outputBufferSize = totalWorkgroups * 9 * sizeof(float) * 4;
    shOutputBuffers.assign(framesInFlight, VK_NULL_HANDLE);
    shOutputMemories.assign(framesInFlight, VK_NULL_HANDLE);
    shOutputMappedData.assign(framesInFlight, nullptr);
    shDescriptorSets.assign(framesInFlight, VK_NULL_HANDLE);

    for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
        std::tie(shOutputBuffers[frame], shOutputMemories[frame]) = renderer->createBuffer(
            outputBufferSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vkMapMemory(renderer->getDevice(), shOutputMemories[frame], 0, outputBufferSize, 0, &shOutputMappedData[frame]);
    }
    
    ComputeShader* shCompute = renderer->getShaderManager()->getComputeShader("sh");
    if (!shCompute) {
        throw std::runtime_error("sh compute shader not found!");
    }
    
    std::vector<VkDescriptorSetLayout> layouts(framesInFlight, shCompute->descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = shCompute->descriptorPool,
        .descriptorSetCount = framesInFlight,
        .pSetLayouts = layouts.data()
    };
    if (vkAllocateDescriptorSets(device, &allocInfo, shDescriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SH compute descriptor set!");
    }

    for (uint32_t frame = 0; frame < framesInFlight; ++frame) {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = shOutputBuffers[frame],
            .offset = 0,
            .range = VK_WHOLE_SIZE
        };
        VkDescriptorImageInfo imageInfo = {
            .sampler = cubemapSampler,
            .imageView = frame < dynamicCubemapViews.size() ? dynamicCubemapViews[frame] : VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        };
        std::array<VkWriteDescriptorSet, 2> descriptorWrites = {{
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = shDescriptorSets[frame],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &bufferInfo
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = shDescriptorSets[frame],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &imageInfo
            }
        }};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
    
    computeResourcesCreated = true;
}

void engine::IrradianceProbe::cleanupComputeResources(Renderer* renderer) {
    VkDevice device = renderer->getDevice();

    if (!shDescriptorSets.empty()) {
        ComputeShader* shCompute = renderer->getShaderManager()->getComputeShader("sh");
        if (shCompute) {
            vkFreeDescriptorSets(
                device,
                shCompute->descriptorPool,
                static_cast<uint32_t>(shDescriptorSets.size()),
                shDescriptorSets.data()
            );
        }
        shDescriptorSets.clear();
    }

    for (size_t frame = 0; frame < shOutputBuffers.size(); ++frame) {
        if (frame < shOutputMappedData.size() && shOutputMappedData[frame] != nullptr && frame < shOutputMemories.size() && shOutputMemories[frame] != VK_NULL_HANDLE) {
            vkUnmapMemory(device, shOutputMemories[frame]);
            shOutputMappedData[frame] = nullptr;
        }
        if (shOutputBuffers[frame] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, shOutputBuffers[frame], nullptr);
        }
        if (frame < shOutputMemories.size() && shOutputMemories[frame] != VK_NULL_HANDLE) {
            vkFreeMemory(device, shOutputMemories[frame], nullptr);
        }
    }
    shOutputBuffers.clear();
    shOutputMemories.clear();
    shOutputMappedData.clear();
    
    computeResourcesCreated = false;
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
        ? (VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT)
        : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    
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
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(postBarriers.size()), postBarriers.data()
    );
    
    if (frameIdx < dynamicImageReady.size()) {
        dynamicImageReady[frameIdx] = 1u;
    }
}

void engine::IrradianceProbe::renderDynamicCubemap(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    if (dynamicCubemapImages.empty() || dynamicCubemapFaceViews.empty()) {
        return;
    }
    const uint32_t frameIdx = currentFrame % static_cast<uint32_t>(dynamicCubemapImages.size());
    VkImage dynamicCubemapImage = dynamicCubemapImages[frameIdx];
    if (dynamicCubemapImage == VK_NULL_HANDLE || frameIdx >= dynamicCubemapFaceViews.size()) {
        return;
    }
    if (frameIdx >= dynamicImageReady.size() || dynamicImageReady[frameIdx] == 0u) {
        return;
    }

    ParticleManager* particleManager = renderer->getParticleManager();
    const std::vector<Particle>& particles = particleManager->getParticles();
    size_t currentParticleCount = particles.size();
    
    // update if there are particles in the scene, or all particles disappeared
    bool particlesChanged = (currentParticleCount > 0) || (currentParticleCount != lastParticleCount);
    lastParticleCount = currentParticleCount;
    
    if (!particlesChanged) {
        if (frameIdx < dynamicCubemapDirty.size()) {
            dynamicCubemapDirty[frameIdx] = 0u;
        }
        return;
    }
    
    if (frameIdx < dynamicCubemapDirty.size()) {
        dynamicCubemapDirty[frameIdx] = 1u;
    }
    
    copyBakedToDynamic(renderer, commandBuffer, frameIdx);
    
    if (particles.empty()) return;

    VkImageMemoryBarrier toColorAttachment = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toColorAttachment
    );
    
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("particlesimple");
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipelineLayout, 0, 1, &particleManager->getDescriptorSets()[currentFrame], 0, nullptr);

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
    for (uint32_t face = 0u; face < 6u; ++face) {
        VkRenderingAttachmentInfo colorAttachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .imageView = dynamicCubemapFaceViews[frameIdx][face],
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE
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
        SimpleParticlePC pushConstants = {
            .viewProj = viewProjs[face],
            .particleSize = 0.1f
        };
        vkCmdPushConstants(commandBuffer, shader->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(SimpleParticlePC), &pushConstants);
        vkCmdDraw(commandBuffer, 4, static_cast<uint32_t>(particles.size()), 0, 0);
        renderer->getFpCmdEndRendering()(commandBuffer);
    }
    
    VkImageMemoryBarrier toShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
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
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toShaderRead
    );
}

void engine::IrradianceProbe::dispatchSHCompute(Renderer* renderer, VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (dynamicCubemapImages.empty()) {
        return;
    }
    const uint32_t frameIdx = frameIndex % static_cast<uint32_t>(dynamicCubemapImages.size());
    const bool frameDynamicReady = frameIdx < dynamicImageReady.size() && dynamicImageReady[frameIdx] != 0u;
    const bool frameInitialComputed = frameIdx < initialSHComputed.size() && initialSHComputed[frameIdx] != 0u;
    const bool frameDynamicDirty = frameIdx < dynamicCubemapDirty.size() && dynamicCubemapDirty[frameIdx] != 0u;
    if (!frameDynamicReady || (frameInitialComputed && !frameDynamicDirty)) {
        return;
    }
    
    if (!computeResourcesCreated) {
        createComputeResources(renderer);
    }
    
    if (frameIdx < initialSHComputed.size()) {
        initialSHComputed[frameIdx] = 1u;
    }
    if (frameIdx < shComputePending.size()) {
        shComputePending[frameIdx] = 1u;
    }
    
    ComputeShader* shCompute = renderer->getShaderManager()->getComputeShader("sh");
    if (!shCompute) {
        std::cout << "ERROR: sh compute shader not found!\n";
        return;
    }
    
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shCompute->pipeline);
    
    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        shCompute->pipelineLayout,
        0,
        1,
        &shDescriptorSets[frameIdx],
        0,
        nullptr
    );
    
    SHPC pc = { .cubemapSize = cubemapSize, .pad = {0, 0, 0} };
    vkCmdPushConstants(
        commandBuffer,
        shCompute->pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(SHPC),
        &pc
    );
    
    vkCmdDispatch(commandBuffer, numWorkgroupsX, numWorkgroupsY, 6);
}

void engine::IrradianceProbe::processSHProjection(Renderer* renderer, uint32_t frameIndex) {
    if (shOutputMappedData.empty()) {
        return;
    }
    const uint32_t frameIdx = frameIndex % static_cast<uint32_t>(shOutputMappedData.size());
    if (frameIdx >= shComputePending.size() || shComputePending[frameIdx] == 0u || shOutputMappedData[frameIdx] == nullptr) {
        return;
    }
    
    shComputePending[frameIdx] = 0u;
    
    float* outputData = static_cast<float*>(shOutputMappedData[frameIdx]);
    
    std::array<glm::vec3, 9> shAccum{};
    for (int i = 0; i < 9; ++i) {
        shAccum[i] = glm::vec3(0.0f);
    }
    
    for (uint32_t workgroup = 0; workgroup < totalWorkgroups; ++workgroup) {
        const size_t baseOffset = workgroup * 9 * 4;
        for (int i = 0; i < 9; ++i) {
            const size_t coeffOffset = baseOffset + i * 4;
            float x = outputData[coeffOffset + 0];
            float y = outputData[coeffOffset + 1];
            float z = outputData[coeffOffset + 2];
            if (!std::isnan(x) && !std::isnan(y) && !std::isnan(z)) {
                shAccum[i] += glm::vec3(x, y, z);
            }
        }
    }
    
    shCoeffs = shAccum;
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
    irradianceBuffers.clear();
    irradianceBuffersMemory.clear();
    irradianceBuffersMapped.clear();
    irradianceProbes.clear();
}

void engine::IrradianceManager::clear() {
    for (IrradianceProbe& probe : irradianceProbes) {
        probe.destroy();
    }
    irradianceProbes.clear();
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
    size_t count = std::min(probes.size(), static_cast<size_t>(32));
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
    for (auto& probe : getIrradianceProbes()) {
        probe.copyBakedToDynamic(renderer, commandBuffer, 0);
        probe.dispatchSHCompute(renderer, commandBuffer, 0);
    }
}

void engine::IrradianceManager::renderDynamicIrradianceGraphics(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    engine::Camera* camera = renderer->getEntityManager()->getCamera();
    if (!camera) return;
    for (auto& probe : getIrradianceProbes()) {
        probe.processSHProjection(renderer, currentFrame);
    }
    for (auto& probe : getIrradianceProbes()) {
        if (camera->isSphereInFrustum(probe.getWorldPosition(), probe.getRadius())) {
            probe.renderDynamicCubemap(renderer, commandBuffer, currentFrame);
        }
    }
}

void engine::IrradianceManager::dispatchDynamicIrradianceSH(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    engine::Camera* camera = renderer->getEntityManager()->getCamera();
    if (!camera) return;
    for (auto& probe : getIrradianceProbes()) {
        if (camera->isSphereInFrustum(probe.getWorldPosition(), probe.getRadius())) {
            probe.dispatchSHCompute(renderer, commandBuffer, currentFrame);
        }
    }
}

void engine::IrradianceManager::renderDynamicIrradiance(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    renderDynamicIrradianceGraphics(commandBuffer, currentFrame);
    dispatchDynamicIrradianceSH(commandBuffer, currentFrame);
}

void engine::IrradianceManager::processIrradianceSH() {
    for (auto& probe : getIrradianceProbes()) {
        probe.processSHProjection(renderer, 0);
    }
    irradianceBakingPending = false;
}