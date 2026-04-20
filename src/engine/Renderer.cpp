#include <engine/Renderer.h>

#include <engine/InputManager.h>
#include <engine/EntityManager.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <engine/UIManager.h>
#include <engine/TextureManager.h>
#include <engine/ShaderManager.h>
#include <engine/SceneManager.h>
#include <engine/ModelManager.h>
#include <engine/Camera.h>
#include <engine/LightManager.h>
#include <engine/IrradianceManager.h>
#include <engine/io.h>
#include <engine/AudioManager.h>
#include <engine/SettingsManager.h>

#include <algorithm>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <array>
#include <set>
#include <thread>
#include <chrono>
#if defined(USE_OPENMP)
#include <omp.h>
#endif

engine::Renderer::Renderer(const std::string& windowTitle) : windowTitle(windowTitle) {}

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
        for (auto& frameSemaphores : crossQueueSegmentSemaphores) {
            for (VkSemaphore sem : frameSemaphores) {
                if (sem != VK_NULL_HANDLE) vkDestroySemaphore(device, sem, nullptr);
            }
            frameSemaphores.clear();
        }
        crossQueueSegmentSemaphores.clear();
        graphicsSegmentCommandBuffers.clear();
        computeSegmentCommandBuffers.clear();

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
        if (nearestSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, nearestSampler, nullptr);
            nearestSampler = VK_NULL_HANDLE;
        }
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
            commandPool = VK_NULL_HANDLE;
        }
        if (computeCommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, computeCommandPool, nullptr);
            computeCommandPool = VK_NULL_HANDLE;
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

void engine::Renderer::calibrateOpenMP() {
#if defined(USE_OPENMP)
    const int maxThreads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) / 2);
    const int N = 100000;

    std::vector<glm::vec4> a(N), b(N);
    for (int i = 0; i < N; ++i) {
        float fi = static_cast<float>(i);
        a[i] = glm::vec4(fi, fi * 0.5f, fi * 0.3f, 1.0f);
        b[i] = glm::vec4(fi * 0.2f, fi, fi * 0.7f, 1.0f);
    }

    int bestThreads = 1;
    double bestTime = std::numeric_limits<double>::max();
    const int warmupRuns = 10;
    const int timedRuns = 50;

    std::vector<int> threadCounts = {1};
    for (int t = 2; t <= maxThreads; t += 2) {
        threadCounts.push_back(t);
    }

    for (int numThreads : threadCounts) {
        omp_set_num_threads(numThreads);
        volatile float sink = 0.0f;

        for (int r = 0; r < warmupRuns; ++r) {
            float sum = 0.0f;
            #pragma omp parallel for reduction(+:sum)
            for (int i = 0; i < N; ++i) {
                sum += glm::dot(a[i], b[i]);
            }
            sink = sum;
        }

        auto start = std::chrono::high_resolution_clock::now();
        for (int r = 0; r < timedRuns; ++r) {
            float sum = 0.0f;
            #pragma omp parallel for reduction(+:sum)
            for (int i = 0; i < N; ++i) {
                sum += glm::dot(a[i], b[i]);
            }
            sink = sum;
        }
        auto end = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double, std::micro>(end - start).count();

        if (elapsed < bestTime) {
            bestTime = elapsed;
            bestThreads = numThreads;
        }
    }

    omp_set_num_threads(bestThreads);
    std::cout << "Ideal thread count: " << bestThreads << " (tested up to " << maxThreads << ")" << std::endl;
#endif
}

void engine::Renderer::initVulkan() {
    calibrateOpenMP();
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain(VK_NULL_HANDLE);
    createImageViews();
    createMainTextureSampler();
    createNearestSampler();
    createCommandPool();
    shaderManager->loadSMAATextures();
    shaderManager->createDefaultShaders();
    shaderManager->resolveRenderGraphShaders();
    buildRenderSubmitGraph();
    buildRenderAttachmentReadStages();
    createAttachmentResources();
    shaderManager->loadAllShaders();
    lightManager->createLightsUBO();
    textureManager->init();
    ensureFallback2DTexture();
    ensureFallbackShadowCubeTexture();
    particleManager->init();
    volumetricManager->init();
    modelManager->init();
    sceneManager->setActiveScene(0);
    uiManager->loadTextures();
    uiManager->loadFonts();
    entityManager->loadTextures();
    lightManager->createAllShadowMaps();
    irradianceManager->createAllIrradianceMaps();
    createPostProcessDescriptorSets();
    createCommandBuffers();
    createSyncObjects();
    createQuadResources();
}

void engine::Renderer::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        if (pendingScreenModeApply) {
            pendingScreenModeApply = false;
            applyScreenMode();
            recreateSwapChain();
        }
        processInput(window);
        drawFrame();
    }
    vkDeviceWaitIdle(device);
}

