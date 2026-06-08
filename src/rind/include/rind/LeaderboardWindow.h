#pragma once
#include <engine/EntityManager.h>
#include <engine/UIManager.h>
#include <engine/TextureManager.h>
#include <engine/Renderer.h>
#include <rind/SteamManager.h>
#include <cstdint>
#include <string>
#include <unordered_set>

namespace rind {
    class LeaderboardWindow : public engine::Entity {
    public:
        explicit LeaderboardWindow(engine::Renderer* renderer);
        ~LeaderboardWindow() override;

        void update(float deltaTime) override;

    private:
        static constexpr int kRows = rind::steam::kLeaderboardRows;

        static constexpr float kPanelW = 0.35f;
        static constexpr float kThinW = 0.04f;
        static constexpr float kPanelH = 0.7f;
        static constexpr float kRowH = 52.0f;
        static constexpr float kTopMargin = 118.0f;
        static constexpr float kAvatarX = 16.0f;
        static constexpr float kRankX = 65.0f;
        static constexpr float kNameX = 100.0f;
        static constexpr float kScoreX = -16.0f;
        static constexpr float kAvatarScale = 0.53f;
        static constexpr float kTitleScale = 0.085f;
        static constexpr float kRowTextScale = 0.05f;

        const glm::vec4 kWhite = glm::vec4(1.0f);
        const glm::vec4 kGold = glm::vec4(1.0f, 0.84f, 0.2f, 1.0f);

        inline glm::mat4 rowTransform(float x, int row, float scale) {
            return glm::scale(
                glm::translate(glm::mat4(1.0f), glm::vec3(x, -kTopMargin - row * kRowH, 0.0f)),
                glm::vec3(scale, scale, 1.0f)
            );
        }

        void rebuild();
        void setExpanded(bool expand);
        void applyScreenFit();

        engine::Renderer* renderer;
        engine::UIManager* uiManager;
        engine::TextureManager* textureManager;
        engine::UIObject* panel = nullptr;
        engine::TextObject* title = nullptr;
        engine::ButtonObject* toggleButton = nullptr;

        struct Slot {
            engine::UIObject* avatar = nullptr;
            engine::TextObject* rank = nullptr;
            engine::TextObject* name = nullptr;
            engine::TextObject* score = nullptr;
        };
        Slot slots[kRows];

        std::unordered_set<uint64_t> uploaded;
        uint32_t seenVersion = 0;
        bool expanded = true;
        float fitScale = 1.0f;
    };
};
