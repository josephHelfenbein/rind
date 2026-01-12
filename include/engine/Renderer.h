#pragma once

#include <vulkan/vulkan.h>

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <string>
#include <vector>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>

namespace engine {
    struct GraphicsShader;
    struct ComputeShader;
    struct PassInfo;
    struct RenderNode;
    struct Texture;
    class UIObject;

    class Renderer {
    public:
        Renderer(std::string windowTitle);
        ~Renderer();
        void run();

        void registerEntityManager(class EntityManager* entityManager) { this->entityManager = entityManager; }
        void registerInputManager(class InputManager* inputManager) { this->inputManager = inputManager; }
        void registerUIManager(class UIManager* uiManager) { this->uiManager = uiManager; }
        void registerTextureManager(class TextureManager* textureManager) { this->textureManager = textureManager; }
        void registerShaderManager(class ShaderManager* shaderManager) { this->shaderManager = shaderManager; }
        void registerSceneManager(class SceneManager* sceneManager) { this->sceneManager = sceneManager; }
        void registerModelManager(class ModelManager* modelManager) { this->modelManager = modelManager; }
        void registerParticleManager(class ParticleManager* particleManager) { this->particleManager = particleManager; }
        void registerAudioManager(class AudioManager* audioManager) { this->audioManager = audioManager; }
        void registerSettingsManager(class SettingsManager* settingsManager) { this->settingsManager = settingsManager; }
        class EntityManager* getEntityManager() { return entityManager; }
        class InputManager* getInputManager() { return inputManager; }
        class UIManager* getUIManager() { return uiManager; }
        class TextureManager* getTextureManager() { return textureManager; }
        class ShaderManager* getShaderManager() { return shaderManager; }
        class SceneManager* getSceneManager() { return sceneManager; }
        class ModelManager* getModelManager() { return modelManager; }
        class ParticleManager* getParticleManager() { return particleManager; }
        class AudioManager* getAudioManager() { return audioManager; }
        class SettingsManager* getSettingsManager() { return settingsManager; }

        void toggleLockCursor(bool lock);
        bool isPaused() const { return paused; }
        void setPaused(bool paused) { this->paused = paused; }

        std::pair<VkBuffer, VkDeviceMemory> createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties);
        std::pair<VkImage, VkDeviceMemory> createImage(
            uint32_t width,
            uint32_t height,
            uint32_t mipLevels,
            VkSampleCountFlagBits samples,
            VkFormat format,
            VkImageTiling tiling,
            VkImageUsageFlags usage,
            VkMemoryPropertyFlags properties,
            uint32_t arrayLayers,
            VkImageCreateFlags flags = 0
        );
        VkImageView createImageView(
            VkImage image,
            VkFormat format,
            VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT,
            uint32_t mipLevels = 1,
            VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D,
            uint32_t layerCount = 1
        );
        void transitionImageLayout(
            VkImage image,
            VkFormat format,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            uint32_t mipLevels,
            uint32_t layerCount = 1
        );
        void transitionImageLayoutInline(
            VkCommandBuffer commandBuffer,
            VkImage image,
            VkFormat format,
            VkImageLayout oldLayout,
            VkImageLayout newLayout,
            uint32_t mipLevels,
            uint32_t layerCount = 1
        );
        void copyBufferToImage(
            VkBuffer buffer,
            VkImage image,
            uint32_t width,
            uint32_t height,
            uint32_t layerCount = 1
        );
        void copyDataToBuffer(
            void* data,
            VkDeviceSize size,
            VkBuffer buffer,
            VkDeviceMemory bufferMemory
        );
        VkSampler createTextureSampler(
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
        );
        std::pair<VkImage, VkDeviceMemory> createImageFromPixels(
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
        );

        void ensureFallbackShadowCubeTexture();
        void ensureFallback2DTexture();
        void refreshDescriptorSets();
        VkImageView getPassImageView(const std::string& shaderName, const std::string& attachmentName);

        VkDevice getDevice() const { return device; }
        uint32_t getFramesInFlight() const { return MAX_FRAMES_IN_FLIGHT; }
        VkExtent2D getSwapChainExtent() const { return swapChainExtent; }
        float getUIScale() const { return uiScale; }
        GLFWwindow* getWindow() const { return window; }
        std::pair<VkBuffer, VkBuffer> getUIBuffers() const { return { uiVertexBuffer, uiIndexBuffer }; }
        UIObject* getHoveredObject() const { return hoveredObject; }
        int getMaxFramesInFlight() const { return MAX_FRAMES_IN_FLIGHT; }
        VkSampler getMainTextureSampler() const { return mainTextureSampler; }
        void setHoveredObject(UIObject* obj) { hoveredObject = obj; }
        PFN_vkCmdBeginRendering getFpCmdBeginRendering() const { return fpCmdBeginRendering; }
        PFN_vkCmdEndRendering getFpCmdEndRendering() const { return fpCmdEndRendering; }
        float getDeltaTime() const { return deltaTime; }