void engine::Renderer::buildRenderSubmitGraph() {
    renderSubmitGraph = {};
    if (!shaderManager) {
        return;
    }

    const auto& renderGraph = shaderManager->getRenderGraph();
    const auto& scheduledOrder = shaderManager->getScheduledNodeOrder();
    std::vector<size_t> fallbackOrder;
    const std::vector<size_t>* resolvedOrder = nullptr;
    if (scheduledOrder.size() == renderGraph.size()) {
        resolvedOrder = &scheduledOrder;
    } else {
        fallbackOrder.resize(renderGraph.size());
        for (size_t idx = 0; idx < fallbackOrder.size(); ++idx) {
            fallbackOrder[idx] = idx;
        }
        resolvedOrder = &fallbackOrder;
    }

    auto& submissions = renderSubmitGraph.submissions;
    submissions.reserve(resolvedOrder->size());
    for (size_t orderIdx = 0; orderIdx < resolvedOrder->size(); ++orderIdx) {
        const size_t nodeIdx = (*resolvedOrder)[orderIdx];
        if (nodeIdx >= renderGraph.size()) {
            continue;
        }
        const RenderNode& node = renderGraph[nodeIdx];
        const bool laneAllowsCompute = !node.lane || node.lane->allowCompute;
        const bool lanePreferAsync = node.lane && node.lane->preferAsync;
        const bool computeShaderOnlyNode = !node.usesRendering && !node.computeShaders.empty() && !node.customRenderFunc;
        const bool computeCapableCustomNode = !node.usesRendering && node.customRenderFunc && node.canRunCustomOnComputeQueue;
        const bool computeEligibleNode = computeShaderOnlyNode || computeCapableCustomNode;

        NodeQueueClass queueClass = NodeQueueClass::Graphics;
        if (hasAsyncComputeQueue && laneAllowsCompute && lanePreferAsync && computeEligibleNode) {
            queueClass = NodeQueueClass::Compute;
        }

        submissions.push_back({
            .nodeIdx = nodeIdx,
            .queueClass = queueClass,
            .dependencySubmissions = {},
            .incomingCrossQueueEdges = {},
            .outgoingCrossQueueEdges = {}
        });

        if (queueClass == NodeQueueClass::Compute) {
            ++renderSubmitGraph.computeSubmissionCount;
        } else {
            ++renderSubmitGraph.graphicsSubmissionCount;
        }
    }

    const size_t invalidSubmission = submissions.size();
    std::vector<size_t> nodeToSubmission(renderGraph.size(), invalidSubmission);
    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        nodeToSubmission[submissions[submissionIdx].nodeIdx] = submissionIdx;
    }

    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        RenderSubmitNode& submission = submissions[submissionIdx];
        const RenderNode& node = renderGraph[submission.nodeIdx];
        for (size_t depNodeIdx : node.resolvedDependencies) {
            if (depNodeIdx >= nodeToSubmission.size()) {
                continue;
            }
            const size_t depSubmissionIdx = nodeToSubmission[depNodeIdx];
            if (depSubmissionIdx == invalidSubmission || depSubmissionIdx == submissionIdx) {
                continue;
            }
            submission.dependencySubmissions.push_back(depSubmissionIdx);
        }
        std::sort(submission.dependencySubmissions.begin(), submission.dependencySubmissions.end());
        submission.dependencySubmissions.erase(
            std::unique(submission.dependencySubmissions.begin(), submission.dependencySubmissions.end()),
            submission.dependencySubmissions.end()
        );
    }

    auto hasDependencyPath = [&](size_t fromSubmission, size_t targetSubmission) {
        if (fromSubmission >= submissions.size() || targetSubmission >= submissions.size()) {
            return false;
        }
        std::vector<uint8_t> visited(submissions.size(), 0);
        std::vector<size_t> stack = { fromSubmission };
        while (!stack.empty()) {
            const size_t current = stack.back();
            stack.pop_back();
            if (current >= submissions.size() || visited[current]) {
                continue;
            }
            visited[current] = 1;
            for (size_t dep : submissions[current].dependencySubmissions) {
                if (dep == targetSubmission) {
                    return true;
                }
                if (dep < submissions.size() && !visited[dep]) {
                    stack.push_back(dep);
                }
            }
        }
        return false;
    };

    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        RenderSubmitNode& submission = submissions[submissionIdx];
        if (submission.dependencySubmissions.size() <= 1) {
            continue;
        }

        std::vector<size_t> reducedDependencies;
        reducedDependencies.reserve(submission.dependencySubmissions.size());
        for (size_t depCandidate : submission.dependencySubmissions) {
            bool isRedundant = false;
            for (size_t otherDep : submission.dependencySubmissions) {
                if (otherDep == depCandidate) {
                    continue;
                }
                if (hasDependencyPath(otherDep, depCandidate)) {
                    isRedundant = true;
                    break;
                }
            }
            if (!isRedundant) {
                reducedDependencies.push_back(depCandidate);
            }
        }
        submission.dependencySubmissions = std::move(reducedDependencies);
    }

    std::unordered_map<const RenderLane*, size_t> lastSubmissionByPreservedLane;
    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        RenderSubmitNode& submission = submissions[submissionIdx];
        const RenderNode& node = renderGraph[submission.nodeIdx];
        if (!node.lane || !node.lane->mustPreserveOrder) {
            continue;
        }

        const RenderLane* laneKey = node.lane.get();
        auto prevIt = lastSubmissionByPreservedLane.find(laneKey);
        if (prevIt != lastSubmissionByPreservedLane.end()) {
            submission.dependencySubmissions.push_back(prevIt->second);
            std::sort(submission.dependencySubmissions.begin(), submission.dependencySubmissions.end());
            submission.dependencySubmissions.erase(
                std::unique(submission.dependencySubmissions.begin(), submission.dependencySubmissions.end()),
                submission.dependencySubmissions.end()
            );
        }
        lastSubmissionByPreservedLane[laneKey] = submissionIdx;
    }

    auto& crossQueueEdges = renderSubmitGraph.crossQueueEdges;
    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        RenderSubmitNode& submission = submissions[submissionIdx];
        for (size_t depSubmissionIdx : submission.dependencySubmissions) {
            if (submissions[depSubmissionIdx].queueClass == submission.queueClass) {
                continue;
            }
            const size_t edgeIdx = crossQueueEdges.size();
            crossQueueEdges.push_back({
                .fromSubmission = depSubmissionIdx,
                .toSubmission = submissionIdx
            });
            submissions[depSubmissionIdx].outgoingCrossQueueEdges.push_back(edgeIdx);
            submission.incomingCrossQueueEdges.push_back(edgeIdx);
        }
    }

    std::vector<std::vector<size_t>> submissionDependents(submissions.size());
    std::vector<size_t> remainingDependencies(submissions.size(), 0);
    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        remainingDependencies[submissionIdx] = submissions[submissionIdx].dependencySubmissions.size();
        for (size_t depSubmissionIdx : submissions[submissionIdx].dependencySubmissions) {
            if (depSubmissionIdx < submissions.size()) {
                submissionDependents[depSubmissionIdx].push_back(submissionIdx);
            }
        }
    }

    std::vector<size_t> readySubmissions;
    readySubmissions.reserve(submissions.size());
    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        if (remainingDependencies[submissionIdx] == 0) {
            readySubmissions.push_back(submissionIdx);
        }
    }

    auto hasIncomingFromCompute = [&](const RenderSubmitNode& submission) {
        for (size_t edgeIdx : submission.incomingCrossQueueEdges) {
            if (edgeIdx >= crossQueueEdges.size()) {
                continue;
            }
            const size_t fromSubmission = crossQueueEdges[edgeIdx].fromSubmission;
            if (fromSubmission < submissions.size() && submissions[fromSubmission].queueClass == NodeQueueClass::Compute) {
                return true;
            }
        }
        return false;
    };

    auto earliestCrossQueueProducerOrder = [&](const RenderSubmitNode& submission) {
        size_t bestOrder = submissions.size();
        for (size_t edgeIdx : submission.incomingCrossQueueEdges) {
            if (edgeIdx >= crossQueueEdges.size()) {
                continue;
            }
            const size_t producerSubmission = crossQueueEdges[edgeIdx].fromSubmission;
            if (producerSubmission < bestOrder) {
                bestOrder = producerSubmission;
            }
        }
        return bestOrder;
    };

    auto pickNextReadySubmission = [&](const std::vector<size_t>& candidates) {
        return *std::min_element(
            candidates.begin(),
            candidates.end(),
            [&](size_t lhs, size_t rhs) {
                const RenderSubmitNode& lhsSubmission = submissions[lhs];
                const RenderSubmitNode& rhsSubmission = submissions[rhs];
                const RenderNode& lhsNode = renderGraph[lhsSubmission.nodeIdx];
                const RenderNode& rhsNode = renderGraph[rhsSubmission.nodeIdx];
                const bool lhsPreferAsyncLane = lhsNode.lane && lhsNode.lane->preferAsync;
                const bool rhsPreferAsyncLane = rhsNode.lane && rhsNode.lane->preferAsync;
                const bool lhsIncomingCompute = hasIncomingFromCompute(lhsSubmission);
                const bool rhsIncomingCompute = hasIncomingFromCompute(rhsSubmission);

                int lhsBucket = 3;
                int rhsBucket = 3;
                if (lhsSubmission.queueClass == NodeQueueClass::Graphics && !lhsIncomingCompute && lhsPreferAsyncLane) {
                    lhsBucket = 0;
                } else if (lhsSubmission.queueClass == NodeQueueClass::Compute) {
                    lhsBucket = 1;
                } else if (lhsSubmission.queueClass == NodeQueueClass::Graphics && !lhsIncomingCompute) {
                    lhsBucket = 2;
                }
                if (rhsSubmission.queueClass == NodeQueueClass::Graphics && !rhsIncomingCompute && rhsPreferAsyncLane) {
                    rhsBucket = 0;
                } else if (rhsSubmission.queueClass == NodeQueueClass::Compute) {
                    rhsBucket = 1;
                } else if (rhsSubmission.queueClass == NodeQueueClass::Graphics && !rhsIncomingCompute) {
                    rhsBucket = 2;
                }

                if (lhsBucket != rhsBucket) {
                    return lhsBucket < rhsBucket;
                }

                if (lhsSubmission.queueClass == NodeQueueClass::Compute && rhsSubmission.queueClass == NodeQueueClass::Compute) {
                    const size_t lhsProducerOrder = earliestCrossQueueProducerOrder(lhsSubmission);
                    const size_t rhsProducerOrder = earliestCrossQueueProducerOrder(rhsSubmission);
                    if (lhsProducerOrder != rhsProducerOrder) {
                        return lhsProducerOrder < rhsProducerOrder;
                    }
                    if (lhsSubmission.incomingCrossQueueEdges.size() != rhsSubmission.incomingCrossQueueEdges.size()) {
                        return lhsSubmission.incomingCrossQueueEdges.size() < rhsSubmission.incomingCrossQueueEdges.size();
                    }
                }

                return lhs < rhs;
            }
        );
    };

    auto& submissionOrder = renderSubmitGraph.submissionOrder;
    submissionOrder.reserve(submissions.size());
    while (!readySubmissions.empty()) {
        const size_t nextSubmission = pickNextReadySubmission(readySubmissions);
        readySubmissions.erase(std::find(readySubmissions.begin(), readySubmissions.end(), nextSubmission));
        submissionOrder.push_back(nextSubmission);
        for (size_t dependentIdx : submissionDependents[nextSubmission]) {
            if (remainingDependencies[dependentIdx] == 0) {
                continue;
            }
            remainingDependencies[dependentIdx]--;
            if (remainingDependencies[dependentIdx] == 0) {
                readySubmissions.push_back(dependentIdx);
            }
        }
    }

    if (submissionOrder.size() != submissions.size()) {
        submissionOrder.clear();
        submissionOrder.reserve(submissions.size());
        for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
            submissionOrder.push_back(submissionIdx);
        }
    }

    size_t imageAvailableWaitOrderPos = submissionOrder.size();
    for (size_t orderPos = 0; orderPos < submissionOrder.size(); ++orderPos) {
        const RenderNode& node = renderGraph[submissions[submissionOrder[orderPos]].nodeIdx];
        if (node.passInfo && node.passInfo->usesSwapchain) {
            imageAvailableWaitOrderPos = orderPos;
            break;
        }
    }
    if (imageAvailableWaitOrderPos == submissionOrder.size() && !submissionOrder.empty()) {
        for (size_t orderPos = 0; orderPos < submissionOrder.size(); ++orderPos) {
            if (submissions[submissionOrder[orderPos]].queueClass == NodeQueueClass::Graphics) {
                imageAvailableWaitOrderPos = orderPos;
                break;
            }
        }
        if (imageAvailableWaitOrderPos == submissionOrder.size()) {
            imageAvailableWaitOrderPos = 0;
        }
    }
    renderSubmitGraph.imageAvailableWaitOrderPos = imageAvailableWaitOrderPos;
    renderSubmitGraph.valid = true;
}

