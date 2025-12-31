#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <engine/Renderer.h>

#include <vector>
#include <functional>
#include <map>

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
        void registerCallback(const std::string& name, std::function<void(const std::vector<InputEvent>&)> callback);
        void unregisterCallback(const std::string& name);

        void setCursorLocked(bool locked) { isCursorLocked = locked; }
        void setUIFocused(bool focused) { isUIFocused = focused; }
        bool getUIFocused() const { return isUIFocused; }
        bool getCursorLocked() const { return isCursorLocked; }

        void resetMouseDelta() {
            hasMousePosition = false;
        }

    private:
        std::map<std::string, std::function<void(const std::vector<InputEvent>&)>> callbacks;
        std::vector<std::string> unregisterQueue;
        int keyStates[GLFW_KEY_LAST + 1] = {0};
        int mouseButtonStates[GLFW_MOUSE_BUTTON_LAST + 1] = {0};
        bool hasMousePosition = false;
        glm::dvec2 lastMouse = {0.0, 0.0};
        bool isCursorLocked = false;
        bool isUIFocused = false;
    };
};