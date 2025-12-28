#include <rind/Player.h>
#include <rind/Enemy.h>
#include <engine/ParticleManager.h>

rind::Player::Player(engine::EntityManager* entityManager, engine::InputManager* inputManager, const std::string& name, std::string shader, glm::mat4 transform, std::vector<std::string> textures = {})
    : engine::CharacterEntity(entityManager, name, shader, transform, textures), inputManager(inputManager) {
        camera = new engine::Camera(
            entityManager,
            "camera",
            glm::mat4(1.0f),
            60.0f,
            16.0f/9.0f,
            0.1f,
            1000.0f,
            true
        );
        addChild(camera);
        entityManager->setCamera(camera);
        engine::OBBCollider* box = new engine::OBBCollider(
            entityManager,
            glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.6f, 0.0f)),
            name,
            glm::vec3(0.5f, 1.8f, 0.5f)
        );
        addChild(box);
        setCollider(box);
        inputManager->registerCallback([this](const std::vector<engine::InputEvent>& events) {
            this->registerInput(events);
        });
    }

void rind::Player::registerInput(const std::vector<engine::InputEvent>& events) {
    for (const auto& event : events) {
        if (event.type == engine::InputEvent::Type::KeyPress) {
            switch (event.keyEvent.key) {
                case GLFW_KEY_W:
                    move(glm::vec3(1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_S:
                    move(glm::vec3(-1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_A:
                    move(glm::vec3(0.0f, 0.0f, -1.0f));
                    break;
                case GLFW_KEY_D:
                    move(glm::vec3(0.0f, 0.0f, 1.0f));
                    break;
                case GLFW_KEY_SPACE:
                    jump(5.0f);
                    break;
                default:
                    break;
            }
        } else if (event.type == engine::InputEvent::Type::KeyRelease) {
            switch (event.keyEvent.key) {
                case GLFW_KEY_W:
                    stopMove(glm::vec3(1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_S:
                    stopMove(glm::vec3(-1.0f, 0.0f, 0.0f));
                    break;
                case GLFW_KEY_A:
                    stopMove(glm::vec3(0.0f, 0.0f, -1.0f));
                    break;
                case GLFW_KEY_D:
                    stopMove(glm::vec3(0.0f, 0.0f, 1.0f));
                    break;
                default:
                    break;
            }
        } else if (event.type == engine::InputEvent::Type::MouseMove) {
            if (inputManager->getCursorLocked()) {
                float xOffset = static_cast<float>(event.mouseMoveEvent.xPos) * mouseSensitivity;
                float yOffset = static_cast<float>(event.mouseMoveEvent.yPos) * mouseSensitivity;
                rotate(glm::vec3(0.0f, -xOffset, -yOffset));
            }
        } else if (event.type == engine::InputEvent::Type::MouseButtonPress) {
            if (event.mouseButtonEvent.button == GLFW_MOUSE_BUTTON_LEFT) {
                if ((std::chrono::steady_clock::now() - lastShotTime) >= std::chrono::duration<float>(shootingCooldown)) {
                    shoot();
                    lastShotTime = std::chrono::steady_clock::now();
                }
            }
        }
    }
    const glm::vec3 currentPress = getPressed();
    if (currentPress != lastPress && glm::length(currentPress) > 1e-6f) {
        lastPress = currentPress;
        lastPressTime = std::chrono::steady_clock::now();
    } else if (glm::length(currentPress) > 1e-6f) {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPressTime).count();
        if (duration >= 100 && duration <= 400 && (now - lastDashTime) >= std::chrono::duration<float>(dashCooldown)) {
            dash(currentPress, 100.0f);
            lastDashTime = now;
        }
        lastPressTime = now;
    }
}

void rind::Player::damage(float amount) {
    setHealth(getHealth() - amount);
    if (getHealth() <= 0.0f) {
        std::cout << "Player has died. Game Over." << std::endl;
    }
}

void rind::Player::shoot() {
    std::cout << "Shooting weapon!" << std::endl;
    glm::vec3 rayDir = -glm::normalize(glm::vec3(camera->getWorldTransform()[2]));
    std::vector<engine::Collider::Collision> hits = engine::Collider::raycast(
        getEntityManager(),
        camera->getWorldPosition(),
        rayDir,
        1000.0f,
        true
    );
    for (engine::Collider::Collision& collision : hits) {
        if (collision.other->getParent() == this) continue; // ignore self hits
        glm::vec3 normal = glm::normalize(collision.mtv.normal);
        glm::vec3 reflectedDir = glm::reflect(rayDir, normal);
        getEntityManager()->getRenderer()->getParticleManager()->burstParticles(
            glm::translate(glm::mat4(1.0f), collision.worldHitPoint),
            glm::vec4(1.0f, 0.5f, 0.0f, 1.0f),
            reflectedDir * 15.0f,
            20,
            5.0f
        );
        rind::Enemy* character = dynamic_cast<rind::Enemy*>(collision.other->getParent());
        if (character) {
            character->damage(25.0f);
            std::cout << "Hit " << character->getName() << " for 25 damage!" << std::endl;
        }
        break;
    }
}