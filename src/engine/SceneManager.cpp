#include <engine/SceneManager.h>

engine::SceneManager::SceneManager(Renderer* renderer, std::vector<std::unique_ptr<Scene>> scenes)
    : renderer(renderer), scenes(std::move(scenes)) {
    if (this->scenes.empty()) {
        throw std::invalid_argument("Scenes vector cannot be empty");
    }
    renderer->registerSceneManager(this);
}

void engine::SceneManager::setActiveScene(int index) {
    if (index < 0 || index >= scenes.size()) {
        throw std::out_of_range("Scene index out of range");
    }
    renderer->getEntityManager()->clear();
    renderer->getUIManager()->clear();
    scenes[index]->run(renderer->getEntityManager(), renderer->getUIManager(), this);
}