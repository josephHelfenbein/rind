#include <engine/Renderer.h>

#include <engine/InputManager.h>
#include <engine/EntityManager.h>
#include <engine/ParticleManager.h>
#include <engine/UIManager.h>
#include <engine/TextureManager.h>
#include <engine/ShaderManager.h>
#include <engine/SceneManager.h>
#include <engine/ModelManager.h>
#include <engine/Camera.h>
#include <engine/Light.h>
#include <engine/io.h>
#include <engine/PushConstants.h>
#include <engine/AudioManager.h>

#include <utility>
#include <unordered_set>
#include <iostream>
#include <array>
#include <typeindex>

engine::Renderer::Renderer(std::string windowTitle) : windowTitle(windowTitle) {}

engine::Renderer::~Renderer() {
    cleanup();
}

void engine::Renderer::run() {
    initWindow();
    initVulkan();
    mainLoop();
}

void engine::Renderer::cleanup() {
    if (device == VK_NULL_HANDLE && instance == VK_NULL_HANDLE) {
        if (window) {
            glfwDestroyWindow(window);
            window = nullptr;
        }
        glfwTerminate();
        return;
    }
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        for (VkFence fence : inFlightFences) {
            if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
        }
        inFlightFences.clear();
        for (VkSemaphore sem : imageAvailableSemaphores) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
        }
        imageAvailableSemaphores.clear();
        for (VkSemaphore sem : renderFinishedSemaphores) {
            if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
        }
        renderFinishedSemaphores.clear();

        if (uiVertexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, uiVertexBuffer, nullptr);
        if (uiVertexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(device, uiVertexBufferMemory, nullptr);
        uiVertexBuffer = VK_NULL_HANDLE;
        uiVertexBufferMemory = VK_NULL_HANDLE;
        if (uiIndexBuffer != VK_NULL_HANDLE) vkDestroyBuffer(device, uiIndexBuffer, nullptr);
        if (uiIndexBufferMemory != VK_NULL_HANDLE) vkFreeMemory(device, uiIndexBufferMemory, nullptr);
        uiIndexBuffer = VK_NULL_HANDLE;
        uiIndexBufferMemory = VK_NULL_HANDLE;
        for (auto& passPtr : managedRenderPasses) {
            if (!passPtr || !passPtr->images.has_value()) continue;
            for (auto& image : passPtr->images.value()) {
                if (image.imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, image.imageView, nullptr);
                    image.imageView = VK_NULL_HANDLE;
                }
                if (image.image != VK_NULL_HANDLE) {
                    vkDestroyImage(device, image.image, nullptr);
                    image.image = VK_NULL_HANDLE;
                }
                if (image.memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, image.memory, nullptr);
                    image.memory = VK_NULL_HANDLE;
                }
            }
        }
        managedRenderPasses.clear();

        for (VkImageView view : swapChainImageViews) {
            if (view != VK_NULL_HANDLE) vkDestroyImageView(device, view, nullptr);
        }
        swapChainImageViews.clear();
        if (swapChain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(device, swapChain, nullptr);
            swapChain = VK_NULL_HANDLE;
        }
        if (mainTextureSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, mainTextureSampler, nullptr);
            mainTextureSampler = VK_NULL_HANDLE;
        }
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }

        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }

    if (enableValidationLayers && instance != VK_NULL_HANDLE && debugMessenger != VK_NULL_HANDLE) {
        destroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
        debugMessenger = VK_NULL_HANDLE;
    }
    if (surface != VK_NULL_HANDLE && instance != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;
    }
    if (window) {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    glfwTerminate();
}

void engine::Renderer::initWindow() {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(WIDTH, HEIGHT, windowTitle.c_str(), nullptr, nullptr);
    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, framebufferResizeCallback);
    glfwSetCursorPosCallback(window, mouseMoveCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
}

void engine::Renderer::initVulkan() {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain(VK_NULL_HANDLE);
    createImageViews();
    std::vector<GraphicsShader> defaultShaders = shaderManager->createDefaultShaders();
    for (auto& shader : defaultShaders) {
        shaderManager->addGraphicsShader(std::move(shader));
    }
    shaderManager->resolveRenderGraphShaders();
    createAttachmentResources();
    createCommandPool();
    createMainTextureSampler();
    shaderManager->loadAllShaders();
    entityManager->createLightsUBO();
    textureManager->init();
    ensureFallback2DTexture();
    ensureFallbackShadowCubeTexture();
    particleManager->init();
    sceneManager->setActiveScene(0);
    uiManager->loadTextures();
    uiManager->loadFonts();
    entityManager->loadTextures();
    entityManager->createAllShadowMaps();
    createPostProcessDescriptorSets();
    modelManager->init();
    createCommandBuffers();
    createSyncObjects();
    createQuadResources();
}

void engine::Renderer::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        processInput(window);
        drawFrame();
    }
    vkDeviceWaitIdle(device);
}

void engine::Renderer::drawFrame() {
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] frame " << currentFrame << " start" << std::endl;
    }
    deltaTime = static_cast<float>(glfwGetTime() - lastFrameTime);
    lastFrameTime = glfwGetTime();
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] acquired imageIndex=" << imageIndex << " result=" << result << std::endl;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }
    vkResetFences(device, 1, &inFlightFences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] recordCommandBuffer begin imageIndex=" << imageIndex << std::endl;
    }
    recordCommandBuffer(commandBuffers[currentFrame], imageIndex);
    VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &imageAvailableSemaphores[currentFrame],
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffers[currentFrame],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinishedSemaphores[currentFrame]
    };
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit draw command buffer!");
    }
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] submit done" << std::endl;
    }
    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinishedSemaphores[currentFrame],
        .swapchainCount = 1,
        .pSwapchains = &swapChain,
        .pImageIndices = &imageIndex
    };
    result = vkQueuePresentKHR(presentQueue, &presentInfo);
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] present result=" << result << std::endl;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || framebufferResized) {
        framebufferResized = false;
        recreateSwapChain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to present swap chain image!");
    }
    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    entityManager->processPendingDeletions();
}

