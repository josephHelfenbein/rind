#include <engine/VolumetricManager.h>
#include <engine/ShaderManager.h>
#include <engine/PushConstants.h>
#include <engine/Camera.h>

engine::Volumetric::Volumetric(
    VolumetricManager* volumetricManager,
    const glm::mat4& initialTransform,
    const glm::mat4& finalTransform,
    const glm::vec4& color,
    float lifetime
) : volumetricManager(volumetricManager), initialTransform(initialTransform), finalTransform(finalTransform), color(color), lifetime(lifetime) {
        volumetricManager->registerVolumetric(this);
    }

engine::Volumetric::~Volumetric() {
    if (volumetricManager) {
        volumetricManager->unregisterVolumetric(this);
    }
}

engine::VolumetricManager::VolumetricManager(engine::Renderer* renderer)
    : renderer(renderer) {
        renderer->registerVolumetricManager(this);
    }

engine::VolumetricManager::~VolumetricManager() {
    for (size_t i = 0; i < volumetricBuffersMapped.size(); ++i) {
        if (volumetricBuffersMapped[i] != nullptr && i < volumetricBufferMemory.size() && volumetricBufferMemory[i] != VK_NULL_HANDLE) {
            vkUnmapMemory(renderer->getDevice(), volumetricBufferMemory[i]);
            volumetricBuffersMapped[i] = nullptr;
        }
    }
    for (size_t i = 0; i < volumetricBuffers.size(); ++i) {
        vkDestroyBuffer(renderer->getDevice(), volumetricBuffers[i], nullptr);
        vkFreeMemory(renderer->getDevice(), volumetricBufferMemory[i], nullptr);
    }
    volumetricBuffers.clear();
    volumetricBufferMemory.clear();
    volumetricBuffersMapped.clear();
    if (cubeVertexBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(renderer->getDevice(), cubeVertexBuffer, nullptr);
        vkFreeMemory(renderer->getDevice(), cubeVertexBufferMemory, nullptr);
    }
    std::vector<Volumetric*> volumetricsCopy = volumetrics;
    volumetrics.clear();
    for (auto* v : volumetricsCopy) {
        delete v;
    }
}

