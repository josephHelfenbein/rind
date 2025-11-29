#pragma once

#include <glfw/include/GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <engine/Renderer.h>

#include <vector>
#include <functional>

namespace engine {
    struct InputEvent {
        enum class Type {
            KeyPress,
            KeyRelease,
            MouseMove,
            MouseButtonPress,
            MouseButtonRelease,
            MouseScroll
        } type;
        union {
            struct { int key; int scancode; int mods; } keyEvent;
            struct { double xPos; double yPos; } mouseMoveEvent;
            struct { int button; int mods; } mouseButtonEvent;
            struct { double xOffset; double yOffset; } mouseScrollEvent;
        };
    };

    class InputManager {
    public:
        InputManager(Renderer* renderer);
        ~InputManager() = default;

        void processInput(GLFWwindow* window);
        void dispatch(const std::vector<InputEvent>& events);
        void registerCallback(std::function<void(const std::vector<InputEvent>&)> callback);

    private:
        std::vector<std::function<void(const std::vector<InputEvent>&)>> callbacks;
        int keyStates[GLFW_KEY_LAST + 1] = {0};
        int mouseButtonStates[GLFW_MOUSE_BUTTON_LAST + 1] = {0};
        bool hasMousePosition = false;
        glm::dvec2 lastMouse = {0.0, 0.0};
    };
};