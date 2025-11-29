#pragma once

#include <vulkan/vulkan.h>

#include <glfw/include/GLFW/glfw3.h>

#include <string>

namespace engine {
    class Renderer {
    public:
        Renderer(std::string windowTitle);
        ~Renderer();
        void run();

        void registerEntityManager(class EntityManager* entityManager) {
            this->entityManager = entityManager;
        }
        void registerInputManager(class InputManager* inputManager) {
            this->inputManager = inputManager;
        }
        class EntityManager* getEntityManager() { return entityManager; }
        class InputManager* getInputManager() { return inputManager; }

    private:
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

        class EntityManager* entityManager;
        class InputManager* inputManager;

        bool cursorLocked;

        static void processInput(GLFWwindow* window);

        static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

        static void mouseMoveCallback(GLFWwindow* window, double xpos, double ypos);
    };
};