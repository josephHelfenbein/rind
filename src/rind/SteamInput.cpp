#include <rind/SteamInput.h>

#if RIND_ENABLE_STEAM

#include "steam/steam_api.h"
#include "steam/isteaminput.h"
#include <stb/stb_image.h>
#include <GLFW/glfw3.h>
#include <rind/SteamManager.h>
#include <rind/GamepadBindings.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <system_error>
#include <unordered_set>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#include <limits.h>
#endif

namespace {
    std::filesystem::path executableDir() {
    #if defined(__APPLE__)
        uint32_t size = PATH_MAX;
        std::vector<char> buf(size + 1, '\0');
        if (_NSGetExecutablePath(buf.data(), &size) != 0) {
            buf.assign(size + 1, '\0');
            if (_NSGetExecutablePath(buf.data(), &size) != 0) return {};
        }
        std::error_code ec;
        auto p = std::filesystem::weakly_canonical(std::filesystem::path(buf.data()), ec);
        return ec ? std::filesystem::path{} : p.parent_path();
    #elif defined(_WIN32)
        char buf[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
        if (n == 0 || n == MAX_PATH) return {};
        std::error_code ec;
        auto p = std::filesystem::weakly_canonical(std::filesystem::path(std::string(buf, n)), ec);
        return ec ? std::filesystem::path{} : p.parent_path();
    #else
        std::error_code ec;
        auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
        return ec ? std::filesystem::path{} : p.parent_path();
    #endif
    }

    using rind::GameAction;
    using rind::steaminput::ActionSet;

    // canonical Steam Input action names
    enum DigitalAction {
        Digital_Jump = 0,
        Digital_Heal,
        Digital_Grenade,
        Digital_Punch,
        Digital_Pause,
        // menu set
        Digital_MenuUp,
        Digital_MenuDown,
        Digital_MenuLeft,
        Digital_MenuRight,
        Digital_MenuSelect,
        Digital_MenuCancel,
        Digital_Count
    };

    enum AnalogAction {
        Analog_Move = 0, // left stick
        Analog_Look, // right stick
        Analog_Shoot, // right trigger
        Analog_Dash, // left trigger
        Analog_Count
    };

    constexpr const char* kDigitalNames[Digital_Count] = {
        "jump", "heal", "grenade", "punch", "pause",
        "menu_up", "menu_down", "menu_left", "menu_right", "menu_select", "menu_cancel"
    };
    constexpr const char* kAnalogNames[Analog_Count] = {
        "move", "look", "shoot", "dash"
    };

    constexpr GameAction kDigitalAction[Digital_Count] = {
        GameAction::Jump, GameAction::Heal, GameAction::Grenade, GameAction::Punch, GameAction::Pause,
        GameAction::MenuUp, GameAction::MenuDown, GameAction::MenuLeft, GameAction::MenuRight,
        GameAction::MenuSelect, GameAction::MenuCancel
    };

    class SteamInputManager {
    public:
        void init() {
            if (!SteamInput()) return;
            if (!m_inited) {
                SteamInput()->Init(true);
                std::error_code ec;
                std::filesystem::path manifest = executableDir() / "controller_config" /
                    ("game_actions_" + std::to_string(rind::steam::kAppId) + ".vdf");
                if (std::filesystem::exists(manifest, ec) && !ec) {
                    SteamInput()->SetInputActionManifestFilePath(manifest.string().c_str());
                }
                m_inited = true;
            }

            m_actionSets[static_cast<int>(ActionSet::Gameplay)] =
                SteamInput()->GetActionSetHandle("gameplay");
            m_actionSets[static_cast<int>(ActionSet::Menu)] =
                SteamInput()->GetActionSetHandle("menu");

            for (int i = 0; i < Digital_Count; ++i) {
                m_digital[i] = SteamInput()->GetDigitalActionHandle(kDigitalNames[i]);
            }
            for (int i = 0; i < Analog_Count; ++i) {
                m_analog[i] = SteamInput()->GetAnalogActionHandle(kAnalogNames[i]);
            }
            m_handlesReady = (m_digital[Digital_Jump] != 0);
        }

        void setTextureManager(engine::TextureManager* textureManager) {
            m_textureManager = textureManager;
        }

        void shutdown() {
            if (m_inited && SteamInput()) {
                SteamInput()->Shutdown();
            }
            m_inited = false;
            m_active = 0;
            m_handlesReady = false;
            m_textureManager = nullptr;
            m_glyphCache.clear();
            for (auto& s : m_prevDigital) s = false;
            for (auto& z : m_prevAxisZone) z = 0;
            for (auto& v : m_prevAxisValue) v = 0.0f;
            m_haveAxisValue = false;
        }

        void runFrame() {
            if (!SteamInput()) return;
            SteamInput()->RunFrame();

            if (!m_handlesReady) {
                init();
                if (!m_handlesReady) return;
            }

            InputHandle_t handles[STEAM_INPUT_MAX_COUNT];
            int count = SteamInput()->GetConnectedControllers(handles);
            if (count > 0) {
                if (m_active != handles[0]) {
                    m_active = handles[0];
                    applyActionSet();
                }
            } else {
                m_active = 0;
            }
        }

        bool isActive() const { return m_active != 0; }

        void setActionSet(ActionSet set) {
            m_currentSet = set;
            applyActionSet();
        }

        void collectEvents(std::vector<engine::InputEvent>& out) {
            if (!isActive() || !SteamInput()) return;

            // digital buttons
            for (int i = 0; i < Digital_Count; ++i) {
                int button = rind::actionToGamepadButton(kDigitalAction[i]);
                if (button < 0) continue;
                if (m_digital[i] == 0) continue;
                InputDigitalActionData_t d =
                    SteamInput()->GetDigitalActionData(m_active, m_digital[i]);
                bool pressed = d.bActive && d.bState;
                if (pressed != m_prevDigital[i]) {
                    engine::InputEvent ev{};
                    ev.type = pressed ? engine::InputEvent::Type::GamepadButtonPress
                                      : engine::InputEvent::Type::GamepadButtonRelease;
                    ev.gamepadButtonEvent = { button, 0 };
                    out.push_back(ev);
                    m_prevDigital[i] = pressed;
                }
            }

            // analog sticks
            emitStick(out, Analog_Move, GLFW_GAMEPAD_AXIS_LEFT_X, GLFW_GAMEPAD_AXIS_LEFT_Y);
            emitStick(out, Analog_Look, GLFW_GAMEPAD_AXIS_RIGHT_X, GLFW_GAMEPAD_AXIS_RIGHT_Y);

            // analog triggers
            emitTrigger(out, Analog_Shoot, rind::actionToGamepadTrigger(GameAction::Shoot));
            emitTrigger(out, Analog_Dash, rind::actionToGamepadTrigger(GameAction::Dash));

            m_haveAxisValue = true;
        }

        std::string glyphTextureName(GameAction action) {
            if (!isActive() || !SteamInput()) return "";

            EInputActionOrigin origins[STEAM_INPUT_MAX_ORIGINS];
            int numOrigins = 0;
            InputActionSetHandle_t setHandle = m_actionSets[static_cast<int>(m_currentSet)];

            int digital = digitalForAction(action);
            int analog = (digital < 0) ? analogForAction(action) : -1;

            if (digital >= 0 && m_digital[digital] != 0) {
                numOrigins = SteamInput()->GetDigitalActionOrigins(
                    m_active, setHandle, m_digital[digital], origins);
            } else if (analog >= 0 && m_analog[analog] != 0) {
                numOrigins = SteamInput()->GetAnalogActionOrigins(
                    m_active, setHandle, m_analog[analog], origins);
            } else {
                return "";
            }

            if (numOrigins <= 0) return "";
            EInputActionOrigin origin = origins[0];
            if (origin == k_EInputActionOrigin_None) return "";

            const std::string name = "steamglyph_" + std::to_string(static_cast<int>(origin));
            if (m_glyphCache.count(static_cast<int>(origin))) return name;
            if (!m_textureManager) return "";

            const char* pngPath = SteamInput()->GetGlyphPNGForActionOrigin(
                origin, k_ESteamInputGlyphSize_Medium, ESteamInputGlyphStyle_Knockout);
            if (!pngPath || pngPath[0] == '\0') return "";

            int w = 0, h = 0, channels = 0;
            unsigned char* rgba = stbi_load(pngPath, &w, &h, &channels, 4);
            if (!rgba) return "";

            bool ok = m_textureManager->createTextureFromRGBA(name, rgba, w, h);
            stbi_image_free(rgba);
            if (!ok) return "";

            m_glyphCache.insert(static_cast<int>(origin));
            return name;
        }

    private:
        void applyActionSet() {
            if (m_active == 0 || !SteamInput()) return;
            SteamInput()->ActivateActionSet(
                m_active, m_actionSets[static_cast<int>(m_currentSet)]);
        }

        void emitStick(std::vector<engine::InputEvent>& out, int analog, int axisX, int axisY) {
            if (m_analog[analog] == 0) return;
            InputAnalogActionData_t a = SteamInput()->GetAnalogActionData(m_active, m_analog[analog]);
            float x = a.bActive ? a.x : 0.0f;
            float y = a.bActive ? -a.y : 0.0f;
            emitAxis(out, axisX, x);
            emitAxis(out, axisY, y);
        }

        void emitTrigger(std::vector<engine::InputEvent>& out, int analog, int axis) {
            if (m_analog[analog] == 0) return;
            InputAnalogActionData_t a = SteamInput()->GetAnalogActionData(m_active, m_analog[analog]);
            float t = a.bActive ? a.x : 0.0f;
            emitAxis(out, axis, t * 2.0f - 1.0f);
        }

        void emitAxis(std::vector<engine::InputEvent>& out, int axis, float value) {
            float prev = m_prevAxisValue[axis];
            if (!m_haveAxisValue || std::abs(value - prev) > engine::InputManager::axisMoveThreshold) {
                engine::InputEvent ev{};
                ev.type = engine::InputEvent::Type::GamepadAxisMove;
                ev.gamepadAxisEvent = { axis, value };
                out.push_back(ev);
            }

            int newZone = (value > engine::InputManager::axisDeadzone) ? 1
                : (value < -engine::InputManager::axisDeadzone) ? -1 : 0;
            if (newZone != m_prevAxisZone[axis]) {
                if (m_prevAxisZone[axis] != 0) {
                    engine::InputEvent rel{};
                    rel.type = engine::InputEvent::Type::GamepadAxisRelease;
                    rel.gamepadAxisEvent = { axis, static_cast<float>(m_prevAxisZone[axis]) };
                    out.push_back(rel);
                }
                if (newZone != 0) {
                    engine::InputEvent pr{};
                    pr.type = engine::InputEvent::Type::GamepadAxisPress;
                    pr.gamepadAxisEvent = { axis, static_cast<float>(newZone) };
                    out.push_back(pr);
                }
                m_prevAxisZone[axis] = newZone;
            }
            m_prevAxisValue[axis] = value;
        }

        static int digitalForAction(GameAction action) {
            for (int i = 0; i < Digital_Count; ++i) {
                if (kDigitalAction[i] == action) return i;
            }
            return -1;
        }

        static int analogForAction(GameAction action) {
            switch (action) {
                case GameAction::MoveForward:
                case GameAction::MoveBackward:
                case GameAction::MoveLeft:
                case GameAction::MoveRight: return Analog_Move;
                case GameAction::Look: return Analog_Look;
                case GameAction::Shoot: return Analog_Shoot;
                case GameAction::Dash: return Analog_Dash;
                default: return -1;
            }
        }

        engine::TextureManager* m_textureManager = nullptr;
        InputHandle_t m_active = 0;
        bool m_inited = false;
        bool m_handlesReady = false;
        ActionSet m_currentSet = ActionSet::Gameplay;

        InputActionSetHandle_t m_actionSets[2] = {0, 0};
        InputDigitalActionHandle_t m_digital[Digital_Count] = {0};
        InputAnalogActionHandle_t m_analog[Analog_Count] = {0};

        bool m_prevDigital[Digital_Count] = {false};
        std::array<int, GLFW_GAMEPAD_AXIS_LAST + 1> m_prevAxisZone = {0};
        std::array<float, GLFW_GAMEPAD_AXIS_LAST + 1> m_prevAxisValue = {0.0f};
        bool m_haveAxisValue = false;

        std::unordered_set<int> m_glyphCache;
    };

    SteamInputManager& instance() {
        static SteamInputManager s;
        return s;
    }
};

namespace rind::steaminput {
    void init() { instance().init(); }
    void shutdown() { instance().shutdown(); }
    void setTextureManager(engine::TextureManager* textureManager) {
        instance().setTextureManager(textureManager);
    }
    void runFrame() { instance().runFrame(); }
    bool isActive() { return instance().isActive(); }
    void setActionSet(ActionSet set) { instance().setActionSet(set); }
    void collectEvents(std::vector<engine::InputEvent>& out) { instance().collectEvents(out); }
    std::string glyphTextureName(rind::GameAction action) {
        return instance().glyphTextureName(action);
    }
}

#else

namespace rind::steaminput {
    void init() {}
    void shutdown() {}
    void setTextureManager(engine::TextureManager*) {}
    void runFrame() {}
    bool isActive() { return false; }
    void setActionSet(ActionSet) {}
    void collectEvents(std::vector<engine::InputEvent>&) {}
    std::string glyphTextureName(rind::GameAction) { return ""; }
}

#endif