void engine::Renderer::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin recording command buffer!");
    }
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[record] frame=" << currentFrame << " imageIndex=" << imageIndex << " begin" << std::endl;
    }
    auto& renderGraph = shaderManager->getRenderGraph();
    const size_t nodeCount = renderGraph.size();
    const auto& roots = entityManager->getRootEntities();
    std::function<bool(const std::vector<Entity*>&)> hasRenderable3D = [&](const std::vector<Entity*>& nodes) -> bool {
        for (Entity* e : nodes) {
            const std::string& shaderName = e->getShader();
            const bool isGBufferShader = shaderName.empty() || shaderName == "gbuffer";
            if (e->getModel() && isGBufferShader) return true;
            if (hasRenderable3D(e->getChildren())) return true;
        }
        return false;
    };
    const bool has3DContent = hasRenderable3D(roots);
    bool gbufferRendered = false;

    entityManager->updateAll(deltaTime);
    audioManager->update();
    if (entityManager->getCamera()) {
        Camera* cam = entityManager->getCamera();
        glm::vec3 pos = cam->getWorldPosition();
        glm::vec3 fwd = -glm::normalize(glm::vec3(cam->getWorldTransform()[2]));
        glm::vec3 up = glm::normalize(glm::vec3(cam->getWorldTransform()[1]));
        audioManager->updateListener(pos, fwd, up);
    }
    particleManager->updateAll(deltaTime);
    entityManager->renderShadows(commandBuffer, currentFrame);
    entityManager->updateLightsUBO(currentFrame);

    for (size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        auto& node = renderGraph[nodeIdx];
        if (!node.passInfo) {
            continue;
        }
        const bool skip3DDraw = !node.is2D && !has3DContent;
        const bool isGBufferPass = node.passInfo && node.passInfo->name == "GBuffer";
        if (isGBufferPass && skip3DDraw) {
            // mark GBuffer as produced even when we only clear attachments for UI-only frames
            gbufferRendered = true;
        }
        std::vector<VkImageMemoryBarrier2> preBarriers;
        std::vector<VkImageMemoryBarrier2> postBarriers;

        if (DEBUG_RENDER_LOGS) {
            std::cout << "[record] nodeIdx=" << nodeIdx
                      << " pass=" << node.passInfo->name
                      << " is2D=" << node.is2D
                      << " skip3D=" << skip3DDraw
                      << " isGBuffer=" << isGBufferPass
                      << " gbufferRendered=" << gbufferRendered
                      << std::endl;
        }


        if (node.passInfo->usesSwapchain) {
            VkImageLayout currentLayout = swapChainImageLayouts.at(imageIndex);
            VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
            if (currentLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR) {
                srcStage = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
                srcAccess = VK_ACCESS_2_NONE;
            } else if (currentLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
                srcStage = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                srcAccess = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
            }
            preBarriers.push_back({
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = srcStage,
                .srcAccessMask = srcAccess,
                .dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = currentLayout,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapChainImages[imageIndex],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            });
            swapChainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
            postBarriers.push_back({
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .dstAccessMask = VK_ACCESS_2_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapChainImages[imageIndex],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            });
            swapChainImageLayouts[imageIndex] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        }
        if (node.passInfo->images.has_value()) {
            for (auto& image : node.passInfo->images.value()) {
                const bool isDepth = (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
                const VkImageAspectFlags aspect = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                const VkImageLayout attachmentLayout = isDepth
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
                if (image.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                    srcStage = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
                    srcAccess = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
                } else if (image.currentLayout == attachmentLayout) {
                    srcStage = isDepth
                        ? (VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)
                        : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
                    srcAccess = isDepth
                        ? (VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT)
                        : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
                }

                preBarriers.push_back({
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                    .srcStageMask = srcStage,
                    .srcAccessMask = srcAccess,
                    .dstStageMask = isDepth
                        ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                        : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                    .dstAccessMask = isDepth
                        ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                        : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                    .oldLayout = image.currentLayout,
                    .newLayout = attachmentLayout,
                    .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                    .image = image.image,
                    .subresourceRange = {
                        .aspectMask = aspect,
                        .baseMipLevel = 0,
                        .levelCount = image.mipLevels,
                        .baseArrayLayer = 0,
                        .layerCount = image.arrayLayers
                    }
                });
                const bool isSampled = (image.usage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0;
                if (isSampled) {
                    postBarriers.push_back({
                        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                        .srcStageMask = isDepth
                            ? VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT
                            : VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                        .srcAccessMask = isDepth
                            ? VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                            : VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        .dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                        .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                        .oldLayout = attachmentLayout,
                        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                        .image = image.image,
                        .subresourceRange = {
                            .aspectMask = aspect,
                            .baseMipLevel = 0,
                            .levelCount = image.mipLevels,
                            .baseArrayLayer = 0,
                            .layerCount = image.arrayLayers
                        }
                    });
                    image.currentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                } else {
                    image.currentLayout = attachmentLayout;
                }
            }
        }
        if (!preBarriers.empty()) {
            std::vector<VkImageMemoryBarrier> legacyBarriers;
            legacyBarriers.reserve(preBarriers.size());
            VkPipelineStageFlags srcStages = 0;
            VkPipelineStageFlags dstStages = 0;
            for (const auto& b2 : preBarriers) {
                legacyBarriers.push_back({
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = static_cast<VkAccessFlags>(b2.srcAccessMask),
                    .dstAccessMask = static_cast<VkAccessFlags>(b2.dstAccessMask),
                    .oldLayout = b2.oldLayout,
                    .newLayout = b2.newLayout,
                    .srcQueueFamilyIndex = b2.srcQueueFamilyIndex,
                    .dstQueueFamilyIndex = b2.dstQueueFamilyIndex,
                    .image = b2.image,
                    .subresourceRange = b2.subresourceRange
                });
                srcStages |= static_cast<VkPipelineStageFlags>(b2.srcStageMask);
                dstStages |= static_cast<VkPipelineStageFlags>(b2.dstStageMask);
            }
            vkCmdPipelineBarrier(
                commandBuffer,
                srcStages,
                dstStages,
                0,
                0, nullptr,
                0, nullptr,
                static_cast<uint32_t>(legacyBarriers.size()),
                legacyBarriers.data()
            );
        }
        VkRenderingInfo renderingInfo = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .renderArea = {
                .offset = {0, 0},
                .extent = swapChainExtent
            },
            .layerCount = 1
        };
        VkRenderingAttachmentInfo swapColor{};
        if (node.passInfo->usesSwapchain) {
            swapColor = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = swapChainImageViews[imageIndex],
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = { .color = {0.0f, 0.0f, 0.0f, 1.0f} }
            };
            renderingInfo.colorAttachmentCount = 1;
            renderingInfo.pColorAttachments = &swapColor;
        } else {
            renderingInfo.colorAttachmentCount = static_cast<uint32_t>(node.passInfo->colorAttachments.size());
            renderingInfo.pColorAttachments = node.passInfo->colorAttachments.data();
            renderingInfo.pDepthAttachment = node.passInfo->hasDepthAttachment ? &node.passInfo->depthAttachment.value() : nullptr;
            if (node.passInfo->images.has_value() && !node.passInfo->images->empty()) {
                const auto& firstImage = (*node.passInfo->images)[0];
                renderingInfo.renderArea.extent = {
                    .width = firstImage.width == 0 ? swapChainExtent.width : firstImage.width,
                    .height = firstImage.height == 0 ? swapChainExtent.height : firstImage.height
                };
            }
        }

        fpCmdBeginRendering(commandBuffer, &renderingInfo);
        VkViewport viewport = {
            .x = static_cast<float>(renderingInfo.renderArea.offset.x),
            .y = static_cast<float>(renderingInfo.renderArea.offset.y),
            .width = static_cast<float>(renderingInfo.renderArea.extent.width),
            .height = static_cast<float>(renderingInfo.renderArea.extent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f
        };
        VkRect2D scissor = {
            .offset = renderingInfo.renderArea.offset,
            .extent = renderingInfo.renderArea.extent
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
        const bool skipLightingWork = (node.passInfo && node.passInfo->name == "LightingPass" && !has3DContent);

        const bool passIsInactive = node.passInfo && !node.passInfo->isActive;

        if (passIsInactive) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] pass " << node.passInfo->name << " is inactive, skipping draw (attachments cleared by loadOp)" << std::endl;
            }
        } else if (node.shaders.find(shaderManager->getGraphicsShader("particle")) != node.shaders.end()) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] rendering Particles" << std::endl;
            }
            particleManager->renderParticles(commandBuffer, currentFrame);
        } else if (node.is2D 
        && (node.shaders.find(shaderManager->getGraphicsShader("ui")) != node.shaders.end()
        || node.shaders.find(shaderManager->getGraphicsShader("text")) != node.shaders.end())
        ) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] rendering UI/Text pass" << std::endl;
            }
            uiManager->renderUI(commandBuffer, node, currentFrame);
        } else if (node.is2D) {
            if (skipLightingWork) {
                if (DEBUG_RENDER_LOGS) {
                    std::cout << "[record] skipping Lighting draw (UI-only frame)" << std::endl;
                }
                // no draw, attachments are cleared by loadOp.
            } else {
                if (DEBUG_RENDER_LOGS) {
                    std::cout << "[record] rendering generic 2D pass" << std::endl;
                }
                draw2DPass(commandBuffer, node);
            }
        } else if (skip3DDraw) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] skipping 3D draw for pass" << std::endl;
            }
        } else {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] rendering 3D entities" << std::endl;
            }
            entityManager->renderEntities(commandBuffer, node, currentFrame, DEBUG_RENDER_LOGS);
        }
        if (!skip3DDraw && isGBufferPass) {
            gbufferRendered = true;
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] gbufferRendered set true" << std::endl;
            }
        }
        fpCmdEndRendering(commandBuffer);

        if (DEBUG_RENDER_LOGS) {
            std::cout << "[record] end pass " << node.passInfo->name << std::endl;
        }

        if (!postBarriers.empty()) {
            std::vector<VkImageMemoryBarrier> legacyBarriers;
            legacyBarriers.reserve(postBarriers.size());
            VkPipelineStageFlags srcStages = 0;
            VkPipelineStageFlags dstStages = 0;
            for (const auto& b2 : postBarriers) {
                legacyBarriers.push_back({
                    .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                    .srcAccessMask = static_cast<VkAccessFlags>(b2.srcAccessMask),
                    .dstAccessMask = static_cast<VkAccessFlags>(b2.dstAccessMask),
                    .oldLayout = b2.oldLayout,
                    .newLayout = b2.newLayout,
                    .srcQueueFamilyIndex = b2.srcQueueFamilyIndex,
                    .dstQueueFamilyIndex = b2.dstQueueFamilyIndex,
                    .image = b2.image,
                    .subresourceRange = b2.subresourceRange
                });
                srcStages |= static_cast<VkPipelineStageFlags>(b2.srcStageMask);
                dstStages |= static_cast<VkPipelineStageFlags>(b2.dstStageMask);
            }
            vkCmdPipelineBarrier(
                commandBuffer,
                srcStages,
                dstStages,
                0,
                0, nullptr,
                0, nullptr,
                static_cast<uint32_t>(legacyBarriers.size()),
                legacyBarriers.data()
            );
        }
    }

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to record command buffer!");
    }
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[record] command buffer end" << std::endl;
    }
}

