#include <rind/Enemy.h>

rind::Enemy::Enemy(engine::EntityManager* entityManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures)
    : engine::CharacterEntity(entityManager, name, shader, transform, textures) {
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 0.0f)),
            name,
            glm::vec3(0.5f, 1.8f, 0.5f)
        );
        addChild(box);
        setCollider(box);
}

void rind::Enemy::damage(float amount) {
    setHealth(getHealth() - amount);
    if (getHealth() <= 0.0f) {
        std::cout<< "Enemy " << getName() << " has died." << std::endl;
        delete this;
    }
}