void engine::Renderer::buildRenderAttachmentReadStages() {
    renderNodeAttachmentReadStages.clear();
    if (!shaderManager) {
        return;
    }

    const auto& renderGraph = shaderManager->getRenderGraph();
    const size_t nodeCount = renderGraph.size();
    renderNodeAttachmentReadStages.resize(nodeCount);

    const VkPipelineStageFlags2 shaderReadStages =
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    for (size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx) {
        const RenderNode& node = renderGraph[nodeIdx];
        if (!node.passInfo || !node.passInfo->images.has_value()) {
            continue;
        }

        auto producerHasShaderName = [&](const std::string& shaderName) {
            return std::find(node.shaderNames.begin(), node.shaderNames.end(), shaderName) != node.shaderNames.end();
        };

        for (const auto& image : node.passInfo->images.value()) {
            VkPipelineStageFlags2 linkedStages = 0;

            for (size_t consumerIdx = 0; consumerIdx < nodeCount; ++consumerIdx) {
                if (consumerIdx == nodeIdx) {
                    continue;
                }

                const RenderNode& consumer = renderGraph[consumerIdx];
                if (std::find(consumer.resolvedDependencies.begin(), consumer.resolvedDependencies.end(), nodeIdx)
                    == consumer.resolvedDependencies.end()) {
                    continue;
                }

                bool readsViaGraphicsBinding = false;
                for (GraphicsShader* shader : consumer.shaders) {
                    if (!shader) {
                        continue;
                    }
                    for (const auto& binding : shader->config.inputBindings) {
                        if (binding.attachmentName != image.name) {
                            continue;
                        }
                        if (binding.sourceShaderName.empty() || !producerHasShaderName(binding.sourceShaderName)) {
                            continue;
                        }
                        readsViaGraphicsBinding = true;
                        break;
                    }
                    if (readsViaGraphicsBinding) {
                        break;
                    }
                }

                bool readsViaComputeBinding = false;
                for (ComputeShader* shader : consumer.computeShaders) {
                    if (!shader) {
                        continue;
                    }
                    for (const auto& binding : shader->config.inputBindings) {
                        if (binding.attachmentName != image.name) {
                            continue;
                        }
                        if (binding.sourceShaderName.empty() || !producerHasShaderName(binding.sourceShaderName)) {
                            continue;
                        }
                        readsViaComputeBinding = true;
                        break;
                    }
                    if (readsViaComputeBinding) {
                        break;
                    }
                }

                if (readsViaGraphicsBinding) {
                    linkedStages |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
                }
                if (readsViaComputeBinding) {
                    linkedStages |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                }
            }

            if (linkedStages == 0) {
                linkedStages = shaderReadStages;
            }
            renderNodeAttachmentReadStages[nodeIdx][image.name] = linkedStages;
        }
    }
}

