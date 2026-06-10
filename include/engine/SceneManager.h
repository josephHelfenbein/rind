#pragma once

#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace engine {
    class Renderer;
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
        void setActiveSceneDeferred(int index);
        void processPendingSceneChange();
        bool hasPendingSceneChange() const { return pendingSceneIndex != -1; }

    private:
        Renderer* renderer;
        std::vector<std::unique_ptr<Scene>> scenes;
        int pendingSceneIndex = -1;
    };
};