#pragma once

#include <engine/Renderer.h>
#include <engine/EntityManager.h>
#include <memory>
#include <string>
#include <functional>
#include <map>

namespace engine {
    class Scene {
    public:
        Scene(std::function<void(EntityManager*)> onLoad) : onLoad(onLoad) {};
        ~Scene() = default;
        void run(EntityManager* entityManager) { onLoad(entityManager); }

    private:
        std::function<void(EntityManager*)> onLoad;
    };

    class SceneManager {
    public:
        SceneManager(Renderer* renderer, EntityManager* entityManager, std::vector<std::unique_ptr<Scene>> scenes);
        ~SceneManager() = default;

        void setActiveScene(int index);

    private:
        Renderer* renderer;
        EntityManager* entityManager;
        std::vector<std::unique_ptr<Scene>> scenes;
    };
};