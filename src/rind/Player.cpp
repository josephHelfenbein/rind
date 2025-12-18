#include <rind/Player.h>

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
        }
    }
}