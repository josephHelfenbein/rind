#pragma once

#include <memory>
#include <game/GameInstance.h>

std::unique_ptr<rift::GameInstance> game;

int main() {
    game = std::make_unique<rift::GameInstance>();
    return 0;
}