void engine::Renderer::draw2DPass(VkCommandBuffer commandBuffer, RenderNode& node) {
    for (GraphicsShader* shader : node.shaders) {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
        std::type_index type = shader->config.pushConstantType;
        if (type == std::type_index(typeid(LightingPC))) {
            Camera* camera = entityManager->getCamera();
            if (camera) {
                glm::mat4 invView = glm::inverse(camera->getViewMatrix());
                glm::mat4 invProj = glm::inverse(camera->getProjectionMatrix());
                LightingPC pc = {
                    .invView = invView,
                    .invProj = invProj,
                    .camPos = camera->getWorldPosition()
                };
                vkCmdPushConstants(
                    commandBuffer,
                    shader->pipelineLayout,
                    shader->config.pushConstantRange.stageFlags,
                    0,
                    sizeof(LightingPC),
                    &pc
                );
            }
        } else if (type == std::type_index(typeid(SSRPC))) {
            Camera* camera = entityManager->getCamera();
            if (camera) {
                glm::mat4 invView = glm::inverse(camera->getViewMatrix());
                glm::mat4 invProj = glm::inverse(camera->getProjectionMatrix());
                SSRPC pc = {
                    .view = camera->getViewMatrix(),
                    .proj = camera->getProjectionMatrix(),
                    .invView = invView,
                    .invProj = invProj
                };
                vkCmdPushConstants(
                    commandBuffer,
                    shader->pipelineLayout,
                    shader->config.pushConstantRange.stageFlags,
                    0,
                    sizeof(SSRPC),
                    &pc
                );
            }
        } else if (type == std::type_index(typeid(AOPC))) {
            Camera* camera = entityManager->getCamera();
            if (camera) {
                glm::mat4 invProj = glm::inverse(camera->getProjectionMatrix());
                glm::mat4 proj = camera->getProjectionMatrix();
                AOPC pc = {
                    .invProj = invProj,
                    .proj = proj,
                    .radius = aoRadius,
                    .bias = aoBias,
                    .intensity = aoIntensity,
                    .flags = aoMode
                };
                vkCmdPushConstants(
                    commandBuffer,
                    shader->pipelineLayout,
                    shader->config.pushConstantRange.stageFlags,
                    0,
                    sizeof(AOPC),
                    &pc
                );
            }
        } else if (type == std::type_index(typeid(CompositePC))) {
            CompositePC pc = {
                .inverseScreenSize = glm::vec2(1.0f / static_cast<float>(swapChainExtent.width), 1.0f / static_cast<float>(swapChainExtent.height)),
                .flags = isFXAAEnabled() ? 1u : 0u
            };
            vkCmdPushConstants(
                commandBuffer,
                shader->pipelineLayout,
                shader->config.pushConstantRange.stageFlags,
                0,
                sizeof(CompositePC),
                &pc
            );
        }
        if (!shader->descriptorSets.empty()) {
            const uint32_t dsIndex = std::min<uint32_t>(currentFrame, static_cast<uint32_t>(shader->descriptorSets.size() - 1));
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[draw2DPass] shader=" << shader->name << " bind DS count=1 idx=" << dsIndex
                          << " handle=" << shader->descriptorSets[dsIndex] << std::endl;
            }
            vkCmdBindDescriptorSets(
                commandBuffer,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                shader->pipelineLayout,
                0,
                1,
                &shader->descriptorSets[dsIndex],
                0,
                nullptr
            );
        } else if (DEBUG_RENDER_LOGS) {
            std::cout << "[draw2DPass] shader=" << shader->name << " has NO descriptor sets" << std::endl;
        }
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}

void engine::Renderer::createInstance() {
    if (!glfwVulkanSupported()) {
        throw std::runtime_error("GLFW: Vulkan not supported");
    }
    if (enableValidationLayers && !checkValidationLayerSupport()) {
        throw std::runtime_error("Validation layers requested, but not available!");
    }
    VkApplicationInfo appInfo = {
        .pApplicationName = windowTitle.c_str(),
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "Rind Engine",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3
    };
    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &appInfo
    };
    #ifdef __APPLE__
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    #endif
    std::vector<const char*> extensions = getRequiredExtensions();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        populateDebugMessengerCreateInfo(debugCreateInfo);
        createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
    } else {
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;
    }
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create Vulkan instance!");
    }
}

