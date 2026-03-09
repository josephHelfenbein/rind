#include <engine/InputManager.h>
#include <engine/SettingsManager.h>

#include <limits>

engine::InputManager::InputManager(Renderer* renderer) : renderer(renderer) {
    renderer->registerInputManager(this);
}

void engine::InputManager::processInput(GLFWwindow* window) {
    if (!window) return;
    static thread_local std::vector<InputEvent> events;
    events.clear();
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        int state = glfwGetKey(window, key);
        if (state == GLFW_PRESS && keyStates[key] != GLFW_PRESS) {
            if (controllerMode) {
                setControllerMode(false);
            }
            InputEvent event = {
                .type = InputEvent::Type::KeyPress,
                .keyEvent = { key, 0, 0 }
            };
            events.push_back(event);
        } else if (state == GLFW_RELEASE && keyStates[key] != GLFW_RELEASE) {
            InputEvent event = {
                .type = InputEvent::Type::KeyRelease,
                .keyEvent = { key, 0, 0 }
            };
            events.push_back(event);
        }
        keyStates[key] = state;
    }
    double xpos = std::numeric_limits<double>::quiet_NaN();
    double ypos = std::numeric_limits<double>::quiet_NaN();
    glfwGetCursorPos(window, &xpos, &ypos);
    if (!std::isnan(xpos) && !std::isnan(ypos)) {
        if (!hasMousePosition) {
            lastMouse = { xpos, ypos };
            hasMousePosition = true;
        }
        double deltaX = xpos - lastMouse.x;
        double deltaY = ypos - lastMouse.y;
        if (deltaX != 0.0 || deltaY != 0.0) {
            if (controllerMode) {
                setControllerMode(false);
            }
            InputEvent event = {
                .type = InputEvent::Type::MouseMove,
                .mouseMoveEvent = { deltaX, deltaY }
            };
            events.push_back(event);
            lastMouse = { xpos, ypos };
        }
    }
    for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        int state = glfwGetMouseButton(window, button);
        if (state == GLFW_PRESS && mouseButtonStates[button] != GLFW_PRESS) {
            if (controllerMode) {
                setControllerMode(false);
            }
            InputEvent event = {
                .type = InputEvent::Type::MouseButtonPress,
                .mouseButtonEvent = { button, 0 }
            };
            events.push_back(event);
        } else if (state == GLFW_RELEASE && mouseButtonStates[button] != GLFW_RELEASE) {
            InputEvent event = {
                .type = InputEvent::Type::MouseButtonRelease,
                .mouseButtonEvent = { button, 0 }
            };
            events.push_back(event);
        }
        mouseButtonStates[button] = state;
    }
    if (glfwJoystickIsGamepad(GLFW_JOYSTICK_1)) {
        GLFWgamepadstate state;
        glfwGetGamepadState(GLFW_JOYSTICK_1, &state);
        for (int button = GLFW_GAMEPAD_BUTTON_A; button <= GLFW_GAMEPAD_BUTTON_LAST; ++button) {
            if (state.buttons[button] && gamepadButtonStates[button] != 1) {
                if (!controllerMode) {
                    setControllerMode(true);
                }
                InputEvent event = {
                    .type = InputEvent::Type::GamepadButtonPress,
                    .gamepadButtonEvent = { button, 0 }
                };
                events.push_back(event);
            } else if (!state.buttons[button] && gamepadButtonStates[button] != 0) {
                InputEvent event = {
                    .type = InputEvent::Type::GamepadButtonRelease,
                    .gamepadButtonEvent = { button, 0 }
                };
                events.push_back(event);
            }
            gamepadButtonStates[button] = state.buttons[button];
        }
        for (int axis = GLFW_GAMEPAD_AXIS_LEFT_X; axis <= GLFW_GAMEPAD_AXIS_LAST; ++axis) {
            float value = state.axes[axis];
            if (std::abs(value - gamepadAxisStates[axis]) > 0.01f) {
                if (!controllerMode) {
                    setControllerMode(true);
                }
                InputEvent event = {
                    .type = InputEvent::Type::GamepadAxisMove,
                    .gamepadAxisEvent = { axis, value }
                };
                events.push_back(event);
            }
            int newZone = (value > axisDeadzone) ? 1 : (value < -axisDeadzone) ? -1 : 0;
            if (newZone != gamepadAxisZones[axis]) {
                if (gamepadAxisZones[axis] != 0) {
                    InputEvent releaseEvent = {
                        .type = InputEvent::Type::GamepadAxisRelease,
                        .gamepadAxisEvent = { axis, static_cast<float>(gamepadAxisZones[axis]) }
                    };
                    events.push_back(releaseEvent);
                }
                if (newZone != 0) {
                    InputEvent pressEvent = {
                        .type = InputEvent::Type::GamepadAxisPress,
                        .gamepadAxisEvent = { axis, static_cast<float>(newZone) }
                    };
                    events.push_back(pressEvent);
                }
                gamepadAxisZones[axis] = newZone;
            }
            gamepadAxisStates[axis] = value;
            if (isUIFocused && controllerMode) {
                if (axis == GLFW_GAMEPAD_AXIS_RIGHT_X || axis == GLFW_GAMEPAD_AXIS_RIGHT_Y) {
                    if (std::abs(value) > 0.15f) {
                        int ww, wh;
                        float sensitivity = renderer->getSettingsManager()->getSettings()->sensitivity;
                        glfwGetWindowSize(renderer->getWindow(), &ww, &wh);
                        fakeControllerCursor += glm::dvec2(
                            (axis == GLFW_GAMEPAD_AXIS_RIGHT_X ? value : 0.0) * sensitivity * 1000.0f,
                            (axis == GLFW_GAMEPAD_AXIS_RIGHT_Y ? value : 0.0) * sensitivity * 1000.0f
                        );
                        fakeControllerCursor = glm::clamp(fakeControllerCursor, glm::dvec2(0.0), glm::dvec2(ww, wh));
                    }
                    renderer->getUIManager()->setFakeCursorPosition(fakeControllerCursor);
                    renderer->setHoveredObject(renderer->getUIManager()->processMouseMovement(window, fakeControllerCursor.x, fakeControllerCursor.y));
                } else if (axis == GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    fakeCursorPressing = value > 0.5f;
                }
            }
        }
    }
    dispatch(events);
}

