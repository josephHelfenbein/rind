#include <engine/InputManager.h>

#include <limits>

engine::InputManager::InputManager(Renderer* renderer) {
    renderer->registerInputManager(this);
}

void engine::InputManager::processInput(GLFWwindow* window) {
    if (!window) return;
    std::vector<InputEvent> events;
    for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
        int state = glfwGetKey(window, key);
        if (state == GLFW_PRESS && keyStates[key] != GLFW_PRESS) {
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
    if (!hasMousePosition) {
        lastMouse = { xpos, ypos };
        hasMousePosition = true;
    }
    double deltaX = xpos - lastMouse.x;
    double deltaY = ypos - lastMouse.y;
    if (deltaX != 0.0 || deltaY != 0.0) {
        InputEvent event = {
            .type = InputEvent::Type::MouseMove,
            .mouseMoveEvent = { deltaX, deltaY }
        };
        events.push_back(event);
        lastMouse = { xpos, ypos };
    }
    for (int button = GLFW_MOUSE_BUTTON_1; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
        int state = glfwGetMouseButton(window, button);
        if (state == GLFW_PRESS && mouseButtonStates[button] != GLFW_PRESS) {
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
    dispatch(events);
}

void engine::InputManager::dispatch(const std::vector<InputEvent>& events) {
    if (unregisterQueue.size()) {
        for (const std::string& name : unregisterQueue) {
            if (callbacks.find(name) != callbacks.end()) {
                callbacks.erase(name);
            }
        }
        unregisterQueue.clear();
    }
    for (const auto& [name, callback] : callbacks) {
        callback(events);
    }
}

void engine::InputManager::registerCallback(const std::string& name, std::function<void(const std::vector<InputEvent>&)> callback) {
    if (callback) {
        callbacks[name] = std::move(callback);
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
}