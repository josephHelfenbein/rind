#include <engine/SceneManager.h>

engine::SceneManager::SceneManager(Renderer* renderer, EntityManager* entityManager, std::vector<std::unique_ptr<Scene>> scenes)
    : renderer(renderer), entityManager(entityManager), scenes(std::move(scenes)) {
    if (scenes.empty()) {
        throw std::invalid_argument("Scenes vector cannot be empty");
    }
    setActiveScene(0);
}

void engine::SceneManager::setActiveScene(int index) {
    if (index < 0 || index >= scenes.size()) {
        throw std::out_of_range("Scene index out of range");
    }
    entityManager->clear();
    scenes[index]->run(entityManager);
}