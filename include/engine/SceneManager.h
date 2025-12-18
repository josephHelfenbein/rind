#pragma once

#include <engine/Renderer.h>
#include <engine/EntityManager.h>
#include <engine/UIManager.h>
#include <memory>
#include <string>
#include <functional>
#include <map>

namespace engine {
    class Scene {
    public:
        Scene(std::function<void(Renderer* renderer)> onLoad) : onLoad(onLoad) {};
        ~Scene() = default;
        void run(Renderer* renderer) { onLoad(renderer); }

    private:
        std::function<void(Renderer* renderer)> onLoad;
    };

    class SceneManager {
    public:
        SceneManager(Renderer* renderer, std::vector<std::unique_ptr<Scene>> scenes);
        ~SceneManager() = default;

        void setActiveScene(int index);

    private:
        Renderer* renderer;
        std::vector<std::unique_ptr<Scene>> scenes;
    };
};