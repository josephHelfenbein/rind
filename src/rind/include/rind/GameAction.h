#pragma once

namespace rind {
    // action-native input layer
    enum class GameAction {
        MoveForward, MoveBackward, MoveLeft, MoveRight,
        Look, // camera (analog)
        Jump, Dash, Shoot, Grenade, Punch, Heal, Pause,
        MenuUp, MenuDown, MenuLeft, MenuRight, MenuSelect, MenuCancel,
        None
    };
}