void engine::Renderer::setupDebugMessenger() {
    if (!enableValidationLayers) return;
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    populateDebugMessengerCreateInfo(createInfo);
    if (createDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &debugMessenger) != VK_SUCCESS) {
        throw std::runtime_error("Failed to set up debug messenger!");
    }
}

void engine::Renderer::createSurface() {
    if (!window) throw std::runtime_error("GLFW window not initialized!");
    VkResult result = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
}

void engine::Renderer::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        throw std::runtime_error("Failed to find GPUs with Vulkan support!");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    std::multimap<int, VkPhysicalDevice> candidates;
    for (const auto& device : devices) {
        int score = rateDeviceSuitability(device);
        candidates.insert(std::make_pair(score, device));
    }
    if (candidates.rbegin()->first > 0) {
        physicalDevice = candidates.rbegin()->second;
    } else {
        throw std::runtime_error("Failed to find a suitable GPU!");
    }
}

void engine::Renderer::createLogicalDevice() {
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
    std::set<uint32_t> uniqueQueueFamilies = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };
    float queuePriority = 1.0f;
    for (uint32_t queueFamily : uniqueQueueFamilies) {
        VkDeviceQueueCreateInfo queueCreateInfo = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = queueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority
        };
        queueCreateInfos.push_back(queueCreateInfo);
    }
    VkPhysicalDeviceFeatures deviceFeatures = {
        .sampleRateShading = VK_TRUE,
        .samplerAnisotropy = VK_TRUE
    };
    bool enableAtomicFloatExt = false;
    bool canUseBufferFloat32AtomicAdd = false;
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT atomicFloatFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT
    };
    VkPhysicalDeviceVulkan13Features vulkan13Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE
    };
    vulkan13Features.pNext = &atomicFloatFeatures;
    VkPhysicalDeviceFeatures2 deviceFeatures2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan13Features
    };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);
    if (hasDeviceExtension(physicalDevice, VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME)) {
        enableAtomicFloatExt = true;
        canUseBufferFloat32AtomicAdd = atomicFloatFeatures.shaderBufferFloat32AtomicAdd == VK_TRUE;
    }
    useCASAdvection = !(enableAtomicFloatExt && canUseBufferFloat32AtomicAdd);
    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan13Features,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data()
    };
    VkPhysicalDeviceVulkan13Features enabledVulkan13Features = vulkan13Features;
    VkPhysicalDeviceFeatures2 enabledFeatures2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enabledVulkan13Features,
        .features = deviceFeatures
    };
    VkPhysicalDeviceShaderAtomicFloatFeaturesEXT enabledAtomicFloat{};
    if (!useCASAdvection && enableAtomicFloatExt && canUseBufferFloat32AtomicAdd) {
        enabledAtomicFloat = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_ATOMIC_FLOAT_FEATURES_EXT,
            .shaderBufferFloat32AtomicAdd = VK_TRUE
        };
        enabledAtomicFloat.pNext = enabledFeatures2.pNext;
        enabledFeatures2.pNext = &enabledAtomicFloat;
    }
    createInfo.pNext = &enabledFeatures2;
    createInfo.pEnabledFeatures = nullptr;
    std::vector<const char*> enabledExtensions = deviceExtensions;
    if (hasDeviceExtension(physicalDevice, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }
    if (hasDeviceExtension(physicalDevice, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    }
    if (!useCASAdvection && enableAtomicFloatExt) {
        enabledExtensions.push_back(VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME);
    }
    createInfo.enabledExtensionCount = static_cast<uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();
    if (enableValidationLayers) {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
    } else {
        createInfo.enabledLayerCount = 0;
    }
    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create logical device!");
    }
    vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue);
    vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue);
    fpCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(vkGetDeviceProcAddr(device, "vkCmdBeginRendering"));
    if (!fpCmdBeginRendering) {
        fpCmdBeginRendering = reinterpret_cast<PFN_vkCmdBeginRendering>(vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
    }
    fpCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(vkGetDeviceProcAddr(device, "vkCmdEndRendering"));
    if (!fpCmdEndRendering) {
        fpCmdEndRendering = reinterpret_cast<PFN_vkCmdEndRendering>(vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));
    }
    if (!fpCmdBeginRendering || !fpCmdEndRendering) {
        throw std::runtime_error("Dynamic rendering not available: vkCmdBeginRendering/End missing (core and KHR)");
    }
}

void engine::Renderer::createSwapChain(VkSwapchainKHR oldSwapchain) {
    SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);
    VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat(swapChainSupport.formats);
    VkPresentModeKHR presentMode = chooseSwapPresentMode(swapChainSupport.presentModes);
    VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
    uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
    if (swapChainSupport.capabilities.maxImageCount > 0 && imageCount > swapChainSupport.capabilities.maxImageCount) {
        imageCount = swapChainSupport.capabilities.maxImageCount;
    }
    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = swapChainSupport.capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain
    };
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.presentFamily.value()
    };
    if (indices.graphicsFamily != indices.presentFamily) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swap chain!");
    }
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
    swapChainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data());
    swapChainImageLayouts.assign(imageCount, VK_IMAGE_LAYOUT_UNDEFINED);
    swapChainImageFormat = surfaceFormat.format;
    swapChainExtent = extent;
}

void engine::Renderer::recreateSwapChain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(device);
    VkSwapchainKHR oldSwapchain = swapChain;
    for (VkImageView view : swapChainImageViews) {
        if (view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, view, nullptr);
        }
    }
    swapChainImageViews.clear();

    createSwapChain(oldSwapchain);
    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
    }

    createImageViews();
    createAttachmentResources();
    ensureFallback2DTexture();
    ensureFallbackShadowCubeTexture();
    createPostProcessDescriptorSets();
    if (entityManager && entityManager->getCamera()) {
        float newAspect = static_cast<float>(swapChainExtent.width) / static_cast<float>(swapChainExtent.height);
        entityManager->getCamera()->setAspectRatio(newAspect);
    }
    GraphicsShader* particleShader = shaderManager->getGraphicsShader("particle");
    if (particleShader && particleShader->descriptorPool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device, particleShader->descriptorPool, 0);
        particleManager->createParticleDescriptorSets();
    }
}

void engine::Renderer::refreshDescriptorSets() {
    vkDeviceWaitIdle(device);
    uiManager->loadTextures();
    createPostProcessDescriptorSets();
}

VkImageView engine::Renderer::getPassImageView(const std::string& shaderName, const std::string& attachmentName) {
    auto shader = shaderManager->getGraphicsShader(shaderName);
    if (!shader || !shader->config.passInfo || !shader->config.passInfo->images.has_value()) {
        return VK_NULL_HANDLE;
    }
    for (const auto& img : shader->config.passInfo->images.value()) {
        if (img.name == attachmentName) {
            return img.imageView;
        }
    }
    return VK_NULL_HANDLE;
}

void engine::Renderer::createImageViews() {
    swapChainImageViews.resize(swapChainImages.size());
    for (size_t i = 0; i < swapChainImages.size(); ++i) {
        swapChainImageViews[i] = createImageView(swapChainImages[i], swapChainImageFormat);
    }
}

