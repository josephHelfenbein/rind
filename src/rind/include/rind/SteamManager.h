#pragma once
#include <cstdint>

// this header is SDK-free and every function is a no-op when RIND_ENABLE_STEAM is undefined
#ifndef RIND_GAME_VERSION
#define RIND_GAME_VERSION "0.0.0-dev"
#endif

namespace rind::steam {
    inline constexpr uint32_t kAppId = 4412940u;
    inline constexpr char kWebApiIdentity[] = "rind-leaderboard";
    inline constexpr char kGameVersion[] = RIND_GAME_VERSION;

    bool restartAppIfNecessary();

    void init();
    void runCallbacks();
    void beginRun();
    void uploadScore(int32_t score);
    void shutdown();
};
