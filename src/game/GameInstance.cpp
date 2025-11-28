#include <game/GameInstance.h>

rift::GameInstance::GameInstance() {
    renderer = std::make_unique<engine::Renderer>();
}

rift::GameInstance::~GameInstance() {
}