VkCommandBuffer engine::Renderer::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);
    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT
    };
    vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void engine::Renderer::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    vkEndCommandBuffer(commandBuffer);
    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer
    };
    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
}

std::pair<VkImage, VkDeviceMemory> engine::Renderer::createImage(
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkSampleCountFlagBits samples,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    uint32_t arrayLayers,
    VkImageCreateFlags flags
) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = flags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = {
            .width = width,
            .height = height,
            .depth = 1
        },
        .mipLevels = mipLevels,
        .arrayLayers = arrayLayers,
        .samples = samples,
        .tiling = tiling,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    VkImage image;
    if (vkCreateImage(device, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }
    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, image, &memRequirements);
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
    };
    VkDeviceMemory imageMemory;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &imageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate image memory!");
    }
    vkBindImageMemory(device, image, imageMemory, 0);
    return std::make_pair(image, imageMemory);
}

std::pair<VkBuffer, VkDeviceMemory> engine::Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    VkBuffer buffer;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer!");
    }
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device, buffer, &memRequirements);
    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties)
    };
    VkDeviceMemory bufferMemory;
    if (vkAllocateMemory(device, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate buffer memory!");
    }
    vkBindBufferMemory(device, buffer, bufferMemory, 0);
    return std::make_pair(buffer, bufferMemory);
}

void engine::Renderer::transitionImageLayout(
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels,
    uint32_t layerCount
) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D16_UNORM ||
        format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
        format == VK_FORMAT_D32_SFLOAT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(format)) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        }
    };
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
    endSingleTimeCommands(commandBuffer);
}

void engine::Renderer::transitionImageLayoutInline(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkFormat format,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    uint32_t mipLevels,
    uint32_t layerCount
) {
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    if (format == VK_FORMAT_D16_UNORM ||
        format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
        format == VK_FORMAT_D32_SFLOAT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(format)) {
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        }
    };
    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else {
        throw std::invalid_argument("Unsupported layout transition!");
    }
    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage, destinationStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );
}

void engine::Renderer::copyDataToBuffer(
    void* data,
    VkDeviceSize size,
    VkBuffer buffer,
    VkDeviceMemory bufferMemory
) {
    void* mappedData;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    std::tie(stagingBuffer, stagingBufferMemory) = createBuffer(
        size,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    vkMapMemory(device, stagingBufferMemory, 0, size, 0, &mappedData);
    memcpy(mappedData, data, static_cast<size_t>(size));
    vkUnmapMemory(device, stagingBufferMemory);
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkBufferCopy copyRegion = {
        .srcOffset = 0,
        .dstOffset = 0,
        .size = size
    };
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, buffer, 1, &copyRegion);
    endSingleTimeCommands(commandBuffer);
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
}

void engine::Renderer::copyBufferToImage(
    VkBuffer buffer,
    VkImage image,
    uint32_t width,
    uint32_t height,
    uint32_t layerCount
) {
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {
            .width = width,
            .height = height,
            .depth = 1
        }
    };
    vkCmdCopyBufferToImage(
        commandBuffer,
        buffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );
    endSingleTimeCommands(commandBuffer);
}

std::pair<VkImage, VkDeviceMemory> engine::Renderer::createImageFromPixels(
    void* pixels,
    VkDeviceSize pixelSize,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    VkSampleCountFlagBits samples,
    VkFormat format,
    VkImageTiling tiling,
    VkImageUsageFlags usage,
    VkMemoryPropertyFlags properties,
    uint32_t arrayLayers,
    VkImageCreateFlags flags
) {
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    std::tie(stagingBuffer, stagingBufferMemory) = createBuffer(
        pixelSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    void* data;
    vkMapMemory(device, stagingBufferMemory, 0, pixelSize, 0, &data);
    memcpy(data, pixels, static_cast<size_t>(pixelSize));
    vkUnmapMemory(device, stagingBufferMemory);
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    std::tie(textureImage, textureImageMemory) = createImage(
        width,
        height,
        mipLevels,
        samples,
        format,
        tiling,
        usage | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        properties,
        arrayLayers,
        flags
    );
    transitionImageLayout(
        textureImage,
        format,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        mipLevels,
        arrayLayers
    );
    copyBufferToImage(
        stagingBuffer,
        textureImage,
        width,
        height,
        arrayLayers
    );
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingBufferMemory, nullptr);
    return std::make_pair(textureImage, textureImageMemory);
}

