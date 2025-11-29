#pragma once

#include <vulkan/vulkan.h>

#include <glfw/include/GLFW/glfw3.h>

#include <string>
#include <vector>
#include <iostream>
#include <set>

namespace engine {
    class Renderer {
    private:
        struct ImageResource {
            uint32_t width;
            uint32_t height;
            uint32_t mipLevels;
            VkSampleCountFlagBits samples;
            VkFormat format;
            VkImageTiling tiling;
            VkImageUsageFlags usage;
            VkMemoryPropertyFlags properties;
            uint32_t arrayLayers;
            VkImageCreateFlags createFlags;
            VkImage image;
            VkDeviceMemory memory;
            VkImageView imageView;
        };
        struct RenderPass {
            std::vector<ImageResource> imageResources;
            std::vector<VkFramebuffer> framebuffers;
            std::vector<VkAttachmentDescription> attachments;
            VkRenderPassCreateInfo createInfo;
            VkSamplerCreateInfo samplerInfo;
            VkRenderPass renderPass;
            VkSampler sampler;
        };
    public:
        Renderer(std::string windowTitle, std::vector<RenderPass> renderPasses);
        ~Renderer();
        void run();

        void registerEntityManager(class EntityManager* entityManager) {
            this->entityManager = entityManager;
        }
        void registerInputManager(class InputManager* inputManager) {
            this->inputManager = inputManager;
        }
        void registerUIManager(class UIManager* uiManager) {
            this->uiManager = uiManager;
        }
        void registerTextureManager(class TextureManager* textureManager) {
            this->textureManager = textureManager;
        }
        void registerShaderManager(class ShaderManager* shaderManager) {
            this->shaderManager = shaderManager;
        }
        class EntityManager* getEntityManager() { return entityManager; }
        class InputManager* getInputManager() { return inputManager; }
        class UIManager* getUIManager() { return uiManager; }
        class TextureManager* getTextureManager() { return textureManager; }
        class ShaderManager* getShaderManager() { return shaderManager; }

        std::vector<VkDescriptorSet> createDescriptorSets(class GraphicsShader* shader, std::vector<class Texture*>& textures, std::vector<VkBuffer>& buffers);

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
        #else
            const bool enableValidationLayers = true;
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
        int msaaSamples = VK_SAMPLE_COUNT_1_BIT;

        VkQueue graphicsQueue;
        VkQueue presentQueue;
        VkSwapchainKHR swapChain;
        std::vector<VkImage> swapChainImages;
        VkFormat swapChainImageFormat;
        VkExtent2D swapChainExtent;
        std::vector<VkImageView> swapChainImageViews;

        class EntityManager* entityManager;
        class InputManager* inputManager;
        class UIManager* uiManager;
        class TextureManager* textureManager;
        class ShaderManager* shaderManager;

        bool cursorLocked;

        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createSwapChain();
        void createImageViews();
        void createRenderPasses();
        void createCommandPool();
        void createTextureSamplers();
        void createFinalDescriptorSets();
        void createColorResources();
        void createDepthResources();
        void createCommandBuffers();
        void createSyncObjects();

        std::vector<RenderPass> renderPasses;

        std::pair<VkImage, VkDeviceMemory> createImage(uint32_t width, uint32_t height, uint32_t mipLevels, VkSampleCountFlagBits samples, VkFormat format, VkImageTiling tiling, VkImageUsageFlags usage, VkMemoryPropertyFlags properties, uint32_t arrayLayers, VkImageCreateFlags flags = 0);
        VkImageView createImageView(VkImage image, VkFormat format, VkImageAspectFlags aspectFlags = VK_IMAGE_ASPECT_COLOR_BIT, uint32_t mipLevels = 1, VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D, uint32_t layerCount = 1);
        void recreateSwapChain();

        bool enableValidationLayers = false;
        std::vector<const char*> getRequiredExtensions();
        bool checkValidationLayerSupport();
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        VkResult createDebugUtilsMessengerEXT(VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkDebugUtilsMessengerEXT* pDebugMessenger);
        void destroyDebugUtilsMessengerEXT(VkInstance instance, VkDebugUtilsMessengerEXT debugMessenger, const VkAllocationCallbacks* pAllocator);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);


        VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
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
        VkSampleCountFlagBits getMaxUsableSampleCount();

        static void processInput(GLFWwindow* window);
        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void mouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    };
};