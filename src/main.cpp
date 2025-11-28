#pragma once

#include <memory>
#include <game/GameInstance.h>

std::unique_ptr<rind::GameInstance> game;

int main() {
    game = std::make_unique<rind::GameInstance>();
    return 0;
}