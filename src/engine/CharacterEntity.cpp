#include <engine/CharacterEntity.h>

void engine::CharacterEntity::update(float deltaTime) {

}

void engine::CharacterEntity::move(const glm::vec3& delta) {
    pressed += delta;
}

void engine::CharacterEntity::stopMove(const glm::vec3& delta) {
    pressed -= delta;
    if (glm::length(pressed) < 1e-6f) {
        resetVelocity();
    }
}

void engine::CharacterEntity::resetVelocity() {
    velocity = glm::vec3(0.0f);
}