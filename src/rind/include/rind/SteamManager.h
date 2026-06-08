#pragma once
#include <cstdint>
#include <string>
#include <vector>

// this header is SDK-free and every function is a no-op when RIND_ENABLE_STEAM is undefined
#ifndef RIND_GAME_VERSION
#define RIND_GAME_VERSION "0.0.0-dev"
#endif

namespace rind::steam {
    inline constexpr uint32_t kAppId = 4412940u;
    inline constexpr char kWebApiIdentity[] = "rind-leaderboard";
    inline constexpr char kLeaderboardName[] = "highscores";
    inline constexpr char kGameVersion[] = RIND_GAME_VERSION;

    bool restartAppIfNecessary();

    void init();
    void runCallbacks();
    void beginRun();
    void uploadScore(int32_t score);
    void shutdown();

    // leaderboard read for the title-screen window
    struct LeaderboardRow {
        int32_t rank = 0;
        int32_t score = 0;
        uint64_t steamId = 0;
        std::string name; // empty until resolved
        bool isPlayer = false;
    };
    struct LeaderboardSnapshot {
        bool ready = false;
        std::vector<LeaderboardRow> rows;
    };

    void requestLeaderboard();
    uint32_t leaderboardVersion();
    LeaderboardSnapshot leaderboard();
    bool getAvatarRGBA(uint64_t steamId, std::vector<uint8_t>& rgba, int& width, int& height);
};
