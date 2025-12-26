#pragma once

#include <engine/CharacterEntity.h>

namespace rind {
    class Enemy : public engine::CharacterEntity {
    public:
        Enemy(engine::EntityManager* entityManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures);
        
        void damage(float amount) override;

    private:
    };
};