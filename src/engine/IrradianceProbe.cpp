#include <engine/IrradianceProbe.h>
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
    if (stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
    }
    if (stagingMemory != VK_NULL_HANDLE) {
        vkFreeMemory(device, stagingMemory, nullptr);
    }
}

void engine::IrradianceProbe::createCubemaps(Renderer* renderer) {
    if (hasImageMap) {
        return;
    }
    
    bakedImageReady = false;
    readbackRecorded = false;
    
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
    renderer->transitionImageLayoutInline(
        commandBuffer,
        bakedCubemapImage,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        1,
        6
    );
    bakedImageReady = true;
}

void engine::IrradianceProbe::recordCubemapReadback(Renderer* renderer, VkCommandBuffer commandBuffer) {
    if (!bakedImageReady || readbackRecorded) {
        std::cout << "WARNING: recordCubemapReadback skipped for " << getName()
                  << " bakedImageReady=" << bakedImageReady
                  << " readbackRecorded=" << readbackRecorded << "\n";
        return;
    }
    VkDevice device = renderer->getDevice();
    const size_t pixelSize = sizeof(uint16_t) * 4;
    const size_t faceSize = cubemapSize * cubemapSize * pixelSize;
    const size_t totalSize = faceSize * 6;
    
    if (stagingBuffer == VK_NULL_HANDLE) {
        std::tie(stagingBuffer, stagingMemory) = renderer->createBuffer(
            totalSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        );
    }
    
    std::vector<VkBufferImageCopy> copyRegions(6);
    for (uint32_t face = 0u; face < 6u; ++face) {
        copyRegions[face] = {
            .bufferOffset = face * faceSize,
            .bufferRowLength = 0,
            .bufferImageHeight = 0,
            .imageSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = 0,
                .baseArrayLayer = face,
                .layerCount = 1
            },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { cubemapSize, cubemapSize, 1 }
        };
    }
    vkCmdCopyImageToBuffer(
        commandBuffer,
        bakedCubemapImage,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        stagingBuffer,
        static_cast<uint32_t>(copyRegions.size()),
        copyRegions.data()
    );
    
    renderer->transitionImageLayoutInline(
        commandBuffer,
        bakedCubemapImage,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1,
        6
    );
    
    readbackRecorded = true;
}

