#include <engine/IrradianceProbe.h>
#include <engine/ShaderManager.h>
#include <cmath>
#include <limits>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

engine::IrradianceProbe::IrradianceProbe(EntityManager* entityManager, const std::string& name, glm::mat4 transform, float radius) 
    : Entity(entityManager, name, "", transform, {}, false), radius(radius) {
        entityManager->addIrradianceProbe(this);
        createCubemaps(entityManager->getRenderer());
    }

engine::IrradianceProbe::~IrradianceProbe() {
    VkDevice device = getEntityManager()->getRenderer()->getDevice();
    for (uint32_t i = 0; i < 6; ++i) {
        if (bakedCubemapFaceViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, bakedCubemapFaceViews[i], nullptr);
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
    if (cubemapSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, cubemapSampler, nullptr);
    }
    cleanupComputeResources(getEntityManager()->getRenderer());
}

void engine::IrradianceProbe::createCubemaps(Renderer* renderer) {
    if (hasImageMap) {
        return;
    }
    
    bakedImageReady = false;
    computeDispatched = false;
    
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
    
    const size_t outputBufferSize = totalWorkgroups * 9 * sizeof(float) * 4;
    std::tie(shOutputBuffer, shOutputMemory) = renderer->createBuffer(
        outputBufferSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    
    ComputeShader* shCompute = renderer->getShaderManager()->getComputeShader("sh");
    if (!shCompute) {
        throw std::runtime_error("sh compute shader not found!");
    }
    
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = shCompute->descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &shCompute->descriptorSetLayout
    };
    if (vkAllocateDescriptorSets(device, &allocInfo, &shDescriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate SH compute descriptor set!");
    }
    
    VkDescriptorBufferInfo bufferInfo = {
        .buffer = shOutputBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };
    VkDescriptorImageInfo imageInfo = {
        .sampler = cubemapSampler,
        .imageView = bakedCubemapView,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    std::array<VkWriteDescriptorSet, 2> descriptorWrites = {{
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = shDescriptorSet,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &bufferInfo
        },
        {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = shDescriptorSet,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &imageInfo
        }
    }};
    vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    
    computeResourcesCreated = true;
}

void engine::IrradianceProbe::cleanupComputeResources(Renderer* renderer) {
    VkDevice device = renderer->getDevice();
    
    if (shDescriptorSet != VK_NULL_HANDLE) {
        ComputeShader* shCompute = renderer->getShaderManager()->getComputeShader("sh");
        if (shCompute) {
            vkFreeDescriptorSets(device, shCompute->descriptorPool, 1, &shDescriptorSet);
        }
        shDescriptorSet = VK_NULL_HANDLE;
    }
    
    if (shOutputBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, shOutputBuffer, nullptr);
        shOutputBuffer = VK_NULL_HANDLE;
    }
    if (shOutputMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, shOutputMemory, nullptr);
        shOutputMemory = VK_NULL_HANDLE;
    }
    
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
    glm::vec3 probePos = getWorldPosition();
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
    std::function<void(Entity*, glm::mat4&)> drawStaticEntity = [&](Entity* entity, glm::mat4& viewProj) {
        if (!entity->getIsMovable()
         && entity->getModel()
         && entity->getShader() == "gbuffer"
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
            std::vector<VkDescriptorSet> descriptorSets = entity->getDescriptorSets();
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
            drawStaticEntity(child, viewProj);
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
            drawStaticEntity(rootEntity, viewProjs[face]);
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

void engine::IrradianceProbe::dispatchSHCompute(Renderer* renderer, VkCommandBuffer commandBuffer) {
    if (!bakedImageReady || computeDispatched) {
        std::cout << "WARNING: dispatchSHCompute skipped for " << getName()
                  << " bakedImageReady=" << bakedImageReady
                  << " computeDispatched=" << computeDispatched << "\n";
        return;
    }
    
    if (!computeResourcesCreated) {
        createComputeResources(renderer);
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
        &shDescriptorSet,
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
    
    VkBufferMemoryBarrier bufferBarrier = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .buffer = shOutputBuffer,
        .offset = 0,
        .size = VK_WHOLE_SIZE
    };
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0, nullptr,
        1, &bufferBarrier,
        0, nullptr
    );
    
    computeDispatched = true;
}

void engine::IrradianceProbe::processSHProjection(Renderer* renderer) {
    if (!computeDispatched || shOutputBuffer == VK_NULL_HANDLE) {
        std::cout << "WARNING: processSHProjection skipped for " << getName() 
                  << " computeDispatched=" << computeDispatched 
                  << " shOutputBuffer=" << (shOutputBuffer != VK_NULL_HANDLE) << "\n";
        return;
    }
    
    VkDevice device = renderer->getDevice();
    
    const size_t outputSize = totalWorkgroups * 9 * sizeof(float) * 4;
    
    void* mappedData;
    vkMapMemory(device, shOutputMemory, 0, outputSize, 0, &mappedData);
    float* outputData = static_cast<float*>(mappedData);
    
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
    
    vkUnmapMemory(device, shOutputMemory);
    
    computeDispatched = false;
}

engine::IrradianceProbeData engine::IrradianceProbe::getProbeData() const {
    IrradianceProbeData data;
    data.position = glm::vec4(getWorldPosition(), radius);
    for (int i = 0; i < 9; ++i) {
        data.shCoeffs[i] = glm::vec4(shCoeffs[i], 0.0f);
    }
    return data;
}