VkImageView engine::Renderer::createImageView(
    VkImage image,
    VkFormat format,
    VkImageAspectFlags aspectFlags,
    uint32_t mipLevels,
    VkImageViewType viewType,
    uint32_t layerCount
) {
    VkImageAspectFlags resolvedAspect = aspectFlags;
    if (format == VK_FORMAT_D16_UNORM ||
        format == VK_FORMAT_X8_D24_UNORM_PACK32 ||
        format == VK_FORMAT_D32_SFLOAT ||
        format == VK_FORMAT_D16_UNORM_S8_UINT ||
        format == VK_FORMAT_D24_UNORM_S8_UINT ||
        format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
        resolvedAspect = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (hasStencilComponent(format)) {
            resolvedAspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
    }
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = viewType,
        .format = format,
        .subresourceRange = {
            .aspectMask = resolvedAspect,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        }
    };
    VkImageView imageView;
    if (vkCreateImageView(device, &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture image view!");
    }
    return imageView;
}

VkSampler engine::Renderer::createTextureSampler(
    VkFilter magFilter,
    VkFilter minFilter,
    VkSamplerMipmapMode mipmapMode,
    VkSamplerAddressMode addressModeU,
    VkSamplerAddressMode addressModeV,
    VkSamplerAddressMode addressModeW,
    float mipLodBias,
    VkBool32 anisotropyEnable,
    float maxAnisotropy,
    VkBool32 compareEnable,
    VkCompareOp compareOp,
    float minLod,
    float maxLod,
    VkBorderColor borderColor,
    VkBool32 unnormalizedCoordinates
) {
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = magFilter,
        .minFilter = minFilter,
        .mipmapMode = mipmapMode,
        .addressModeU = addressModeU,
        .addressModeV = addressModeV,
        .addressModeW = addressModeW,
        .mipLodBias = 0.0f,
        .anisotropyEnable = anisotropyEnable,
        .maxAnisotropy = maxAnisotropy,
        .compareEnable = compareEnable,
        .compareOp = compareOp,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = borderColor,
        .unnormalizedCoordinates = unnormalizedCoordinates
    };
    VkSampler textureSampler;
    if (vkCreateSampler(device, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler!");
    }
    return textureSampler;
}

void engine::Renderer::createAttachmentResources() {
    auto destroyPassResources = [this](PassInfo& pass) {
        if (pass.images.has_value()) {
            for (auto& image : pass.images.value()) {
                if (image.imageView != VK_NULL_HANDLE) {
                    vkDestroyImageView(device, image.imageView, nullptr);
                    image.imageView = VK_NULL_HANDLE;
                }
                if (image.image != VK_NULL_HANDLE) {
                    vkDestroyImage(device, image.image, nullptr);
                    image.image = VK_NULL_HANDLE;
                }
                if (image.memory != VK_NULL_HANDLE) {
                    vkFreeMemory(device, image.memory, nullptr);
                    image.memory = VK_NULL_HANDLE;
                }
            }
        }
    };
    std::vector<GraphicsShader> shaders = shaderManager->getGraphicsShaders();
    managedRenderPasses.clear();
    managedRenderPasses.reserve(shaders.size());
    std::unordered_set<PassInfo*> processedPasses;
    for (auto& shader : shaders) {
        auto renderPassPtr = shader.config.passInfo;
        if (!renderPassPtr) {
            continue;
        }
        PassInfo* rawPtr = renderPassPtr.get();
        if (!processedPasses.insert(rawPtr).second) {
            continue;
        }
        managedRenderPasses.push_back(renderPassPtr);
        auto& renderPass = *renderPassPtr;
        destroyPassResources(renderPass);
        
        renderPass.colorAttachments.clear();
        renderPass.depthAttachment.reset();
        
        if (renderPass.images.has_value()) {
            renderPass.attachmentFormats.clear();
            renderPass.depthAttachmentFormat = VK_FORMAT_UNDEFINED;
            renderPass.hasDepthAttachment = false;
        }

        if (renderPass.usesSwapchain) {
            renderPass.attachmentFormats.push_back(swapChainImageFormat);
        }

        if (!renderPass.images.has_value()) {
            continue;
        }

        auto& images = renderPass.images.value();
        renderPass.colorAttachments.reserve(images.size());
        for (auto& image : images) {
            const uint32_t width = image.width == 0 ? swapChainExtent.width : image.width;
            const uint32_t height = image.height == 0 ? swapChainExtent.height : image.height;
            VkImage createdImage;
            VkDeviceMemory createdMemory;
            std::tie(createdImage, createdMemory) = createImage(
                width,
                height,
                image.mipLevels,
                image.samples,
                image.format,
                image.tiling,
                image.usage,
                image.properties,
                image.arrayLayers,
                image.flags
            );
            image.image = createdImage;
            image.memory = createdMemory;
            image.currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            const bool isDepthAttachment = (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            VkImageAspectFlags aspectMask = isDepthAttachment ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
            if (isDepthAttachment && hasStencilComponent(image.format)) {
                aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            image.imageView = createImageView(
                image.image,
                image.format,
                aspectMask,
                image.mipLevels,
                VK_IMAGE_VIEW_TYPE_2D,
                image.arrayLayers
            );

            VkRenderingAttachmentInfo attachmentInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = image.imageView,
                .imageLayout = isDepthAttachment ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = image.clearValue
            };

            if (isDepthAttachment) {
                renderPass.hasDepthAttachment = true;
                renderPass.depthAttachmentFormat = image.format;
                renderPass.depthAttachment = attachmentInfo;
            } else {
                renderPass.attachmentFormats.push_back(image.format);
                renderPass.colorAttachments.push_back(attachmentInfo);
            }
        }
    }
}

void engine::Renderer::createCommandPool() {
    QueueFamilyIndices queueFamilyIndices = findQueueFamilies(physicalDevice);
    VkCommandPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndices.graphicsFamily.value()
    };
    if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool!");
    }
}

void engine::Renderer::createMainTextureSampler() {
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_TRUE,
        .maxAnisotropy = 16.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };
    if (vkCreateSampler(device, &samplerInfo, nullptr, &mainTextureSampler) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create texture sampler!");
    }
}

void engine::Renderer::ensureFallbackShadowCubeTexture() {
    if (!textureManager) return;
    if (textureManager->getTexture("fallback_shadow_cube")) {
        return;
    }
    // 1x1x6 R32_SFLOAT cube map filled with 1.0 (max depth = far = no shadow)
    const uint32_t fallbackWidth = 1;
    const uint32_t fallbackHeight = 1;
    const uint32_t layerCount = 6;
    VkImage cubeImage;
    VkDeviceMemory cubeMemory;
    std::tie(cubeImage, cubeMemory) = createImage(
        fallbackWidth,
        fallbackHeight,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        layerCount,
        VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    );
    VkCommandBuffer cmdBuf = beginSingleTimeCommands();
    VkImageMemoryBarrier toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = cubeImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        }
    };
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);
    VkClearColorValue clearValue = {{1.0f, 0.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange clearRange = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = layerCount
    };
    vkCmdClearColorImage(cmdBuf, cubeImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clearValue, 1, &clearRange);
    VkImageMemoryBarrier toShaderRead = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = cubeImage,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        }
    };
    vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toShaderRead);
    endSingleTimeCommands(cmdBuf);

    VkImageView cubeView = createImageView(
        cubeImage,
        VK_FORMAT_R32_SFLOAT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        VK_IMAGE_VIEW_TYPE_CUBE,
        6
    );

    Texture fallbackCube = {
        .path = "fallback_shadow_cube",
        .image = cubeImage,
        .imageView = cubeView,
        .imageMemory = cubeMemory,
        .imageSampler = VK_NULL_HANDLE,
        .format = VK_FORMAT_R32_SFLOAT,
        .width = 1,
        .height = 1
    };
    textureManager->registerTexture("fallback_shadow_cube", fallbackCube);
}