void engine::Renderer::drawFrame() {
    auto shouldRebuildAttachments = [this]() {
        if (!shaderManager) return false;
        const auto& renderGraph = shaderManager->getRenderGraph();
        for (const auto& node : renderGraph) {
            if (!node.usesRendering || !node.passInfo) {
                continue;
            }
            if (node.passInfo->usesSwapchain) {
                continue;
            }

            bool hasValidColorAttachment = false;
            for (const auto& colorAttachment : node.passInfo->colorAttachments) {
                if (colorAttachment.imageView == VK_NULL_HANDLE) {
                    return true;
                }
                hasValidColorAttachment = true;
            }
            bool hasValidDepthAttachment = false;
            if (node.passInfo->hasDepthAttachment) {
                if (!node.passInfo->depthAttachment.has_value() || node.passInfo->depthAttachment->imageView == VK_NULL_HANDLE) {
                    return true;
                }
                hasValidDepthAttachment = true;
            }
            if (!hasValidColorAttachment && !hasValidDepthAttachment) {
                return true;
            }

            if (swapChainExtent.width > 1 && swapChainExtent.height > 1 && node.passInfo->images.has_value()) {
                for (const auto& image : *node.passInfo->images) {
                    const bool isAttachment =
                        (image.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0 ||
                        (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
                    if (!isAttachment) {
                        continue;
                    }
                    if (image.allocatedWidth <= 1 || image.allocatedHeight <= 1) {
                        return true;
                    }
                }
            }
        }
        return false;
    };
    if (shouldRebuildAttachments()) {
        recreateSwapChain();
        return;
    }
    entityManager->processPendingDeletions();
    entityManager->processPendingAdditions();
    if (shadowMapRecreationPending) {
        lightManager->createAllShadowMaps();
        vkDeviceWaitIdle(device);
        createPostProcessDescriptorSets();
        shadowMapRecreationPending = false;
    }
    if (irradianceManager->needsIrradianceBaking()) {
        entityManager->loadTextures();
        irradianceManager->createAllIrradianceMaps();
        VkCommandBuffer cmdBuffer = beginSingleTimeCommands();
        irradianceManager->bakeIrradianceMaps(cmdBuffer);
        irradianceManager->recordIrradianceReadback(cmdBuffer);
        endSingleTimeCommands(cmdBuffer);
        irradianceManager->processIrradianceSH();
        irradianceManager->setIrradianceBakingPending(false);
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            irradianceManager->updateIrradianceProbesUBO(i);
        }
        vkDeviceWaitIdle(device);
        createPostProcessDescriptorSets();
    }
    if (settingsManager->getSettings()->fpsLimit > 1e-6f) {
        double frameDuration = 1.0 / static_cast<double>(settingsManager->getSettings()->fpsLimit);
        double targetTime = lastFrameTime + frameDuration;
        double currentTime = glfwGetTime();
        double remainingTime = targetTime - currentTime;
        if (remainingTime > 0.002) {
            std::this_thread::sleep_for(std::chrono::duration<double>(remainingTime - 0.002));
        }
        while (glfwGetTime() < targetTime) {
            std::this_thread::yield();
        }
    }
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] frame " << currentFrame << " start" << std::endl;
    }
    vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);
    deltaTime = static_cast<float>(glfwGetTime()) - lastFrameTime;
    lastFrameTime = static_cast<float>(glfwGetTime());
    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] acquired imageIndex=" << imageIndex << " result=" << result << std::endl;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        recreateSwapChain();
        return;
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to acquire swap chain image!");
    }
    vkResetFences(device, 1, &inFlightFences[currentFrame]);
    vkResetCommandBuffer(commandBuffers[currentFrame], 0);

    const auto& renderGraph = shaderManager->getRenderGraph();
    bool submitGraphStale = !renderSubmitGraph.valid;
    if (!submitGraphStale) {
        for (const RenderSubmitNode& submission : renderSubmitGraph.submissions) {
            if (submission.nodeIdx >= renderGraph.size()) {
                submitGraphStale = true;
                break;
            }
        }
    }
    if (submitGraphStale) {
        buildRenderSubmitGraph();
        buildRenderAttachmentReadStages();
    }
    const auto& submissions = renderSubmitGraph.submissions;
    const auto& crossQueueEdges = renderSubmitGraph.crossQueueEdges;
    const auto& submissionOrder = renderSubmitGraph.submissionOrder;
    if (submissionOrder.empty()) {
        throw std::runtime_error("RenderSubmitGraph has no submissions!");
    }

    auto ensureCommandBufferCapacity = [&](std::vector<VkCommandBuffer>& buffers, uint32_t needed, VkCommandPool pool) {
        if (buffers.size() >= needed) {
            return;
        }
        const uint32_t allocateCount = needed - static_cast<uint32_t>(buffers.size());
        std::vector<VkCommandBuffer> newBuffers(allocateCount, VK_NULL_HANDLE);
        VkCommandBufferAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = allocateCount
        };
        if (vkAllocateCommandBuffers(device, &allocInfo, newBuffers.data()) != VK_SUCCESS) {
            throw std::runtime_error("Failed to allocate segmented command buffers!");
        }
        buffers.insert(buffers.end(), newBuffers.begin(), newBuffers.end());
    };

    std::vector<VkCommandBuffer>& frameGraphicsSegments = graphicsSegmentCommandBuffers[currentFrame];
    std::vector<VkCommandBuffer>& frameComputeSegments = computeSegmentCommandBuffers[currentFrame];
    const uint32_t graphicsSubmissionCount = renderSubmitGraph.graphicsSubmissionCount;
    const uint32_t computeSubmissionCount = renderSubmitGraph.computeSubmissionCount;
    const uint32_t extraGraphicsNeeded = graphicsSubmissionCount > 0 ? graphicsSubmissionCount - 1 : 0;
    ensureCommandBufferCapacity(frameGraphicsSegments, extraGraphicsNeeded, commandPool);
    ensureCommandBufferCapacity(frameComputeSegments, computeSubmissionCount, computeCommandPool);

    std::vector<VkCommandBuffer> submissionCommandBuffers(submissions.size(), VK_NULL_HANDLE);
    uint32_t usedGraphicsExtra = 0;
    uint32_t usedCompute = 0;
    bool usedPrimaryGraphicsBuffer = false;
    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        const RenderSubmitNode& submission = submissions[submissionIdx];
        if (submission.queueClass == NodeQueueClass::Compute) {
            submissionCommandBuffers[submissionIdx] = frameComputeSegments[usedCompute++];
        } else {
            if (!usedPrimaryGraphicsBuffer) {
                submissionCommandBuffers[submissionIdx] = commandBuffers[currentFrame];
                usedPrimaryGraphicsBuffer = true;
            } else {
                submissionCommandBuffers[submissionIdx] = frameGraphicsSegments[usedGraphicsExtra++];
            }
        }
        vkResetCommandBuffer(submissionCommandBuffers[submissionIdx], 0);
    }

    bool framePrepDone = false;
    for (size_t submissionIdx = 0; submissionIdx < submissions.size(); ++submissionIdx) {
        std::vector<size_t> nodeOrder = { submissions[submissionIdx].nodeIdx };
        recordCommandBuffer(submissionCommandBuffers[submissionIdx], imageIndex, &nodeOrder, !framePrepDone);
        framePrepDone = true;
    }

    std::vector<VkSemaphore>& frameBoundarySemaphores = crossQueueSegmentSemaphores[currentFrame];
    while (frameBoundarySemaphores.size() < crossQueueEdges.size()) {
        VkSemaphoreCreateInfo semaphoreInfo = {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
        };
        VkSemaphore newSemaphore = VK_NULL_HANDLE;
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &newSemaphore) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create segmented boundary semaphore!");
        }
        frameBoundarySemaphores.push_back(newSemaphore);
    }

    if (DEBUG_RENDER_LOGS) {
        std::cout << "[drawFrame] recordCommandBuffer begin imageIndex=" << imageIndex
                  << " submissions=" << submissions.size()
                  << " graphicsSubmissions=" << graphicsSubmissionCount
                  << " computeSubmissions=" << computeSubmissionCount
                  << " crossQueueEdges=" << crossQueueEdges.size() << std::endl;
    }

    auto submitNode = [&](VkQueue queue,
                          VkCommandBuffer cb,
                          const std::vector<VkSemaphore>& waitSemaphores,
                          const std::vector<VkPipelineStageFlags>& waitStages,
                          const std::vector<VkSemaphore>& signalSemaphores,
                          VkFence fence) {
        VkSubmitInfo submitInfo = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size()),
            .pWaitSemaphores = waitSemaphores.empty() ? nullptr : waitSemaphores.data(),
            .pWaitDstStageMask = waitStages.empty() ? nullptr : waitStages.data(),
            .commandBufferCount = 1,
            .pCommandBuffers = &cb,
            .signalSemaphoreCount = static_cast<uint32_t>(signalSemaphores.size()),
            .pSignalSemaphores = signalSemaphores.empty() ? nullptr : signalSemaphores.data()
        };
        return vkQueueSubmit(queue, 1, &submitInfo, fence);
    };

    size_t imageAvailableWaitOrderPos = renderSubmitGraph.imageAvailableWaitOrderPos;
    if (imageAvailableWaitOrderPos >= submissionOrder.size()) {
        imageAvailableWaitOrderPos = 0;
    }

    for (size_t orderPos = 0; orderPos < submissionOrder.size(); ++orderPos) {
        const size_t submissionIdx = submissionOrder[orderPos];
        const RenderSubmitNode& submission = submissions[submissionIdx];
        const bool isLastSubmission = orderPos + 1 == submissionOrder.size();

        std::vector<VkSemaphore> waitSemaphores;
        std::vector<VkPipelineStageFlags> waitStages;
        std::vector<VkSemaphore> signalSemaphores;

        const bool shouldWaitOnImageAvailable = orderPos == imageAvailableWaitOrderPos;
        if (shouldWaitOnImageAvailable) {
            waitSemaphores.push_back(imageAvailableSemaphores[currentFrame]);
            waitStages.push_back(submission.queueClass == NodeQueueClass::Compute
                ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        }

        for (size_t edgeIdx : submission.incomingCrossQueueEdges) {
            waitSemaphores.push_back(frameBoundarySemaphores[edgeIdx]);
            waitStages.push_back(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        }
        for (size_t edgeIdx : submission.outgoingCrossQueueEdges) {
            signalSemaphores.push_back(frameBoundarySemaphores[edgeIdx]);
        }
        if (isLastSubmission) {
            signalSemaphores.push_back(renderFinishedSemaphores[currentFrame]);
        }

        const VkFence submitFence = isLastSubmission ? inFlightFences[currentFrame] : VK_NULL_HANDLE;
        VkQueue submitQueue = submission.queueClass == NodeQueueClass::Compute ? computeQueue : graphicsQueue;
        if (submitNode(
                submitQueue,
                submissionCommandBuffers[submissionIdx],
                waitSemaphores,
                waitStages,
                signalSemaphores,
                submitFence) != VK_SUCCESS) {
            throw std::runtime_error("Failed to submit node command buffer!");
        }
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
    fpsFrameCount++;
}

void engine::Renderer::recordCommandBuffer(
    VkCommandBuffer commandBuffer,
    uint32_t imageIndex,
    const std::vector<size_t>* nodeOrder,
    bool doFramePrep
) {
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
    const auto& scheduledOrder = shaderManager->getScheduledNodeOrder();
    std::vector<size_t> fallbackOrder;
    const std::vector<size_t>* resolvedOrder = nodeOrder;
    if (!resolvedOrder) {
        if (scheduledOrder.size() == nodeCount) {
            resolvedOrder = &scheduledOrder;
        } else {
            fallbackOrder.resize(nodeCount);
            for (size_t idx = 0; idx < nodeCount; ++idx) {
                fallbackOrder[idx] = idx;
            }
            resolvedOrder = &fallbackOrder;
        }
    }

    if (doFramePrep && !paused) {
        entityManager->updateAll(deltaTime);
        audioManager->update();
        particleManager->updateAll(deltaTime);
        volumetricManager->updateAll(deltaTime);
    }
    if (doFramePrep) {
        particleManager->updateParticleBuffer(currentFrame);
        volumetricManager->updateVolumetricBuffer(currentFrame);
    }
    if (doFramePrep && entityManager->getCamera()) {
        Camera* cam = entityManager->getCamera();
        glm::vec3 pos = cam->getWorldPosition();
        glm::vec3 fwd = -glm::normalize(glm::vec3(cam->getWorldTransform()[2]));
        glm::vec3 up = glm::normalize(glm::vec3(cam->getWorldTransform()[1]));
        audioManager->updateListener(pos, fwd, up);
    }

    for (size_t nodeOrderIdx = 0; nodeOrderIdx < resolvedOrder->size(); ++nodeOrderIdx) {
        const size_t nodeIdx = (*resolvedOrder)[nodeOrderIdx];
        if (nodeIdx >= nodeCount) {
            continue;
        }
        auto& node = renderGraph[nodeIdx];
        if (!node.passInfo) {
            const bool skipDraw = node.skipCondition && node.skipCondition(this);
            if (!skipDraw && node.customRenderFunc && !node.usesRendering) {
                if (DEBUG_RENDER_LOGS) {
                    std::cout << "[record] executing passless custom render function for node '" << node.name << "'" << std::endl;
                }
                node.customRenderFunc(this, commandBuffer, currentFrame);
            }
            continue;
        }
        const bool skipDraw = node.skipCondition && node.skipCondition(this);
        std::vector<VkImageMemoryBarrier2> preBarriers;
        std::vector<VkImageMemoryBarrier2> postBarriers;

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
        if (node.usePassManagedTransitions && node.passInfo->images.has_value()) {
            const VkPipelineStageFlags2 shaderReadStages =
                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            for (auto& image : node.passInfo->images.value()) {
                VkPipelineStageFlags2 linkedReadStages = shaderReadStages;
                if (nodeIdx < renderNodeAttachmentReadStages.size()) {
                    auto stageIt = renderNodeAttachmentReadStages[nodeIdx].find(image.name);
                    if (stageIt != renderNodeAttachmentReadStages[nodeIdx].end()) {
                        linkedReadStages = stageIt->second;
                    }
                }
                const bool isDepth = (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
                const bool isStorage = (image.usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
                const VkImageAspectFlags aspect = isDepth
                    ? VK_IMAGE_ASPECT_DEPTH_BIT
                    : VK_IMAGE_ASPECT_COLOR_BIT;
                const VkImageLayout attachmentLayout = isDepth
                    ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

                if (isStorage) {
                    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
                    if (image.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                        srcStage = shaderReadStages;
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
                        .dstStageMask = node.storageWriteStage,
                        .dstAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
                        .oldLayout = image.currentLayout,
                        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
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
                    if (image.usage & VK_IMAGE_USAGE_SAMPLED_BIT){
                        postBarriers.push_back({
                            .srcStageMask = node.storageWriteStage,
                            .srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT,
                            .dstStageMask = linkedReadStages,
                            .dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
                            .oldLayout = VK_IMAGE_LAYOUT_GENERAL,
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
                        image.currentLayout = VK_IMAGE_LAYOUT_GENERAL;
                    }
                } else {
                    VkPipelineStageFlags2 srcStage = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
                    VkAccessFlags2 srcAccess = VK_ACCESS_2_NONE;
                    if (image.currentLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                        srcStage = shaderReadStages;
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
                            .dstStageMask = linkedReadStages,
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
        const bool usesRendering = node.usesRendering;
        bool beganRendering = false;
        bool renderingBlocked = false;
        VkRenderingInfo renderingInfo{};
        VkRenderingAttachmentInfo swapColor{};
        if (usesRendering) {
            renderingInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea = {
                    .offset = {0, 0},
                    .extent = swapChainExtent
                },
                .layerCount = 1
            };
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
                if (node.passInfo->images.has_value()) {
                    bool foundAttachmentExtent = false;
                    uint32_t attachmentWidth = 0;
                    uint32_t attachmentHeight = 0;
                    for (const auto& image : *node.passInfo->images) {
                        const bool isAttachment =
                            (image.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0 ||
                            (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
                        if (!isAttachment) {
                            continue;
                        }
                        uint32_t imageWidth = image.allocatedWidth;
                        uint32_t imageHeight = image.allocatedHeight;
                        if (imageWidth == 0 || imageHeight == 0) {
                            const uint32_t divider = image.resolutionDivider > 0 ? image.resolutionDivider : 1;
                            imageWidth = image.width == 0 ? std::max(1u, swapChainExtent.width / divider) : image.width;
                            imageHeight = image.height == 0 ? std::max(1u, swapChainExtent.height / divider) : image.height;
                        }
                        if (!foundAttachmentExtent) {
                            attachmentWidth = imageWidth;
                            attachmentHeight = imageHeight;
                            foundAttachmentExtent = true;
                        } else {
                            attachmentWidth = std::min(attachmentWidth, imageWidth);
                            attachmentHeight = std::min(attachmentHeight, imageHeight);
                        }
                    }
                    if (foundAttachmentExtent) {
                        renderingInfo.renderArea.extent = {
                            .width = attachmentWidth,
                            .height = attachmentHeight
                        };
                    }
                }
            }

            const bool hasColorAttachments = renderingInfo.colorAttachmentCount > 0 && renderingInfo.pColorAttachments != nullptr;
            const bool hasDepthAttachment = renderingInfo.pDepthAttachment != nullptr;
            if (!hasColorAttachments && !hasDepthAttachment) {
                if (DEBUG_RENDER_LOGS) {
                    std::cout << "[record] skipping pass '" << node.passInfo->name
                              << "' because it has no render attachments" << std::endl;
                }
                renderingBlocked = true;
            }

            bool hasInvalidAttachment = false;
            if (!renderingBlocked && hasColorAttachments) {
                for (uint32_t i = 0; i < renderingInfo.colorAttachmentCount; ++i) {
                    if (renderingInfo.pColorAttachments[i].imageView == VK_NULL_HANDLE) {
                        hasInvalidAttachment = true;
                        break;
                    }
                }
            }
            if (!renderingBlocked && !hasInvalidAttachment && hasDepthAttachment && renderingInfo.pDepthAttachment->imageView == VK_NULL_HANDLE) {
                hasInvalidAttachment = true;
            }
            if (hasInvalidAttachment) {
                if (DEBUG_RENDER_LOGS) {
                    std::cout << "[record] skipping pass '" << node.passInfo->name
                              << "' due to null attachment image view" << std::endl;
                }
                renderingBlocked = true;
            }

            if (!renderingBlocked && DEBUG_RENDER_LOGS) {
                std::cout << "[record] begin pass '" << node.passInfo->name
                          << "' extent=" << renderingInfo.renderArea.extent.width << "x"
                          << renderingInfo.renderArea.extent.height
                          << " colorCount=" << renderingInfo.colorAttachmentCount
                          << " hasDepth=" << (hasDepthAttachment ? 1 : 0) << std::endl;
            }

            if (!renderingBlocked) {
                fpCmdBeginRendering(commandBuffer, &renderingInfo);
                beganRendering = true;
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
            }
        }

        const bool passIsInactive = node.passInfo && !node.passInfo->isActive;
        const bool passCannotRender = usesRendering && !beganRendering;

        if (passIsInactive || skipDraw || passCannotRender) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] pass " << node.passInfo->name << " skipping draw (inactive, skip condition, or blocked rendering)" << std::endl;
            }
        } else if (node.customRenderFunc) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] executing custom render function for pass" << std::endl;
            }
            node.customRenderFunc(this, commandBuffer, currentFrame);
        } else if (!node.computeShaders.empty() && !node.usesRendering) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] dispatching compute pass" << std::endl;
            }
            dispatchComputePass(commandBuffer, node);
        } else if (node.is2D) {
            if (DEBUG_RENDER_LOGS) {
                std::cout << "[record] rendering generic 2D pass" << std::endl;
            }
            draw2DPass(commandBuffer, node);
        }
        if (beganRendering) {
            fpCmdEndRendering(commandBuffer);
        }

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
        if (shader->config.fillPushConstants) {
            shader->config.fillPushConstants(this, shader, commandBuffer);
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

void engine::Renderer::dispatchComputePass(VkCommandBuffer commandBuffer, RenderNode& node) {
    if (node.computeShaders.empty()) {
        return;
    }
    VkExtent2D extent = swapChainExtent;
    uint32_t width = extent.width;
    uint32_t height = extent.height;
    uint32_t layerLimit = 1;
    bool hasLayerLimit = false;
    if (node.passInfo && node.passInfo->images.has_value() && !node.passInfo->images->empty()) {
        const auto& img = node.passInfo->images->at(0);
        const uint32_t divider = img.resolutionDivider > 0 ? img.resolutionDivider : 1;
        width = img.width == 0 ? width / divider : img.width;
        height = img.height == 0 ? height / divider : img.height;
        layerLimit = img.arrayLayers > 0 ? img.arrayLayers : 1;
        hasLayerLimit = true;
    }
    if (width == 0 || height == 0) {
        return;
    }
    for (ComputeShader* shader : node.computeShaders) {
        if (!shader || shader->pipeline == VK_NULL_HANDLE) {
            continue;
        }
        if (shader->descriptorSets.empty()) {
            continue;
        }
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, shader->pipeline);
        if (shader->config.fillPushConstants) {
            shader->config.fillPushConstants(this, shader, commandBuffer);
        }
        uint32_t dsIndex = currentFrame;
        if (dsIndex >= shader->descriptorSets.size()) {
            dsIndex = static_cast<uint32_t>(shader->descriptorSets.size() - 1);
        }
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
        uint32_t wgX = shader->config.workgroupSizeX == 0 ? 1u : shader->config.workgroupSizeX;
        uint32_t wgY = shader->config.workgroupSizeY == 0 ? 1u : shader->config.workgroupSizeY;
        uint32_t wgZ = shader->config.workgroupSizeZ == 0 ? 1u : shader->config.workgroupSizeZ;
        uint32_t dispatchWidth = width;
        uint32_t dispatchHeight = height;
        if (shader->config.getDispatchWidth) {
            dispatchWidth = shader->config.getDispatchWidth(this, shader);
        }
        if (shader->config.getDispatchHeight) {
            dispatchHeight = shader->config.getDispatchHeight(this, shader);
        }
        if (dispatchWidth == 0u || dispatchHeight == 0u) {
            continue;
        }
        uint32_t groupX = (dispatchWidth + wgX - 1) / wgX;
        uint32_t groupY = (dispatchHeight + wgY - 1) / wgY;
        uint32_t dispatchLayers = 1;
        if (shader->config.getDispatchLayerCount) {
            dispatchLayers = shader->config.getDispatchLayerCount(this, shader);
        }
        if (hasLayerLimit) {
            dispatchLayers = std::min(dispatchLayers, layerLimit);
        }
        if (dispatchLayers == 0u) {
            continue;
        }
        uint32_t groupZ = (dispatchLayers + wgZ - 1) / wgZ;
        vkCmdDispatch(commandBuffer, groupX, groupY, groupZ);
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
    if (indices.computeFamily.has_value()) {
        uniqueQueueFamilies.insert(indices.computeFamily.value());
    }
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
        .samplerAnisotropy = VK_TRUE,
        .fragmentStoresAndAtomics = VK_TRUE,
        .shaderStorageImageReadWithoutFormat = VK_TRUE,
        .shaderStorageImageWriteWithoutFormat = VK_TRUE
    };
    VkPhysicalDeviceVulkan13Features vulkan13Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
        .synchronization2 = VK_TRUE,
        .dynamicRendering = VK_TRUE
    };
    VkPhysicalDeviceVulkan12Features vulkan12Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &vulkan13Features,
        .shaderFloat16 = VK_TRUE
    };
    VkPhysicalDeviceVulkan11Features vulkan11Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &vulkan12Features
    };
    VkPhysicalDeviceFeatures2 deviceFeatures2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vulkan11Features
    };
    vkGetPhysicalDeviceFeatures2(physicalDevice, &deviceFeatures2);
    if (vulkan12Features.shaderFloat16 != VK_TRUE) {
        std::cout << "Warning: shaderFloat16 is not supported; f16 compute shaders may fail to create.\n";
    }
    if (vulkan11Features.multiview != VK_TRUE) {
        throw std::runtime_error("Device does not support multiview, which is required for shadow rendering.");
    }
    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &vulkan12Features,
        .queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size()),
        .pQueueCreateInfos = queueCreateInfos.data()
    };
    VkPhysicalDeviceVulkan13Features enabledVulkan13Features = vulkan13Features;
    VkPhysicalDeviceVulkan12Features enabledVulkan12Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
        .pNext = &enabledVulkan13Features,
        .shaderFloat16 = vulkan12Features.shaderFloat16
    };
    VkPhysicalDeviceVulkan11Features enabledVulkan11Features = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &enabledVulkan12Features,
        .multiview = VK_TRUE
    };
    VkPhysicalDeviceFeatures2 enabledFeatures2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &enabledVulkan11Features,
        .features = deviceFeatures
    };
    createInfo.pNext = &enabledFeatures2;
    createInfo.pEnabledFeatures = nullptr;
    std::vector<const char*> enabledExtensions = deviceExtensions;
    if (hasDeviceExtension(physicalDevice, VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    }
    if (hasDeviceExtension(physicalDevice, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME)) {
        enabledExtensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
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
    uint32_t computeFamilyIndex = indices.computeFamily.value_or(indices.graphicsFamily.value());
    vkGetDeviceQueue(device, computeFamilyIndex, 0, &computeQueue);
    hasAsyncComputeQueue = indices.computeFamily.has_value() && indices.computeFamily.value() != indices.graphicsFamily.value();
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

void engine::Renderer::applyScreenMode() {
    if (!settingsManager) return;
    uint32_t mode = settingsManager->getSettings()->screenMode;
    if (mode == currentScreenMode) return;

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    const GLFWvidmode* vidmode = glfwGetVideoMode(monitor);
    if (!vidmode) return;

    if (mode == 0) {
        // Windowed
        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_TRUE);
        glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
        glfwSetWindowMonitor(window, nullptr, windowedPosX, windowedPosY, windowedWidth, windowedHeight, 0);
    } else {
        if (currentScreenMode == 0) {
            glfwGetWindowPos(window, &windowedPosX, &windowedPosY);
            glfwGetWindowSize(window, &windowedWidth, &windowedHeight);
        }
        if (mode == 1) {
            // Borderless
            glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
            glfwSetWindowMonitor(window, nullptr, 0, 0, vidmode->width, vidmode->height, 0);
        } else if (mode == 2) {
            // Fullscreen
            glfwSetWindowMonitor(window, monitor, 0, 0, vidmode->width, vidmode->height, vidmode->refreshRate);
        }
    }
    currentScreenMode = mode;
    clicking = false;
    ignoreMouseUntilRelease = true;
    if (inputManager) inputManager->resetKeyStates();
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
    uint32_t extentRetryCount = 0;
    while ((extent.width <= 1 || extent.height <= 1) && extentRetryCount < 120) {
        glfwWaitEventsTimeout(1.0 / 60.0);
        swapChainSupport = querySwapChainSupport(physicalDevice);
        extent = chooseSwapExtent(swapChainSupport.capabilities);
        createInfo.imageExtent = extent;
        ++extentRetryCount;
    }
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
    applyScreenMode();
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
    GraphicsShader* volumetricShader = shaderManager->getGraphicsShader("volumetric");
    if (volumetricShader && volumetricShader->descriptorPool != VK_NULL_HANDLE) {
        vkResetDescriptorPool(device, volumetricShader->descriptorPool, 0);
        volumetricManager->createVolumetricDescriptorSets();
    }
    inputManager->dispatchRecreateSwapChain();
}

void engine::Renderer::refreshDescriptorSets() {
    vkDeviceWaitIdle(device);
    uiManager->loadTextures();
    uiManager->reloadFontDescriptorSets();
    createPostProcessDescriptorSets();
}

void engine::Renderer::resetPostProcessDescriptorPools() {
    vkDeviceWaitIdle(device);
    auto shaders = shaderManager->getGraphicsShaders();
    for (const auto& shaderCopy : shaders) {
        if (shaderCopy.config.inputBindings.empty()) continue;
        auto shader = shaderManager->getGraphicsShader(shaderCopy.name);
        if (!shader) continue;
        if (shader->descriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(device, shader->descriptorPool, 0);
        }
        shader->descriptorSets.clear();
    }

    auto computeShaders = shaderManager->getComputeShaders();
    for (const auto& shaderCopy : computeShaders) {
        if (shaderCopy.config.inputBindings.empty()) continue;
        auto shader = shaderManager->getComputeShader(shaderCopy.name);
        if (!shader) continue;
        if (shader->descriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(device, shader->descriptorPool, 0);
        }
        shader->descriptorSets.clear();
    }
}

void engine::Renderer::resetPerObjectDescriptorPools() {
    vkDeviceWaitIdle(device);
    auto shaders = shaderManager->getGraphicsShaders();
    for (const auto& shaderCopy : shaders) {
        if (!shaderCopy.config.inputBindings.empty()) continue;
        if (shaderCopy.config.poolMultiplier <= 1) continue;
        auto shader = shaderManager->getGraphicsShader(shaderCopy.name);
        if (!shader) continue;
        if (shader->descriptorPool != VK_NULL_HANDLE) {
            vkResetDescriptorPool(device, shader->descriptorPool, 0);
        }
        shader->descriptorSets.clear();
    }
}

VkImageView engine::Renderer::getPassImageView(const std::string& shaderName, const std::string& attachmentName) {
    if (shaderManager) {
        const auto& renderGraph = shaderManager->getRenderGraph();
        for (const auto& node : renderGraph) {
            bool nameMatch = false;
            for (const auto& nodeName : node.shaderNames) {
                if (nodeName == shaderName) {
                    nameMatch = true;
                    break;
                }
            }
            if (!nameMatch || !node.passInfo || !node.passInfo->images.has_value()) {
                continue;
            }
            for (const auto& img : node.passInfo->images.value()) {
                if (img.name == attachmentName) {
                    return img.imageView;
                }
            }
        }

        auto shader = shaderManager->getGraphicsShader(shaderName);
        if (shader && shader->config.passInfo && shader->config.passInfo->images.has_value()) {
            for (const auto& img : shader->config.passInfo->images.value()) {
                if (img.name == attachmentName) {
                    return img.imageView;
                }
            }
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
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.computeFamily.value_or(indices.graphicsFamily.value())
    };
    const bool shareAcrossQueues = hasAsyncComputeQueue && queueFamilyIndices[0] != queueFamilyIndices[1];
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
        .sharingMode = shareAcrossQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = shareAcrossQueues ? 2u : 0u,
        .pQueueFamilyIndices = shareAcrossQueues ? queueFamilyIndices : nullptr,
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
    QueueFamilyIndices indices = findQueueFamilies(physicalDevice);
    uint32_t queueFamilyIndices[] = {
        indices.graphicsFamily.value(),
        indices.computeFamily.value_or(indices.graphicsFamily.value())
    };
    const bool shareAcrossQueues = hasAsyncComputeQueue && queueFamilyIndices[0] != queueFamilyIndices[1];
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = shareAcrossQueues ? VK_SHARING_MODE_CONCURRENT : VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = shareAcrossQueues ? 2u : 0u,
        .pQueueFamilyIndices = shareAcrossQueues ? queueFamilyIndices : nullptr
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
    const VkPipelineStageFlags shaderReadStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
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
        destinationStage = shaderReadStages;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = shaderReadStages;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = shaderReadStages;
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
    const VkPipelineStageFlags shaderReadStages = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
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
        destinationStage = shaderReadStages;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = shaderReadStages;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = shaderReadStages;
    } else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = shaderReadStages;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = shaderReadStages;
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
        destinationStage = shaderReadStages;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = shaderReadStages;
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
        sourceStage = shaderReadStages;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = shaderReadStages;
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

bool engine::Renderer::formatSupportsLinearBlit(VkFormat format) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);
    const VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT |
        VK_FORMAT_FEATURE_BLIT_SRC_BIT |
        VK_FORMAT_FEATURE_BLIT_DST_BIT;
    return (props.optimalTilingFeatures & required) == required;
}

