#include <engine/Renderer.h>

#include <engine/InputManager.h>
#include <engine/EntityManager.h>

engine::Renderer::Renderer(std::string windowTitle) : windowTitle(windowTitle) {}

engine::Renderer::~Renderer() {
    cleanup();
}

void engine::Renderer::run() {
    initWindow();
    initVulkan();
    mainLoop();
    cleanup();
}

void engine::Renderer::cleanup() {
    if (device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device);
    if (device) {
        vkDestroyDevice(device, nullptr);
        device = VK_NULL_HANDLE;
    }
    if (instance) {
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
    cursorLocked = false;
}

void engine::Renderer::initVulkan() {
    // Vulkan initialization code here
}

void engine::Renderer::mainLoop() {
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        processInput(window);
        // drawFrame();
    }
    vkDeviceWaitIdle(device);
}

void engine::Renderer::processInput(GLFWwindow* window) {
    auto renderer = reinterpret_cast<engine::Renderer*>(glfwGetWindowUserPointer(window));
    if (renderer && renderer->inputManager) {
        renderer->inputManager->processInput(window);
    }
}

void engine::Renderer::framebufferResizeCallback(GLFWwindow* window, int width, int height) {
    auto renderer = reinterpret_cast<engine::Renderer*>(glfwGetWindowUserPointer(window));
    // Handle framebuffer resize
}

void engine::Renderer::mouseMoveCallback(GLFWwindow* window, double xpos, double ypos) {
    auto renderer = reinterpret_cast<engine::Renderer*>(glfwGetWindowUserPointer(window));
    // Handle mouse movement
}