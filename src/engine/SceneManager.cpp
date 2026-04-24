#include <engine/SceneManager.h>
#include <engine/ParticleManager.h>
#include <engine/VolumetricManager.h>
#include <engine/LightManager.h>
#include <engine/IrradianceManager.h>
#include <engine/AudioManager.h>

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
    vkDeviceWaitIdle(renderer->getDevice());
    renderer->resetPostProcessDescriptorPools();
    renderer->getEntityManager()->clear();
    renderer->getLightManager()->clear();
    renderer->getIrradianceManager()->clear();
    renderer->getUIManager()->clear();
    renderer->getUIManager()->createCursorObject();
    renderer->getParticleManager()->clear();
    renderer->getVolumetricManager()->clear();
    renderer->getAudioManager()->stopAllSounds();
    renderer->resetPerObjectDescriptorPools();
    scenes[index]->run(renderer);
    renderer->getIrradianceManager()->setIrradianceBakingPending(true);
    renderer->refreshDescriptorSets();
}

void engine::SceneManager::setActiveSceneDeferred(int index) {
    pendingSceneIndex = index;
}

void engine::SceneManager::processPendingSceneChange() {
    if (pendingSceneIndex != -1) {
        setActiveScene(pendingSceneIndex);
        pendingSceneIndex = -1;
    }
}