void engine::Renderer::ensureFallback2DTexture() {
    if (!textureManager) return;
    if (textureManager->getTexture("fallback_white_2d")) {
        return;
    }

    // 1x1 white RGBA texture for sampled image fallbacks.
    const std::array<uint8_t, 4> pixel = {255, 255, 255, 255};
    VkImage texImage;
    VkDeviceMemory texMemory;
    std::tie(texImage, texMemory) = createImageFromPixels(
        const_cast<uint8_t*>(pixel.data()),
        pixel.size(),
        1,
        1,
        1,
        VK_SAMPLE_COUNT_1_BIT,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        1,
        0
    );
    transitionImageLayout(
        texImage,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        1,
        1
    );

    VkImageView texView = createImageView(
        texImage,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_ASPECT_COLOR_BIT,
        1,
        VK_IMAGE_VIEW_TYPE_2D,
        1
    );
    VkSampler texSampler = createTextureSampler(
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

    Texture fallbackTex = {
        .path = "fallback_white_2d",
        .image = texImage,
        .imageView = texView,
        .imageMemory = texMemory,
        .imageSampler = texSampler,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .width = 1,
        .height = 1
    };
    textureManager->registerTexture("fallback_white_2d", fallbackTex);
}

void engine::Renderer::createPostProcessDescriptorSets() {
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[Debug] createPostProcessDescriptorSets starting..." << std::endl;
    }
    auto shaders = shaderManager->getGraphicsShaders();
    for (const auto& shaderCopy : shaders) {
        if (shaderCopy.config.inputBindings.empty()) continue;
        if (DEBUG_RENDER_LOGS) {
            std::cout << "[Debug] Processing shader: " << shaderCopy.name << std::endl;
        }

        auto shader = shaderManager->getGraphicsShader(shaderCopy.name);
        if (!shader) continue;

        if (shader->descriptorPool != VK_NULL_HANDLE) {
            VkResult poolReset = vkResetDescriptorPool(device, shader->descriptorPool, 0);
            if (poolReset != VK_SUCCESS) {
                throw std::runtime_error("Failed to reset descriptor pool for shader '" + shader->name + "'!");
            }
        }

        shader->descriptorSets.clear();

        const int vertexBindings = std::max(shader->config.vertexBitBindings, 0);
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

        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES_IN_FLIGHT, shader->descriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = shader->descriptorPool,
            .descriptorSetCount = static_cast<uint32_t>(MAX_FRAMES_IN_FLIGHT),
            .pSetLayouts = layouts.data()
        };

        shader->descriptorSets.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateDescriptorSets(device, &allocInfo, shader->descriptorSets.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate descriptor sets for shader '" + shader->name + "'!");
        }

        size_t maxImageInfosPerFrame = 0;
        for (int frag = 0; frag < fragmentBindings; ++frag) {
            maxImageInfosPerFrame += getFragmentCount(frag);
        }
        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(MAX_FRAMES_IN_FLIGHT * maxImageInfosPerFrame + 4u);

        std::vector<VkWriteDescriptorSet> descriptorWrites;
        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.reserve(static_cast<size_t>(MAX_FRAMES_IN_FLIGHT * std::max(vertexBindings, 1)));
        descriptorWrites.reserve(static_cast<size_t>(MAX_FRAMES_IN_FLIGHT * (vertexBindings + fragmentBindings)));
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            std::vector<bool> fragmentBindingWritten(static_cast<size_t>(fragmentBindings), false);
            if (shader->name == "lighting") {
                auto& lightsBuffers = entityManager->getLightsBuffers();
                if (lightsBuffers.size() < MAX_FRAMES_IN_FLIGHT) {
                    entityManager->createLightsUBO();
                }
                if (i >= lightsBuffers.size() || lightsBuffers[i] == VK_NULL_HANDLE) {
                    std::cout << "Warning: Lights UBO buffer missing for frame " << i << " after ensure. Skipping descriptor write.\n";
                } else {
                    bufferInfos.push_back({
                        .buffer = lightsBuffers[i],
                        .offset = 0,
                        .range = sizeof(engine::LightsUBO)
                    });
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = 0,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        .pBufferInfo = &bufferInfos.back()
                    });
                }
            }
            for (const auto& binding : shader->config.inputBindings) {
                auto sourceShader = shaderManager->getGraphicsShader(binding.sourceShaderName);
                if (!sourceShader) {
                    std::cout << "Warning: Source shader '" << binding.sourceShaderName << "' for binding " << binding.binding << " in shader '" << shader->name << "' not found.\n";
                    continue;
                }

                auto renderPass = sourceShader->config.passInfo;
                if (!renderPass || !renderPass->images.has_value()) {
                    std::cout << "Warning: Render pass for shader '" << binding.sourceShaderName << "' has no images.\n";
                    continue;
                }

                VkImageView imageView = VK_NULL_HANDLE;
                for (const auto& img : renderPass->images.value()) {
                    if (img.name == binding.attachmentName) {
                        imageView = img.imageView;
                        break;
                    }
                }

                if (imageView == VK_NULL_HANDLE) {
                    std::cout << "Warning: Attachment '" << binding.attachmentName << "' not found in shader '" << binding.sourceShaderName << "'.\n";
                    continue;
                }

                const int fragIndex = static_cast<int>(binding.binding) - vertexBindings;
                VkDescriptorType descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                uint32_t descriptorCount = 1;
                if (fragIndex >= 0 && fragIndex < fragmentBindings) {
                    descriptorType = getFragmentType(fragIndex);
                    descriptorCount = getFragmentCount(fragIndex);
                    fragmentBindingWritten[static_cast<size_t>(fragIndex)] = true;
                }

                const size_t startIndex = imageInfos.size();
                switch (descriptorType) {
                    case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE: {
                        imageInfos.push_back({
                            .sampler = VK_NULL_HANDLE,
                            .imageView = imageView,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        });
                        break;
                    }
                    case VK_DESCRIPTOR_TYPE_SAMPLER: {
                        for (uint32_t c = 0; c < descriptorCount; ++c) {
                            imageInfos.push_back({ .sampler = mainTextureSampler });
                        }
                        break;
                    }
                    default: {
                        imageInfos.push_back({
                            .sampler = mainTextureSampler,
                            .imageView = imageView,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        });
                        break;
                    }
                }

                descriptorWrites.push_back({
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = shader->descriptorSets[i],
                    .dstBinding = binding.binding,
                    .dstArrayElement = 0,
                    .descriptorCount = descriptorCount,
                    .descriptorType = descriptorType,
                    .pImageInfo = &imageInfos[startIndex]
                });
                if (DEBUG_RENDER_LOGS) {
                    const uint64_t viewHandle = (uint64_t) imageView;
                    const uint64_t samplerHandle = descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER
                        ? (uint64_t) mainTextureSampler
                        : (uint64_t) imageInfos[startIndex].sampler;
                    std::cout << "[descriptors] shader=" << shader->name
                              << " frame=" << i
                              << " binding=" << binding.binding
                              << " type=" << descriptorType
                              << " imageView=0x" << std::hex << viewHandle << std::dec
                              << " sampler=0x" << std::hex << samplerHandle << std::dec
                              << std::endl;
                }
            }
            auto& lights = entityManager->getLights();
            for (int frag = 0; frag < fragmentBindings; ++frag) {
                if (fragmentBindingWritten[static_cast<size_t>(frag)]) continue;
                VkDescriptorType type = getFragmentType(frag);
                const uint32_t descriptorCount = getFragmentCount(frag);
                const size_t startIndex = imageInfos.size();

                if (type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    for (uint32_t c = 0; c < descriptorCount; ++c) {
                        imageInfos.push_back({ .sampler = mainTextureSampler });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = static_cast<uint32_t>(vertexBindings + frag),
                        .dstArrayElement = 0,
                        .descriptorCount = descriptorCount,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    continue;
                }

                if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    const bool isLightingShadowBinding = (shader->name == "lighting" && frag == 5);
                    
                    if (isLightingShadowBinding) {
                        Texture* fallbackTex = textureManager ? textureManager->getTexture("fallback_shadow_cube") : nullptr;
                        VkImageView fallbackView = (fallbackTex && fallbackTex->imageView != VK_NULL_HANDLE) ? fallbackTex->imageView : VK_NULL_HANDLE;

                        for (uint32_t c = 0; c < descriptorCount; ++c) {
                            VkImageView viewToBind = fallbackView;
                            if (c < lights.size()) {
                                Light* light = lights[c];
                                if (light && light->getShadowImageView() != VK_NULL_HANDLE) {
                                    viewToBind = light->getShadowImageView();
                                }
                            }
                            imageInfos.push_back({
                                .sampler = VK_NULL_HANDLE,
                                .imageView = viewToBind,
                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            });
                        }
                    } else {
                        Texture* fallbackTex = nullptr;
                        if (textureManager) {
                            fallbackTex = textureManager->getTexture("materials_default_albedo");
                        }
                        if (!fallbackTex && textureManager) {
                            fallbackTex = textureManager->getTexture("ui_window");
                        }
                        if (!fallbackTex && textureManager) {
                            fallbackTex = textureManager->getTexture("fallback_white_2d");
                        }
                        if (!fallbackTex || fallbackTex->imageView == VK_NULL_HANDLE || fallbackTex->image == VK_NULL_HANDLE) {
                            std::cout << "Warning: No fallback texture available for shader '" << shader->name << "' binding " << (vertexBindings + frag) << ". Skipping descriptor write.\n";
                            continue;
                        }
                        for (uint32_t c = 0; c < descriptorCount; ++c) {
                            imageInfos.push_back({
                                .sampler = (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? mainTextureSampler : VK_NULL_HANDLE,
                                .imageView = fallbackTex->imageView,
                                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            });
                        }
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = static_cast<uint32_t>(vertexBindings + frag),
                        .dstArrayElement = 0,
                        .descriptorCount = descriptorCount,
                        .descriptorType = type,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    if (DEBUG_RENDER_LOGS) {
                        const uint64_t viewHandle = (uint64_t) (imageInfos[startIndex].imageView);
                        const uint64_t samplerHandle = (uint64_t) (imageInfos[startIndex].sampler);
                        std::cout << "[descriptors] shader=" << shader->name
                                  << " frame=" << i
                                  << " binding=" << (vertexBindings + frag)
                                  << " type=" << type
                                  << " fallback imageView=0x" << std::hex << viewHandle << std::dec
                                  << " sampler=0x" << std::hex << samplerHandle << std::dec
                                  << " count=" << descriptorCount
                                  << std::endl;
                    }
                }
            }
        }

        if (!descriptorWrites.empty()) {
            vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
        }
    }
}