void engine::Renderer::generateMipmaps(
    VkImage image,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t mipLevels,
    uint32_t layerCount
) {
    if (mipLevels <= 1) {
        return;
    }
    if (!formatSupportsLinearBlit(format)) {
        throw std::runtime_error("Texture format does not support linear blit for mipmap generation!");
    }
    VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = layerCount
        }
    };
    int32_t mipWidth = static_cast<int32_t>(width);
    int32_t mipHeight = static_cast<int32_t>(height);
    for (uint32_t i = 1; i < mipLevels; ++i) {
        barrier.subresourceRange.baseMipLevel = i - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier
        );
        const int32_t nextWidth = mipWidth > 1 ? mipWidth / 2 : 1;
        const int32_t nextHeight = mipHeight > 1 ? mipHeight / 2 : 1;
        VkImageBlit blit = {
            .srcSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i - 1,
                .baseArrayLayer = 0,
                .layerCount = layerCount
            },
            .srcOffsets = { {0, 0, 0}, {mipWidth, mipHeight, 1} },
            .dstSubresource = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .mipLevel = i,
                .baseArrayLayer = 0,
                .layerCount = layerCount
            },
            .dstOffsets = { {0, 0, 0}, {nextWidth, nextHeight, 1} }
        };
        vkCmdBlitImage(
            commandBuffer,
            image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1, &blit,
            VK_FILTER_LINEAR
        );
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier
        );
        mipWidth = nextWidth;
        mipHeight = nextHeight;
    }
    barrier.subresourceRange.baseMipLevel = mipLevels - 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier
    );
    endSingleTimeCommands(commandBuffer);
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
        .mipLodBias = mipLodBias,
        .anisotropyEnable = anisotropyEnable,
        .maxAnisotropy = maxAnisotropy,
        .compareEnable = compareEnable,
        .compareOp = compareOp,
        .minLod = minLod,
        .maxLod = maxLod,
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
                image.allocatedWidth = 0;
                image.allocatedHeight = 0;
            }
        }
    };
    const auto& passes = shaderManager->getRenderPasses();
    managedRenderPasses.clear();
    managedRenderPasses.reserve(passes.size());
    std::unordered_set<PassInfo*> processedPasses;
    for (const auto& renderPassPtr : passes) {
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
            const uint32_t divider = image.resolutionDivider > 0 ? image.resolutionDivider : 1;
            const uint32_t width = image.width == 0 ? std::max(1u, swapChainExtent.width / divider) : image.width;
            const uint32_t height = image.height == 0 ? std::max(1u, swapChainExtent.height / divider) : image.height;
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
            image.allocatedWidth = width;
            image.allocatedHeight = height;

            const bool isDepthAttachment = (image.usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
            const bool isColorAttachment = (image.usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0;
            VkImageAspectFlags aspectMask = isDepthAttachment
                ? VK_IMAGE_ASPECT_DEPTH_BIT
                : VK_IMAGE_ASPECT_COLOR_BIT;
            if (isDepthAttachment && hasStencilComponent(image.format)) {
                aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            }

            if (image.arrayLayers > 1) {
                image.imageView = createImageView(
                    image.image,
                    image.format,
                    aspectMask,
                    image.mipLevels,
                    VK_IMAGE_VIEW_TYPE_2D_ARRAY,
                    image.arrayLayers
                );
            } else {
                image.imageView = createImageView(
                    image.image,
                    image.format,
                    aspectMask,
                    image.mipLevels,
                    VK_IMAGE_VIEW_TYPE_2D,
                    image.arrayLayers
                );
            }

            VkImageLayout targetLayout = isDepthAttachment ?
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
                isColorAttachment
                    ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
                    : VK_IMAGE_LAYOUT_GENERAL;

            VkRenderingAttachmentInfo attachmentInfo = {
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView = image.imageView,
                .imageLayout = targetLayout,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue = image.clearValue
            };

            if (isDepthAttachment) {
                renderPass.hasDepthAttachment = true;
                renderPass.depthAttachmentFormat = image.format;
                renderPass.depthAttachment = attachmentInfo;
            } else if (isColorAttachment) {
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

    VkCommandPoolCreateInfo computePoolInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueFamilyIndices.computeFamily.value_or(queueFamilyIndices.graphicsFamily.value())
    };
    if (vkCreateCommandPool(device, &computePoolInfo, nullptr, &computeCommandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create compute command pool!");
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

void engine::Renderer::createNearestSampler() {
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 0.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK,
        .unnormalizedCoordinates = VK_FALSE
    };
    if (vkCreateSampler(device, &samplerInfo, nullptr, &nearestSampler) != VK_SUCCESS) {
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
        .name = "fallback_shadow_cube",
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
        .name = "fallback_white_2d",
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
        size_t bufferInfoReserve = static_cast<size_t>(MAX_FRAMES_IN_FLIGHT * std::max(vertexBindings, 1));
        size_t bufferBindingCount = 0;
        for (const auto& b : shader->config.inputBindings) {
            if (b.bufferProvider) bufferBindingCount++;
        }
        bufferInfoReserve = MAX_FRAMES_IN_FLIGHT * std::max(vertexBindings + (int) bufferBindingCount, 1);
        bufferInfos.reserve(bufferInfoReserve);
        descriptorWrites.reserve(static_cast<size_t>(MAX_FRAMES_IN_FLIGHT * (vertexBindings + fragmentBindings + 2)));
        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            std::vector<bool> fragmentBindingWritten(static_cast<size_t>(fragmentBindings), false);
            VkSampler samplerToUse = shader->config.sampler ? shader->config.sampler : mainTextureSampler;
            for (const auto& binding : shader->config.inputBindings) {
                if (binding.bufferProvider) {
                    VkDescriptorBufferInfo info = binding.bufferProvider(this, i);
                    if (info.buffer == VK_NULL_HANDLE) {
                        continue;
                    }
                    bufferInfos.push_back(info);
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = binding.binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = binding.descriptorType,
                        .pBufferInfo = &bufferInfos.back()
                    });
                    if (static_cast<int>(binding.binding) >= vertexBindings) {
                        fragmentBindingWritten[binding.binding - vertexBindings] = true;
                    }
                    continue;
                }
                if (!binding.textureName.empty()) {
                    Texture* tex = textureManager->getTexture(binding.textureName);
                    if (!tex) {
                        std::cout << "Warning: Texture '" << binding.textureName << "' not found for shader '" << shader->name << "'\n";
                        continue;
                    }
                    imageInfos.push_back({
                        .sampler = VK_NULL_HANDLE,
                        .imageView = tex->imageView,
                        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    });
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = binding.binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = binding.descriptorType,
                        .pImageInfo = &imageInfos.back()
                    });
                    fragmentBindingWritten[binding.binding - vertexBindings] = true;
                    continue;
                }
                if (binding.imageArrayProvider) {
                    const int fragIndex = static_cast<int>(binding.binding) - vertexBindings;
                    uint32_t descriptorCount = 1;
                    if (fragIndex >= 0 && fragIndex < fragmentBindings && !shader->config.fragmentDescriptorCounts.empty()) {
                        descriptorCount = std::max(shader->config.fragmentDescriptorCounts[static_cast<size_t>(fragIndex)], 1u);
                    }
                    const size_t startIndex = imageInfos.size();
                    binding.imageArrayProvider(this, i, descriptorCount, imageInfos);
                    if (imageInfos.size() > startIndex) {
                        descriptorWrites.push_back({
                            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .dstSet = shader->descriptorSets[i],
                            .dstBinding = binding.binding,
                            .dstArrayElement = 0,
                            .descriptorCount = static_cast<uint32_t>(imageInfos.size() - startIndex),
                            .descriptorType = binding.descriptorType,
                            .pImageInfo = &imageInfos[startIndex]
                        });
                        if (fragIndex >= 0 && fragIndex < fragmentBindings) {
                            fragmentBindingWritten[static_cast<size_t>(fragIndex)] = true;
                        }
                    }
                    continue;
                }
                VkImageView imageView = getPassImageView(binding.sourceShaderName, binding.attachmentName);
                if (imageView == VK_NULL_HANDLE) {
                    std::cout << "Warning: Attachment '" << binding.attachmentName << "' not found for shader '" << binding.sourceShaderName << "'.\n";
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
                            imageInfos.push_back({ .sampler = samplerToUse });
                            samplerToUse = mainTextureSampler;
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
            samplerToUse = shader->config.sampler ? shader->config.sampler : mainTextureSampler;
            for (int frag = 0; frag < fragmentBindings; ++frag) {
                if (fragmentBindingWritten[static_cast<size_t>(frag)]) continue;
                VkDescriptorType type = getFragmentType(frag);
                const uint32_t descriptorCount = getFragmentCount(frag);
                const size_t startIndex = imageInfos.size();

                if (type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    for (uint32_t c = 0; c < descriptorCount; ++c) {
                        imageInfos.push_back({ .sampler = samplerToUse });
                        samplerToUse = mainTextureSampler;
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
                    {
                        Texture* fallbackTex = textureManager ? textureManager->getTexture("fallback_white_2d") : nullptr;
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

    createComputeDescriptorSets();
}

void engine::Renderer::createComputeDescriptorSets() {
    auto shaders = shaderManager->getComputeShaders();
    for (const auto& shaderCopy : shaders) {
        if (shaderCopy.config.inputBindings.empty()) continue;
        auto shader = shaderManager->getComputeShader(shaderCopy.name);
        if (!shader) continue;

        if (shader->descriptorPool != VK_NULL_HANDLE) {
            VkResult poolReset = vkResetDescriptorPool(device, shader->descriptorPool, 0);
            if (poolReset != VK_SUCCESS) {
                throw std::runtime_error("Failed to reset descriptor pool for compute shader '" + shader->name + "'!");
            }
        }

        shader->descriptorSets.clear();

        const int computeBindings = std::max(shader->config.computeBitBindings, 0);
        auto getComputeType = [&](int index) {
            if (!shader->config.computeDescriptorTypes.empty() && static_cast<size_t>(index) < shader->config.computeDescriptorTypes.size()) {
                return shader->config.computeDescriptorTypes[static_cast<size_t>(index)];
            }
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        };
        auto getComputeCount = [&](int index) {
            if (!shader->config.computeDescriptorCounts.empty() && shader->config.computeDescriptorCounts.size() == static_cast<size_t>(computeBindings)) {
                return std::max(shader->config.computeDescriptorCounts[static_cast<size_t>(index)], 1u);
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
            throw std::runtime_error("Failed to allocate descriptor sets for compute shader '" + shader->name + "'!");
        }

        size_t maxImageInfosPerFrame = 0;
        for (int binding = 0; binding < computeBindings; ++binding) {
            VkDescriptorType type = getComputeType(binding);
            if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
                type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE || type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                maxImageInfosPerFrame += getComputeCount(binding);
            }
        }

        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(MAX_FRAMES_IN_FLIGHT * maxImageInfosPerFrame + 4u);

        std::vector<VkDescriptorBufferInfo> bufferInfos;
        bufferInfos.reserve(MAX_FRAMES_IN_FLIGHT * static_cast<size_t>(std::max(computeBindings, 1)));

        std::vector<VkWriteDescriptorSet> descriptorWrites;
        descriptorWrites.reserve(MAX_FRAMES_IN_FLIGHT * static_cast<size_t>(computeBindings + 2));

        for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            std::vector<bool> bindingWritten(static_cast<size_t>(computeBindings), false);
            VkSampler samplerToUse = mainTextureSampler;

            for (const auto& binding : shader->config.inputBindings) {
                if (binding.bufferProvider) {
                    VkDescriptorBufferInfo info = binding.bufferProvider(this, i);
                    if (info.buffer == VK_NULL_HANDLE) {
                        continue;
                    }
                    bufferInfos.push_back(info);
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = binding.binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = binding.descriptorType,
                        .pBufferInfo = &bufferInfos.back()
                    });
                    if (binding.binding < static_cast<uint32_t>(computeBindings)) {
                        bindingWritten[binding.binding] = true;
                    }
                    continue;
                }

                if (!binding.textureName.empty()) {
                    Texture* tex = textureManager ? textureManager->getTexture(binding.textureName) : nullptr;
                    if (!tex) {
                        std::cout << "Warning: Texture '" << binding.textureName << "' not found for compute shader '" << shader->name << "'\n";
                        continue;
                    }
                    const size_t startIndex = imageInfos.size();
                    if (binding.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
                        imageInfos.push_back({ .sampler = tex->imageSampler != VK_NULL_HANDLE ? tex->imageSampler : mainTextureSampler });
                    } else {
                        imageInfos.push_back({
                            .sampler = (binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? mainTextureSampler : VK_NULL_HANDLE,
                            .imageView = tex->imageView,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = binding.binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = binding.descriptorType,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    if (binding.binding < static_cast<uint32_t>(computeBindings)) {
                        bindingWritten[binding.binding] = true;
                    }
                    continue;
                }

                if (binding.imageArrayProvider) {
                    uint32_t descriptorCount = 1;
                    if (binding.binding < static_cast<uint32_t>(computeBindings)) {
                        descriptorCount = getComputeCount(static_cast<int>(binding.binding));
                    }
                    const size_t startIndex = imageInfos.size();
                    binding.imageArrayProvider(this, i, descriptorCount, imageInfos);
                    if (imageInfos.size() > startIndex) {
                        descriptorWrites.push_back({
                            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                            .dstSet = shader->descriptorSets[i],
                            .dstBinding = binding.binding,
                            .dstArrayElement = 0,
                            .descriptorCount = static_cast<uint32_t>(imageInfos.size() - startIndex),
                            .descriptorType = binding.descriptorType,
                            .pImageInfo = &imageInfos[startIndex]
                        });
                        if (binding.binding < static_cast<uint32_t>(computeBindings)) {
                            bindingWritten[binding.binding] = true;
                        }
                    }
                    continue;
                }

                if (!binding.sourceShaderName.empty() && !binding.attachmentName.empty()) {
                    VkImageView imageView = getPassImageView(binding.sourceShaderName, binding.attachmentName);
                    if (imageView == VK_NULL_HANDLE) {
                        std::cout << "Warning: Attachment '" << binding.attachmentName << "' not found for shader '" << binding.sourceShaderName << "'.\n";
                        continue;
                    }

                    const size_t startIndex = imageInfos.size();
                    VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                    if (binding.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                        layout = VK_IMAGE_LAYOUT_GENERAL;
                    }
                    imageInfos.push_back({
                        .sampler = (binding.descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? mainTextureSampler : VK_NULL_HANDLE,
                        .imageView = imageView,
                        .imageLayout = layout
                    });
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = binding.binding,
                        .dstArrayElement = 0,
                        .descriptorCount = 1,
                        .descriptorType = binding.descriptorType,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    if (binding.binding < static_cast<uint32_t>(computeBindings)) {
                        bindingWritten[binding.binding] = true;
                    }
                }
            }

            for (int binding = 0; binding < computeBindings; ++binding) {
                if (bindingWritten[static_cast<size_t>(binding)]) continue;
                VkDescriptorType type = getComputeType(binding);
                uint32_t descriptorCount = getComputeCount(binding);
                const size_t startIndex = imageInfos.size();

                if (type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    for (uint32_t c = 0; c < descriptorCount; ++c) {
                        imageInfos.push_back({ .sampler = samplerToUse });
                        samplerToUse = mainTextureSampler;
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = static_cast<uint32_t>(binding),
                        .dstArrayElement = 0,
                        .descriptorCount = descriptorCount,
                        .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER,
                        .pImageInfo = &imageInfos[startIndex]
                    });
                    continue;
                }

                if (type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE || type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    Texture* fallbackTex = textureManager ? textureManager->getTexture("fallback_white_2d") : nullptr;
                    if (!fallbackTex || fallbackTex->imageView == VK_NULL_HANDLE || fallbackTex->image == VK_NULL_HANDLE) {
                        std::cout << "Warning: No fallback texture available for compute shader '" << shader->name << "' binding " << binding << ".\n";
                        continue;
                    }
                    for (uint32_t c = 0; c < descriptorCount; ++c) {
                        imageInfos.push_back({
                            .sampler = (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) ? mainTextureSampler : VK_NULL_HANDLE,
                            .imageView = fallbackTex->imageView,
                            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                        });
                    }
                    descriptorWrites.push_back({
                        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                        .dstSet = shader->descriptorSets[i],
                        .dstBinding = static_cast<uint32_t>(binding),
                        .dstArrayElement = 0,
                        .descriptorCount = descriptorCount,
                        .descriptorType = type,
                        .pImageInfo = &imageInfos[startIndex]
                    });
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
    graphicsSegmentCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
    computeSegmentCommandBuffers.resize(MAX_FRAMES_IN_FLIGHT);
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
    crossQueueSegmentSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
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
    std::optional<uint32_t> fallbackComputeFamily;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            indices.graphicsFamily = i;
        }
        if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            if (!(queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && !indices.computeFamily.has_value()) {
                indices.computeFamily = i;
            }
            if (!fallbackComputeFamily.has_value()) {
                fallbackComputeFamily = i;
            }
        }
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
        if (presentSupport) {
            indices.presentFamily = i;
        }
        if (indices.graphicsFamily.has_value() && indices.presentFamily.has_value() && indices.computeFamily.has_value()) {
            break;
        }
    }
    if (!indices.computeFamily.has_value()) {
        if (fallbackComputeFamily.has_value()) {
            indices.computeFamily = fallbackComputeFamily;
        } else {
            indices.computeFamily = indices.graphicsFamily;
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
    if (settingsManager->getSettings()->fpsLimit <= 1e-6f) {
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return availablePresentMode;
        }
    }
    for (const auto& availablePresentMode : availablePresentModes) {
        if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
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
    InputManager* inputManager = renderer->getInputManager();
    inputManager->processInput(window);
    bool pressing = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS
                 || (inputManager->isControllerMode() && inputManager->isFakeCursorPressing());
    if (renderer->ignoreMouseUntilRelease) {
        if (!pressing) renderer->ignoreMouseUntilRelease = false;
        pressing = false;
    }
    if (!pressing) {
        renderer->clicking = false;
    } else if (renderer->getHoveredObject() && pressing) {
        if (!renderer->clicking) {
            if (renderer->getHoveredObject()->getType() == UIType::Button) {
                ButtonObject* button = static_cast<ButtonObject*>(renderer->getHoveredObject());
                button->click();
                renderer->clicking = true;
                if (renderer->uiManager) {
                    renderer->uiManager->processPendingRemovals();
                }
                return;
            } else if (renderer->getHoveredObject()->getType() == UIType::Checkbox) {
                CheckboxObject* toggle = static_cast<CheckboxObject*>(renderer->getHoveredObject());
                toggle->toggle();
                renderer->clicking = true;
            }
        }
        if (renderer->getHoveredObject()->getType() == UIType::Slider) {
            SliderObject* slider = static_cast<SliderObject*>(renderer->getHoveredObject());
            slider->setValue(slider->getSliderValueFromMouse(window));
            renderer->clicking = true;
        } else if (renderer->getHoveredObject()->getParent()->getType() == UIType::Slider) {
            SliderObject* slider = static_cast<SliderObject*>(renderer->getHoveredObject()->getParent());
            slider->setValue(slider->getSliderValueFromMouse(window));
            renderer->clicking = true;
        }
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS
    && !renderer->inputManager->getCursorLocked() && !renderer->inputManager->getUIFocused()) {
        renderer->toggleLockCursor(true);
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