    private:
        const int MAX_FRAMES_IN_FLIGHT = 2;
        uint32_t currentFrame = 0;
        const std::vector<const char*> validationLayers = {
            "VK_LAYER_KHRONOS_validation"
        };
        std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        #ifdef __APPLE__
            , "VK_KHR_portability_subset"
        #endif
        };
        #ifndef VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME
        #define VK_EXT_SHADER_ATOMIC_FLOAT_EXTENSION_NAME "VK_EXT_shader_atomic_float"
        #endif
        // Runtime toggle: use CAS fallback when float atomicAdd isn’t available via VK_EXT_shader_atomic_float
        bool useCASAdvection = true;
        #ifdef NDEBUG
            const bool enableValidationLayers = false;
            const bool DEBUG_RENDER_LOGS = false;
        #else
            const bool enableValidationLayers = true;
            const bool DEBUG_RENDER_LOGS = true;
        #endif

        void initWindow();
        void initVulkan();
        void mainLoop();
        void cleanup();

        const int WIDTH = 800;
        const int HEIGHT = 600;
        std::string windowTitle;
        GLFWwindow* window;
        VkInstance instance;
        VkDevice device;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice;
        VkSurfaceKHR surface;
        float aoRadius = 0.5f;
        float aoBias = 0.025f;
        float aoIntensity = 2.0f;
        bool framebufferResized = false;

        PFN_vkCmdBeginRendering fpCmdBeginRendering = nullptr;
        PFN_vkCmdEndRendering fpCmdEndRendering = nullptr;

        float deltaTime = 0.0f;
        float lastFrameTime = 0.0f;
        float uiScale = 1.0f;

        bool paused = false;

        VkQueue graphicsQueue;
        VkQueue presentQueue;
        VkSwapchainKHR swapChain;
        std::vector<VkImage> swapChainImages;
        std::vector<VkImageLayout> swapChainImageLayouts;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;
        std::vector<std::shared_ptr<PassInfo>> managedRenderPasses;
        VkCommandPool commandPool;
        VkSampler mainTextureSampler;

        VkBuffer uiVertexBuffer;
        VkDeviceMemory uiVertexBufferMemory;
        VkBuffer uiIndexBuffer;
        VkDeviceMemory uiIndexBufferMemory;

        std::vector<VkFence> inFlightFences;
        std::vector<VkSemaphore> imageAvailableSemaphores;
        std::vector<VkSemaphore> renderFinishedSemaphores;
        std::vector<VkCommandBuffer> commandBuffers;

        class EntityManager* entityManager;
        class InputManager* inputManager;
        class UIManager* uiManager;
        class TextureManager* textureManager;
        class ShaderManager* shaderManager;
        class SceneManager* sceneManager;
        class ModelManager* modelManager;
        class ParticleManager* particleManager;
        class AudioManager* audioManager;
        class SettingsManager* settingsManager;

        UIObject* hoveredObject = nullptr;
        bool clicking = true;

        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createSwapChain(VkSwapchainKHR oldSwapchain);
        void createImageViews();
        void createAttachmentResources();
        void createCommandPool();
        void createMainTextureSampler();
        void createPostProcessDescriptorSets();
        void createCommandBuffers();
        void createSyncObjects();
        void createQuadResources();

        void drawFrame();

        VkCommandBuffer beginSingleTimeCommands();
        void endSingleTimeCommands(VkCommandBuffer commandBuffer);
        void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex);

        void draw2DPass(VkCommandBuffer commandBuffer, RenderNode& node);

        void recreateSwapChain();

        std::vector<const char*> getRequiredExtensions();
        bool checkValidationLayerSupport();
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
        void destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);


        static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
            std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;
            return VK_FALSE;
        }
        struct QueueFamilyIndices {
            std::optional<uint32_t> graphicsFamily;
            std::optional<uint32_t> presentFamily;
            bool isComplete() {
                return graphicsFamily.has_value() && presentFamily.has_value();
            }
        };
        struct SwapChainSupportDetails {
            VkSurfaceCapabilitiesKHR capabilities;
            std::vector<VkSurfaceFormatKHR> formats;
            std::vector<VkPresentModeKHR> presentModes;
        };

        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        bool hasStencilComponent(VkFormat format);
        bool hasDeviceExtension(VkPhysicalDevice dev, const char* extensionName);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);
        VkSurfaceFormatKHR chooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
        VkPresentModeKHR chooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
        VkExtent2D chooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);
        int rateDeviceSuitability(VkPhysicalDevice device);

        static void processInput(GLFWwindow* window);
        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void mouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    };
};