void engine::Renderer::createCommandBuffers() {
    commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = static_cast<uint32_t>(commandBuffers.size())
    };
    if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffers!");
    }
}

void engine::Renderer::createSyncObjects() {
    imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };
    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create synchronization objects for a frame!");
        }
    }
}

void engine::Renderer::createQuadResources() {
    float vertices[] = {
        // positions (unit quad centered at origin, size 1x1) // texCoords
        -0.5f, -0.5f,  0.0f, 0.0f,
         0.5f, -0.5f,  1.0f, 0.0f,
         0.5f,  0.5f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.0f, 1.0f
    };
    uint16_t indices[] = {
        0, 1, 2, 2, 3, 0
    };
    VkDeviceSize vertexBufferSize = sizeof(vertices);
    VkDeviceSize indexBufferSize = sizeof(indices);
    std::tie(uiVertexBuffer, uiVertexBufferMemory) = createBuffer(
        vertexBufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    void* vertexData;
    vkMapMemory(device, uiVertexBufferMemory, 0, vertexBufferSize, 0, &vertexData);
    memcpy(vertexData, vertices, static_cast<size_t>(vertexBufferSize));
    vkUnmapMemory(device, uiVertexBufferMemory);
    std::tie(uiIndexBuffer, uiIndexBufferMemory) = createBuffer(
        indexBufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    void* indexData;
    vkMapMemory(device, uiIndexBufferMemory, 0, indexBufferSize, 0, &indexData);
    memcpy(indexData, indices, static_cast<size_t>(indexBufferSize));
    vkUnmapMemory(device, uiIndexBufferMemory);
}

VkResult engine::Renderer::createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger) {
    auto func = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
    if (func != nullptr) {
        return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
    } else {
        return VK_ERROR_EXTENSION_NOT_PRESENT;
    }
}

void engine::Renderer::destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator) {
    auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
    if (func != nullptr) {
        func(instance, debugMessenger, pAllocator);
    }
}

std::vector<const char*> engine::Renderer::getRequiredExtensions() {
    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    #ifdef __APPLE__
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
    #endif
    return extensions;
}

uint32_t engine::Renderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for(uint32_t i=0; i<memProperties.memoryTypeCount; i++) {
        if(typeFilter & (1 << i) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type!");
}

bool engine::Renderer::checkValidationLayerSupport() {
    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());
    for (const char* layerName : validationLayers) {
        bool layerFound = false;
        for (const auto& layerProperties : availableLayers) {
            if (strcmp(layerName, layerProperties.layerName) == 0) {
                layerFound = true;
                break;
            }
        }
        if (!layerFound) {
            return false;
        }
    }
    return true;
}

void engine::Renderer::populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
    createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = debugCallback
    };
}

engine::Renderer::QueueFamilyIndices engine::Renderer::findQueueFamilies(VkPhysicalDevice device) {
    QueueFamilyIndices indices;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies.data());
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.isComplete()) {
            break;
        }
    }
    return indices;
}

bool engine::Renderer::hasStencilComponent(VkFormat format) {
    return format == VK_FORMAT_D32_SFLOAT_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT;
}

bool engine::Renderer::hasDeviceExtension(VkPhysicalDevice dev, const char* extensionName) {
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &extensionCount, availableExtensions.data());
    for (const auto& extension : availableExtensions) {
        if (strcmp(extension.extensionName, extensionName) == 0) {
            return true;
        }
    }
    return false;
}

engine::Renderer::SwapChainSupportDetails engine::Renderer::querySwapChainSupport(VkPhysicalDevice device) {
    SwapChainSupportDetails details;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
    if (formatCount != 0) {
        details.formats.resize(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
    }
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
    if (presentModeCount != 0) {
        details.presentModes.resize(presentModeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
    }
    return details;
}

VkSurfaceFormatKHR engine::Renderer::chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
    for (const auto& availableFormat : availableFormats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return availableFormat;
        }
    }
    return availableFormats[0];
}

VkPresentModeKHR engine::Renderer::chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D engine::Renderer::chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        return capabilities.currentExtent;
    } else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        VkExtent2D actualExtent = {
            .width = std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            .height = std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height)
        };
        return actualExtent;
    }
}

int engine::Renderer::rateDeviceSuitability(VkPhysicalDevice device) {
    VkPhysicalDeviceProperties deviceProperties;
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceProperties(device, &deviceProperties);
    vkGetPhysicalDeviceFeatures(device, &deviceFeatures);
    int score = 0;
    if (deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
        score += 1000;
    }
    #ifdef __APPLE__
    score += 500;
    #endif
    score += deviceProperties.limits.maxImageDimension2D;
    return score;
}

void engine::Renderer::processInput(GLFWwindow* window) {
    auto renderer = reinterpret_cast<engine::Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer->getHoveredObject() && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (ButtonObject* button = dynamic_cast<ButtonObject*>(renderer->getHoveredObject())) {
            button->click();
        }
    } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        renderer->toggleLockCursor(false);
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS
    && !renderer->inputManager->getCursorLocked() && !renderer->inputManager->getUIFocused()) {
        renderer->toggleLockCursor(true);
    }
    if (renderer && renderer->inputManager) {
        renderer->inputManager->processInput(window);
    }
}

void engine::Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto renderer = reinterpret_cast<engine::Renderer*>(glfwGetWindowUserPointer(window));
    renderer->framebufferResized = true;
}

void engine::Renderer::toggleLockCursor(bool lock) {
    if (lock) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    inputManager->setCursorLocked(lock);
    inputManager->resetMouseDelta();
}

void engine::Renderer::mouseMoveCallback(GLFWwindow* window, double xpos, double ypos) {
    auto renderer = reinterpret_cast<engine::Renderer*>(glfwGetWindowUserPointer(window));
    renderer->setHoveredObject(renderer->getUIManager()->processMouseMovement(window, xpos, ypos));
}
