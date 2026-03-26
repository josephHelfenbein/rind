#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <engine/Renderer.h>
#include <engine/UIManager.h>

#include <vector>
#include <functional>
#include <unordered_map>

namespace engine {
    struct InputEvent {
        enum class Type {
            KeyPress,
            KeyRelease,
            MouseMove,
            MouseButtonPress,
            MouseButtonRelease,
            MouseScroll,
            GamepadButtonPress,
            GamepadButtonRelease,
            GamepadAxisMove,
            GamepadAxisPress,
            GamepadAxisRelease,
        } type;
        union {
            struct { int key; int scancode; int mods; } keyEvent;
            struct { double xPos; double yPos; } mouseMoveEvent;
            struct { int button; int mods; } mouseButtonEvent;
            struct { double xOffset; double yOffset; } mouseScrollEvent;
            struct { int button; int mods; } gamepadButtonEvent;
            struct { int axis; float value; } gamepadAxisEvent;
        };
    };

    class InputManager {
    public:
        InputManager(Renderer* renderer);
        ~InputManager() = default;

        void processInput(GLFWwindow* window);
        void dispatch(const std::vector<InputEvent>& events);
        void dispatchRecreateSwapChain();
        void registerCallback(const std::string& name, std::function<void(const std::vector<InputEvent>&)> callback);
        void unregisterCallback(const std::string& name);
        void registerRecreateSwapChainCallback(const std::string& name, std::function<void()> callback);
        void resetKeyStates();

        void setCursorLocked(bool locked) { isCursorLocked = locked; }
        void setUIFocused(bool focused) {
            isUIFocused = focused;
            if (focused) {
                renderer->getUIManager()->setCursorEnabled(controllerMode);
                if (controllerMode) {
                    int ww, wh;
                    glfwGetWindowSize(renderer->getWindow(), &ww, &wh);
                    fakeControllerCursor = glm::dvec2(ww / 2.0, wh / 2.0);
                    renderer->getUIManager()->setFakeCursorPosition(fakeControllerCursor);
                }
            } else {
                renderer->getUIManager()->setCursorEnabled(false);
                fakeCursorPressing = false;
            }
        }
        bool getUIFocused() const { return isUIFocused; }
        bool getCursorLocked() const { return isCursorLocked; }
        void setControllerMode(bool controllerMode) {
            this->controllerMode = controllerMode;
            fakeCursorPressing = false;
            if (isUIFocused) {
                renderer->getUIManager()->setCursorEnabled(controllerMode);
                if (controllerMode) {
                    int ww, wh;
                    glfwGetWindowSize(renderer->getWindow(), &ww, &wh);
                    fakeControllerCursor = glm::dvec2(ww / 2.0, wh / 2.0);
                    renderer->getUIManager()->setFakeCursorPosition(fakeControllerCursor);
                }
            }
        }
        bool isControllerMode() const { return controllerMode; }
        bool isFakeCursorPressing() const { return fakeCursorPressing; }
        const glm::dvec2& getFakeControllerCursor() const { return fakeControllerCursor; }

        void resetMouseDelta() {
            hasMousePosition = false;
        }

    private:
        Renderer* renderer;
        std::unordered_map<std::string, std::function<void(const std::vector<InputEvent>&)>> callbacks;
        std::unordered_map<std::string, std::function<void()>> recreateSwapChainCallbacks;
        std::vector<std::string> unregisterQueue;
        int keyStates[GLFW_KEY_LAST + 1] = {0};
        int mouseButtonStates[GLFW_MOUSE_BUTTON_LAST + 1] = {0};
        int gamepadButtonStates[GLFW_GAMEPAD_BUTTON_LAST + 1] = {0};
        float gamepadAxisStates[GLFW_GAMEPAD_AXIS_LAST + 1] = {0.0f};
        int gamepadAxisZones[GLFW_GAMEPAD_AXIS_LAST + 1] = {0}; // -1, 0, or 1
        static constexpr float axisDeadzone = 0.2f;
        glm::dvec2 fakeControllerCursor{0.0, 0.0};
        bool fakeCursorPressing = false;
        bool hasMousePosition = false;
        glm::dvec2 lastMouse = {0.0, 0.0};
        bool isCursorLocked = false;
        bool isUIFocused = false;
        bool controllerMode = false;
    };
};