void engine::VolumetricManager::init() {
    VkDeviceSize cubeSize = sizeof(unitCube);
    std::tie(cubeVertexBuffer, cubeVertexBufferMemory) = renderer->createBuffer(
        cubeSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    renderer->copyDataToBuffer(unitCube, cubeSize, cubeVertexBuffer, cubeVertexBufferMemory);
    VkDeviceSize bufferSize = maxVolumetrics * sizeof(VolumetricGPU);
    size_t frames = static_cast<size_t>(renderer->getMaxFramesInFlight());
    volumetricBuffers.resize(frames);
    volumetricBufferMemory.resize(frames);
    volumetricBuffersMapped.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        std::tie(volumetricBuffers[i], volumetricBufferMemory[i]) = renderer->createBuffer(
            bufferSize,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
        vkMapMemory(renderer->getDevice(), volumetricBufferMemory[i], 0, bufferSize, 0, &volumetricBuffersMapped[i]);
    }
    createVolumetricDescriptorSets();
}

void engine::VolumetricManager::clear() {
    for (auto* v : volumetrics) {
        v->markForDeletion();
    }
}

void engine::VolumetricManager::createVolumetricDescriptorSets() {
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("volumetric");
    VkDevice device = renderer->getDevice();
    size_t frames = static_cast<size_t>(renderer->getMaxFramesInFlight());
    VkImageView depthImageView = renderer->getPassImageView("gbuffer", "Depth");
    if (depthImageView == VK_NULL_HANDLE) {
        throw std::runtime_error("Failed to get gbuffer depth image view for volumetrics!");
    }

    std::vector<VkDescriptorSetLayout> layouts(frames, shader->descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = shader->descriptorPool,
        .descriptorSetCount = static_cast<uint32_t>(frames),
        .pSetLayouts = layouts.data()
    };
    descriptorSets.resize(frames);
    if (vkAllocateDescriptorSets(device, &allocInfo, descriptorSets.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate volumetric descriptor sets!");
    }

    for (size_t i = 0; i < frames; ++i) {
        VkDescriptorBufferInfo bufferInfo = {
            .buffer = volumetricBuffers[i],
            .offset = 0,
            .range = maxVolumetrics * sizeof(VolumetricGPU)
        };
        VkDescriptorImageInfo depthImageInfo = {
            .sampler = VK_NULL_HANDLE,
            .imageView = depthImageView,
            .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
        };
        VkDescriptorImageInfo samplerInfo = {
            .sampler = renderer->getMainTextureSampler(),
            .imageView = VK_NULL_HANDLE,
            .imageLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };
        std::array<VkWriteDescriptorSet, 3> descriptorWrites = {{
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &bufferInfo
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 1,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                .pImageInfo = &depthImageInfo
            },
            {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = descriptorSets[i],
                .dstBinding = 2,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                .pImageInfo = &samplerInfo
            }
        }};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
    }
}

void engine::VolumetricManager::updateVolumetricBuffer(uint32_t currentFrame) {
    VkDevice device = renderer->getDevice();
    if (volumetrics.size() > maxVolumetrics) {
        vkDeviceWaitIdle(device);
        if (volumetrics.size() > hardCap) {
            size_t toRemove = volumetrics.size() - hardCap;
            for (size_t i = 0; i < toRemove; ++i) {
                volumetrics[i]->detachFromManager();
                delete volumetrics[i];
            }
            volumetrics.erase(volumetrics.begin(), volumetrics.begin() + toRemove);
        }
        maxVolumetrics = std::min(std::max(maxVolumetrics * 2, static_cast<uint32_t>(volumetrics.size())), hardCap);
        for (size_t i = 0; i < volumetricBuffersMapped.size(); ++i) {
            if (volumetricBuffersMapped[i] != nullptr && i < volumetricBufferMemory.size() && volumetricBufferMemory[i] != VK_NULL_HANDLE) {
                vkUnmapMemory(device, volumetricBufferMemory[i]);
                volumetricBuffersMapped[i] = nullptr;
            }
        }
        for (size_t i = 0; i < volumetricBuffers.size(); ++i) {
            vkDestroyBuffer(device, volumetricBuffers[i], nullptr);
            vkFreeMemory(device, volumetricBufferMemory[i], nullptr);
        }
        volumetricBuffers.clear();
        volumetricBufferMemory.clear();
        volumetricBuffersMapped.clear();
        volumetricBuffers.resize(renderer->getMaxFramesInFlight());
        volumetricBufferMemory.resize(renderer->getMaxFramesInFlight());
        volumetricBuffersMapped.resize(renderer->getMaxFramesInFlight(), nullptr);
        for (size_t i = 0; i < volumetricBuffers.size(); ++i) {
            VkDeviceSize bufferSize = maxVolumetrics * sizeof(VolumetricGPU);
            std::tie(volumetricBuffers[i], volumetricBufferMemory[i]) = renderer->createBuffer(
                bufferSize,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            );
            vkMapMemory(device, volumetricBufferMemory[i], 0, bufferSize, 0, &volumetricBuffersMapped[i]);
        }
        GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("volumetric");
        vkResetDescriptorPool(renderer->getDevice(), shader->descriptorPool, 0);
        createVolumetricDescriptorSets();
    }
    VolumetricGPU* gpuData = static_cast<VolumetricGPU*>(volumetricBuffersMapped[currentFrame]);
    for (size_t i = 0; i < volumetrics.size(); ++i) {
        gpuData[i] = volumetrics[i]->getGPUData();
    }
}

void engine::VolumetricManager::renderVolumetrics(VkCommandBuffer commandBuffer, uint32_t currentFrame) {
    if (volumetrics.empty()) return;
    GraphicsShader* shader = renderer->getShaderManager()->getGraphicsShader("volumetric");
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipelineLayout, 0, 1, &descriptorSets[currentFrame], 0, nullptr);
    Camera* camera = renderer->getEntityManager()->getCamera();
    if (!camera) return;
    VkExtent2D extent = renderer->getSwapChainExtent();
    VolumetricPC pushConstants = {
        .viewProj = camera->getViewProjectionMatrix(),
        .camPos = camera->getWorldPosition()
    };
    vkCmdPushConstants(commandBuffer, shader->pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(VolumetricPC), &pushConstants);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, &cubeVertexBuffer, &offset);
    vkCmdDraw(commandBuffer, 36, static_cast<uint32_t>(volumetrics.size()), 0, 0);
}

void engine::VolumetricManager::updateAll(float deltaTime) {
#if defined(USE_OPENMP)
    #pragma omp parallel for
#endif
    for (int i = 0; i < static_cast<int>(volumetrics.size()); ++i) {
        Volumetric* volumetric = volumetrics[static_cast<size_t>(i)];
        volumetric->setAge(volumetric->getAge() + deltaTime);
        if (volumetric->getAge() >= volumetric->getLifetime()) {
            volumetric->markForDeletion();
        }
    }
    auto it = std::remove_if(volumetrics.begin(), volumetrics.end(), [](Volumetric* v) {
        if (v->isMarkedForDeletion()) {
            v->detachFromManager();
            delete v;
            return true;
        }
        return false;
    });
    volumetrics.erase(it, volumetrics.end());
}