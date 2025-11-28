#pragma once

#include <engine/Renderer.h>
#include <memory>
#include <string>
#include <functional>
#include <map>

namespace engine {
    class Scene {
    public:
        Scene(std::function<void()> onLoad) : onLoad(onLoad) {};
        ~Scene() = default;
        void run() { onLoad(); }

    private:
        std::function<void()> onLoad;
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