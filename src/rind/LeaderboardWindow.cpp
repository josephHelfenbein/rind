#include <rind/LeaderboardWindow.h>
#include <rind/SteamManager.h>
#include <engine/Renderer.h>
#include <engine/UIManager.h>
#include <engine/TextureManager.h>
#include <engine/InputManager.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <functional>
#include <string>
#include <variant>
#include <vector>

rind::LeaderboardWindow::LeaderboardWindow(engine::Renderer* renderer)
    : engine::Entity(renderer->getEntityManager(), "leaderboardWindow", "", glm::mat4(1.0f), {}),
      renderer(renderer),
      uiManager(renderer->getUIManager()),
      textureManager(renderer->getTextureManager()) {
        panel = new engine::UIObject(
            uiManager,
            glm::scale(glm::mat4(1.0f), glm::vec3(kPanelW, kPanelH, 1.0f)),
            "leaderboardPanel",
            glm::vec4(0.5f, 0.5f, 0.6f, 0.9f),
            "ui_window",
            engine::Corner::Right
        );

        title = new engine::TextObject(
            uiManager,
            glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -30.0f, 0.0f)), glm::vec3(kTitleScale, kTitleScale, 1.0f)),
            "leaderboardTitle",
            kWhite,
            "LEADERBOARD",
            "RubikGlitch",
            engine::Corner::Top
        );
        panel->addChild(title);

        for (int i = 0; i < kRows; ++i) {
            Slot& s = slots[i];
            s.avatar = new engine::UIObject(
                uiManager,
                rowTransform(kAvatarX, i, kAvatarScale),
                "lb_avatar_obj_" + std::to_string(i),
                kWhite,
                "",
                engine::Corner::TopLeft
            );
            panel->addChild(s.avatar);

            s.rank = new engine::TextObject(
                uiManager,
                rowTransform(kRankX, i, kRowTextScale),
                "lb_rank_" + std::to_string(i),
                kWhite,
                std::to_string(i + 1),
                "Lato",
                engine::Corner::TopLeft
            );
            panel->addChild(s.rank);

            s.name = new engine::TextObject(
                uiManager,
                rowTransform(kNameX, i, kRowTextScale),
                "lb_name_" + std::to_string(i),
                kWhite,
                "-",
                "Lato",
                engine::Corner::TopLeft
            );
            panel->addChild(s.name);

            s.score = new engine::TextObject(
                uiManager,
                rowTransform(kScoreX, i, kRowTextScale),
                "lb_score_" + std::to_string(i),
                kWhite,
                "",
                "Lato",
                engine::Corner::TopRight
            );
            panel->addChild(s.score);
        }

        toggleButton = new engine::ButtonObject(
            uiManager,
            glm::scale(glm::mat4(1.0f), glm::vec3(0.025f, 0.04f, 1.0f)),
            "leaderboardToggle",
            glm::vec4(0.5f, 0.5f, 0.6f, 1.0f),
            kWhite,
            "ui_window",
            ">",
            "Lato",
            [this]() { setExpanded(!expanded); },
            engine::Corner::TopRight
        );
        panel->addChild(toggleButton);

        setExpanded(true);
        applyScreenFit();
        renderer->getInputManager()->registerRecreateSwapChainCallback(
            "leaderboardScreenFit", [this]() { applyScreenFit(); });
        rind::steam::requestLeaderboard();
    }

rind::LeaderboardWindow::~LeaderboardWindow() {
    renderer->getInputManager()->unregisterCallback("leaderboardScreenFit");
}

void rind::LeaderboardWindow::update(float deltaTime) {
    uint32_t v = rind::steam::leaderboardVersion();
    if (v == seenVersion) return;
    seenVersion = v;
    rebuild();
}