void engine::InputManager::dispatch(const std::vector<InputEvent>& events) {
    if (unregisterQueue.size()) {
        for (const std::string& name : unregisterQueue) {
            if (callbacks.find(name) != callbacks.end()) {
                callbacks.erase(name);
            }
            if (recreateSwapChainCallbacks.find(name) != recreateSwapChainCallbacks.end()) {
                recreateSwapChainCallbacks.erase(name);
            }
        }
        unregisterQueue.clear();
    }
    for (const auto& [name, callback] : callbacks) {
        callback(events);
    }
}

void engine::InputManager::dispatchRecreateSwapChain() {
    if (unregisterQueue.size()) {
        for (const std::string& name : unregisterQueue) {
            if (callbacks.find(name) != callbacks.end()) {
                callbacks.erase(name);
            }
            if (recreateSwapChainCallbacks.find(name) != recreateSwapChainCallbacks.end()) {
                recreateSwapChainCallbacks.erase(name);
            }
        }
        unregisterQueue.clear();
    }
    for (const auto& [name, callback] : recreateSwapChainCallbacks) {
        callback();
    }
}

void engine::InputManager::registerCallback(const std::string& name, std::function<void(const std::vector<InputEvent>&)> callback) {
    if (callback) {
        callbacks[name] = std::move(callback);
    }
}

void engine::InputManager::registerRecreateSwapChainCallback(const std::string& name, std::function<void()> callback) {
    if (callback) {
        recreateSwapChainCallbacks[name] = std::move(callback);
    }
}

void engine::InputManager::unregisterCallback(const std::string& name) {
    unregisterQueue.push_back(name);
}

void engine::InputManager::resetKeyStates() {
    for (int& state : keyStates) {
        state = GLFW_RELEASE;
    }
    for (int& state : mouseButtonStates) {
        state = GLFW_RELEASE;
    }
    for (int& state : gamepadButtonStates) {
        state = 0;
    }
    for (float& state : gamepadAxisStates) {
        state = 0.0f;
    }
    for (int& zone : gamepadAxisZones) {
        zone = 0;
    }
}