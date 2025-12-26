#include <rind/Enemy.h>

rind::Enemy::Enemy(engine::EntityManager* entityManager, rind::Player* player, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures)
    : engine::CharacterEntity(entityManager, name, shader, transform, textures), targetPlayer(player) {
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 0.0f)),
            name,
            glm::vec3(0.5f, 1.8f, 0.5f)
        );
        addChild(box);
        setCollider(box);
    }

void rind::Enemy::update(float deltaTime) {
    std::function<void()> wander = [&]() {
        // pathfinding logic to wander around
    };
    if (targetPlayer) {
        glm::vec3 toPlayer = targetPlayer->getWorldPosition() - getWorldPosition();
        float distanceToPlayer = glm::length(toPlayer);

        switch (state) {
            case EnemyState::Idle: {
                wander();
                break;
            }
            case EnemyState::Chasing: {
                break;
            }
            case EnemyState::Attacking: {
                break;
            }
        }
    } else {
        wander();
    }
    engine::CharacterEntity::update(deltaTime);
}

void rind::Enemy::damage(float amount) {
    setHealth(getHealth() - amount);
    if (getHealth() <= 0.0f) {
        std::cout<< "Enemy " << getName() << " has died." << std::endl;
        delete this;
    }
}