void rind::LeaderboardWindow::rebuild() {
    rind::steam::LeaderboardSnapshot snap = rind::steam::leaderboard();
    for (int i = 0; i < kRows; ++i) {
        Slot& s = slots[i];
        if (i < static_cast<int>(snap.rows.size())) {
            const rind::steam::LeaderboardRow& row = snap.rows[i];
            const glm::vec4& tint = row.isPlayer ? kGold : kWhite;
            s.rank->setText(std::to_string(row.rank));
            s.rank->setTint(tint);
            s.name->setText(row.name.empty() ? "-" : row.name);
            s.name->setTint(tint);
            s.score->setText(std::to_string(row.score));
            s.score->setTint(tint);

            std::string texName = "lb_avatar_" + std::to_string(row.steamId);
            if (uploaded.count(row.steamId)) {
                s.avatar->setTexture(texName);
            } else {
                std::vector<uint8_t> rgba;
                int w = 0, h = 0;
                if (rind::steam::getAvatarRGBA(row.steamId, rgba, w, h)) {
                    const int stride = w * 4;
                    for (int y = 0; y < h / 2; ++y) {
                        std::swap_ranges(
                            rgba.begin() + y * stride,
                            rgba.begin() + (y + 1) * stride,
                            rgba.begin() + (h - 1 - y) * stride);
                    }
                    textureManager->registerTextureFromRGBA(texName, rgba.data(), w, h);
                    s.avatar->setTexture(texName);
                    uploaded.insert(row.steamId);
                } else {
                    s.avatar->setTexture("");
                }
            }
        } else {
            s.rank->setText(std::to_string(i + 1));
            s.rank->setTint(kWhite);
            s.name->setText("-");
            s.name->setTint(kWhite);
            s.score->setText("");
            s.score->setTint(kWhite);
            s.avatar->setTexture("");
        }
    }
}

void rind::LeaderboardWindow::setExpanded(bool expand) {
    expanded = expand;
    panel->setTransform(glm::scale(glm::mat4(1.0f),
        glm::vec3((expand ? kPanelW : kThinW) * fitScale, kPanelH * fitScale, 1.0f)));
    if (title) title->setEnabled(expand);
    for (int i = 0; i < kRows; ++i) {
        slots[i].avatar->setEnabled(expand);
        slots[i].rank->setEnabled(expand);
        slots[i].name->setEnabled(expand);
        slots[i].score->setEnabled(expand);
    }
    if (engine::TextObject* t = uiManager->getTextObject("leaderboardToggle_text")) {
        t->setText(expand ? ">" : "<");
    }
}

void rind::LeaderboardWindow::applyScreenFit() {
    if (!panel) return;

    if (fitScale < 1.0f) {
        float undo = 1.0f / fitScale;
        glm::mat4 undoMat = glm::scale(glm::mat4(1.0f), glm::vec3(undo, undo, 1.0f));
        glm::mat4 rt = panel->getTransform();
        panel->setTransform(glm::scale(glm::mat4(1.0f), glm::vec3(rt[0][0] * undo, rt[1][1] * undo, 1.0f)));
        std::function<void(engine::UIObject*)> undoChildren = [&](engine::UIObject* parent) {
            for (auto& child : parent->getChildren()) {
                if (std::holds_alternative<engine::UIObject*>(child)) {
                    engine::UIObject* obj = std::get<engine::UIObject*>(child);
                    obj->setTransform(undoMat * obj->getTransform());
                    undoChildren(obj);
                } else {
                    engine::TextObject* textObj = std::get<engine::TextObject*>(child);
                    glm::mat4 t = textObj->getTransform();
                    t[3][0] *= undo;
                    t[3][1] *= undo;
                    textObj->setTransform(t);
                }
            }
        };
        undoChildren(panel);
        fitScale = 1.0f;
    }

    float contentScale = 1.0f;
#ifdef __APPLE__
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(renderer->getWindow(), &xscale, &yscale);
    contentScale = std::max(xscale, yscale);
#endif
    float layoutScale = std::max(renderer->getUIScale() * contentScale, 0.0001f);
    glm::mat4 currentTransform = panel->getTransform();
    float rootScaleY = currentTransform[1][1];
    if (engine::Texture* tex = textureManager->getTexture("ui_window")) {
        float panelPixelHeight = static_cast<float>(tex->height) * rootScaleY * layoutScale;
        float screenHeight = static_cast<float>(renderer->getSwapChainExtent().height);
        float maxHeight = 0.9f * screenHeight;
        if (panelPixelHeight > maxHeight) {
            float s = maxHeight / panelPixelHeight;
            panel->setTransform(glm::scale(glm::mat4(1.0f),
                glm::vec3(currentTransform[0][0] * s, rootScaleY * s, 1.0f)));
            glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(s, s, 1.0f));
            std::function<void(engine::UIObject*)> scaleChildren = [&](engine::UIObject* parent) {
                for (auto& child : parent->getChildren()) {
                    if (std::holds_alternative<engine::UIObject*>(child)) {
                        engine::UIObject* obj = std::get<engine::UIObject*>(child);
                        obj->setTransform(scaleMat * obj->getTransform());
                        scaleChildren(obj);
                    } else {
                        engine::TextObject* textObj = std::get<engine::TextObject*>(child);
                        glm::mat4 t = textObj->getTransform();
                        t[3][0] *= s;
                        t[3][1] *= s;
                        textObj->setTransform(t);
                    }
                }
            };
            scaleChildren(panel);
            fitScale = s;
        }
    }
}