void engine::IrradianceProbe::processSHProjection(Renderer* renderer) {
    if (!readbackRecorded || stagingBuffer == VK_NULL_HANDLE) {
        std::cout << "WARNING: processSHProjection skipped for " << getName() 
                  << " readbackRecorded=" << readbackRecorded 
                  << " stagingBuffer=" << (stagingBuffer != VK_NULL_HANDLE) << "\n";
        return;
    }
    
    VkDevice device = renderer->getDevice();
    
    const size_t pixelSize = sizeof(uint16_t) * 4;
    const size_t faceSize = cubemapSize * cubemapSize * pixelSize;
    const size_t totalSize = faceSize * 6;
    
    void* mappedData;
    vkMapMemory(device, stagingMemory, 0, totalSize, 0, &mappedData);
    uint16_t* pixelData = static_cast<uint16_t*>(mappedData);

    std::function<float(uint16_t)> halfToFloat = [&](uint16_t h) -> float {
        uint32_t sign = (h >> 15) & 0x1;
        uint32_t exp = (h >> 10) & 0x1F;
        uint32_t mant = h & 0x3FF;
        if (exp == 0) {
            if (mant == 0) return sign ? -0.0f : 0.0f;
            float m = mant / 1024.0f;
            return (sign ? -1.0f : 1.0f) * m * std::pow(2.0f, -14.0f);
        } else if (exp == 31) {
            return mant ? std::numeric_limits<float>::quiet_NaN() 
                        : (sign ? -std::numeric_limits<float>::infinity() 
                                : std::numeric_limits<float>::infinity());
        }
        return (sign ? -1.0f : 1.0f) * (1.0f + mant / 1024.0f) * std::pow(2.0f, static_cast<float>(exp) - 15.0f);
    };
    std::function<glm::vec3(uint32_t, float, float)> cubemapTexelToDirection = [&](uint32_t face, float u, float v) -> glm::vec3 {
        glm::vec3 dir;
        switch (face) {
            case 0: dir = glm::vec3( 1.0f, -v, -u); break;
            case 1: dir = glm::vec3(-1.0f, -v, u); break;
            case 2: dir = glm::vec3(u, 1.0f, v); break;
            case 3: dir = glm::vec3(u, -1.0f, -v); break;
            case 4: dir = glm::vec3(u, -v, 1.0f); break;
            case 5: dir = glm::vec3(-u, -v, -1.0f); break;
        }
        return glm::normalize(dir);
    };
    std::function<float(float, float, float)> texelSolidAngle = [&](float u, float v, float invSize) -> float {
        float x0 = u - invSize;
        float y0 = v - invSize;
        float x1 = u + invSize;
        float y1 = v + invSize;
        return std::atan2(x0 * y0, std::sqrt(x0 * x0 + y0 * y0 + 1.0f)) -
               std::atan2(x0 * y1, std::sqrt(x0 * x0 + y1 * y1 + 1.0f)) -
               std::atan2(x1 * y0, std::sqrt(x1 * x1 + y0 * y0 + 1.0f)) +
               std::atan2(x1 * y1, std::sqrt(x1 * x1 + y1 * y1 + 1.0f));
    };
    std::function<float(const glm::vec3&, int)> shBasis = [&](const glm::vec3& n, int index) -> float {
        float x = n.z, y = n.x, z = n.y;
        constexpr float k01 = 0.282095f;
        constexpr float k02 = 0.488603f;
        constexpr float k03 = 1.092548f;
        constexpr float k04 = 0.315392f;
        constexpr float k05 = 0.546274f;
        
        switch (index) {
            case 0: return k01;
            case 1: return -k02 * y;
            case 2: return k02 * z;
            case 3: return -k02 * x;
            case 4: return k03 * x * y;
            case 5: return -k03 * y * z;
            case 6: return k04 * (3.0f * z * z - 1.0f);
            case 7: return -k03 * x * z;
            case 8: return k05 * (x * x - y * y);
            default: return 0.0f;
        }
    };
    std::array<glm::vec3, 9> shAccum{};
    for (int i = 0; i < 9; ++i) {
        shAccum[i] = glm::vec3(0.0f);
    }
    float invSize = 1.0f / static_cast<float>(cubemapSize);
#if defined(USE_OPENMP)
    #pragma omp parallel
    {
        std::array<glm::vec3, 9> localAccum{};
        for (int i = 0; i < 9; ++i) {
            localAccum[i] = glm::vec3(0.0f);
        }
        
        #pragma omp for collapse(2) schedule(static)
        for (int face = 0; face < 6; ++face) {
            for (int y = 0; y < static_cast<int>(cubemapSize); ++y) {
                for (int x = 0; x < static_cast<int>(cubemapSize); ++x) {
                    float u = (static_cast<float>(x) + 0.5f) * invSize * 2.0f - 1.0f;
                    float v = (static_cast<float>(y) + 0.5f) * invSize * 2.0f - 1.0f;
                    glm::vec3 dir = cubemapTexelToDirection(static_cast<uint32_t>(face), u, v);
                    float solidAngle = texelSolidAngle(u, v, invSize);
                    size_t pixelOffset = (face * cubemapSize * cubemapSize + y * cubemapSize + x) * 4;
                    float r = halfToFloat(pixelData[pixelOffset + 0]);
                    float g = halfToFloat(pixelData[pixelOffset + 1]);
                    float b = halfToFloat(pixelData[pixelOffset + 2]);
                    if (std::isnan(r) || std::isnan(g) || std::isnan(b)) continue;
                    glm::vec3 color(r, g, b);
                    for (int i = 0; i < 9; ++i) {
                        float basis = shBasis(dir, i);
                        localAccum[i] += color * basis * solidAngle;
                    }
                }
            }
        }
        #pragma omp critical
        {
            for (int i = 0; i < 9; ++i) {
                shAccum[i] += localAccum[i];
            }
        }
    }
#else
    for (int face = 0; face < 6; ++face) {
        for (int y = 0; y < static_cast<int>(cubemapSize); ++y) {
            for (int x = 0; x < static_cast<int>(cubemapSize); ++x) {
                float u = (static_cast<float>(x) + 0.5f) * invSize * 2.0f - 1.0f;
                float v = (static_cast<float>(y) + 0.5f) * invSize * 2.0f - 1.0f;
                glm::vec3 dir = cubemapTexelToDirection(static_cast<uint32_t>(face), u, v);
                float solidAngle = texelSolidAngle(u, v, invSize);
                size_t pixelOffset = (face * cubemapSize * cubemapSize + y * cubemapSize + x) * 4;
                float r = halfToFloat(pixelData[pixelOffset + 0]);
                float g = halfToFloat(pixelData[pixelOffset + 1]);
                float b = halfToFloat(pixelData[pixelOffset + 2]);
                if (std::isnan(r) || std::isnan(g) || std::isnan(b)) continue;
                glm::vec3 color(r, g, b);
                for (int i = 0; i < 9; ++i) {
                    float basis = shBasis(dir, i);
                    shAccum[i] += color * basis * solidAngle;
                }
            }
        }
    }
#endif

    shCoeffs = shAccum;
    
    vkUnmapMemory(device, stagingMemory);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);
    stagingBuffer = VK_NULL_HANDLE;
    stagingMemory = VK_NULL_HANDLE;
    readbackRecorded = false;
}

engine::IrradianceProbeData engine::IrradianceProbe::getProbeData() const {
    IrradianceProbeData data;
    data.position = glm::vec4(getWorldPosition(), radius);
    for (int i = 0; i < 9; ++i) {
        data.shCoeffs[i] = glm::vec4(shCoeffs[i], 0.0f);
    }
    return data;
}