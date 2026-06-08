#pragma once

#include <GLFW/glfw3.h>
#include <rind/GameAction.h>

namespace rind {

    struct GamepadButtonBinding { int button; GameAction action; };
    struct GamepadTriggerBinding { int axis; GameAction action; };

    inline constexpr GamepadButtonBinding kGamepadButtonBindings[] = {
        { GLFW_GAMEPAD_BUTTON_A, GameAction::Jump },
        { GLFW_GAMEPAD_BUTTON_B, GameAction::Heal },
        { GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, GameAction::Grenade },
        { GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, GameAction::Punch },
        { GLFW_GAMEPAD_BUTTON_START, GameAction::Pause },
    };

    inline constexpr GamepadTriggerBinding kGamepadTriggerBindings[] = {
        { GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER, GameAction::Shoot },
        { GLFW_GAMEPAD_AXIS_LEFT_TRIGGER, GameAction::Dash },
    };

    inline GameAction gamepadButtonToAction(int button) {
        for (const auto& b : kGamepadButtonBindings) {
            if (b.button == button) return b.action;
        }
        return GameAction::None;
    }

    inline GameAction gamepadTriggerToAction(int axis) {
        for (const auto& t : kGamepadTriggerBindings) {
            if (t.axis == axis) return t.action;
        }
        return GameAction::None;
    }

    inline int actionToGamepadButton(GameAction action) {
        for (const auto& b : kGamepadButtonBindings) {
            if (b.action == action) return b.button;
        }
        return -1;
    }

    inline int actionToGamepadTrigger(GameAction action) {
        for (const auto& t : kGamepadTriggerBindings) {
            if (t.action == action) return t.axis;
        }
        return -1;
    }

}
