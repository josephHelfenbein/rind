#include <rind/SteamManager.h>

#if RIND_ENABLE_STEAM

#include "steam/steam_api.h"
#include "steam/isteamuser.h"
#include "steam/isteamuserstats.h"
#include "steam/isteamfriends.h"
#include "steam/isteamutils.h"
#include "steam/isteamhttp.h"

#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>

// backend config baked in at build time
#ifndef RIND_LEADERBOARD_URL
#define RIND_LEADERBOARD_URL ""
#endif
#ifndef RIND_LEADERBOARD_TOKEN
#define RIND_LEADERBOARD_TOKEN ""
#endif

namespace {
    constexpr char kHexDigits[] = "0123456789abcdef";

    std::string toHex(const uint8* data, int len) {
        std::string s;
        s.reserve(static_cast<size_t>(len) * 2);
        for (int i = 0; i < len; ++i) { s += kHexDigits[data[i] >> 4]; s += kHexDigits[data[i] & 0xF]; }
        return s;
    }

    std::string randomHex(int bytes) {
        std::random_device rd;
        std::uniform_int_distribution<int> d(0, 255);
        std::string s;
        s.reserve(static_cast<size_t>(bytes) * 2);
        for (int i = 0; i < bytes; ++i) { const int b = d(rd); s += kHexDigits[b >> 4]; s += kHexDigits[b & 0xF]; }
        return s;
    }

    // one in-flight HTTP POST
    class HttpPost {
    public:
        bool finished = false;
        HTTPRequestHandle req = INVALID_HTTPREQUEST_HANDLE;
        CCallResult<HttpPost, HTTPRequestCompleted_t> result;

        void send(HTTPRequestHandle r, SteamAPICall_t call) {
            req = r;
            result.Set(call, this, &HttpPost::onDone);
        }

        void onDone(HTTPRequestCompleted_t* p, bool ioFailure) {
            if (ioFailure || p == nullptr || !p->m_bRequestSuccessful) {
                std::cerr << "[steam] leaderboard POST failed (transport error)" << std::endl;
            } else if (p->m_eStatusCode >= 400) {
                // surface errors in logs
                const std::string reason = readBody();
                std::cerr << "[steam] leaderboard POST rejected (HTTP " << static_cast<int>(p->m_eStatusCode) << ")";
                if (!reason.empty()) std::cerr << ": " << reason;
                std::cerr << std::endl;
            }
            if (SteamHTTP() && req != INVALID_HTTPREQUEST_HANDLE) {
                SteamHTTP()->ReleaseHTTPRequest(req);
                req = INVALID_HTTPREQUEST_HANDLE;
            }
            finished = true;
        }

    private:
        std::string readBody() {
            ISteamHTTP* http = SteamHTTP();
            if (!http || req == INVALID_HTTPREQUEST_HANDLE) return "";
            uint32 size = 0;
            if (!http->GetHTTPResponseBodySize(req, &size) || size == 0 || size > 2048) return "";
            std::string buf(size, '\0');
            if (!http->GetHTTPResponseBodyData(req, reinterpret_cast<uint8*>(buf.data()), size)) return "";
            return buf;
        }
    };

    class SteamLeaderboard {
    public:
        constexpr static int kRows = rind::steam::kLeaderboardRows;
        using LeaderboardRow = rind::steam::LeaderboardRow;
        using LeaderboardSnapshot = rind::steam::LeaderboardSnapshot;

        bool restartAppIfNecessary() {
            return SteamAPI_RestartAppIfNecessary(rind::steam::kAppId);
        }

        void init() {
            SteamErrMsg err = {};
            if (SteamAPI_InitEx(&err) != k_ESteamAPIInitResult_OK) {
                // Steam is unavailable, submissions stay no-ops
                std::cerr << "[steam] SteamAPI_InitEx failed: " << err << std::endl;
                m_initialized = false;
                return;
            }
            m_initialized = true;

            m_url = RIND_LEADERBOARD_URL;
            m_token = RIND_LEADERBOARD_TOKEN;
            if (!m_url.empty() && m_url.rfind("https://", 0) != 0) {
                std::cerr << "[steam] RIND_LEADERBOARD_URL must be https://; leaderboard submission disabled" << std::endl;
                m_url.clear();
            }
            if (m_url.empty()) {
                std::cerr << "[steam] no leaderboard URL compiled in; submission disabled" << std::endl;
            }
            m_ticketCb.Register(this, &SteamLeaderboard::OnGetTicket);
            m_personaCb.Register(this, &SteamLeaderboard::OnPersonaStateChange);
            m_avatarCb.Register(this, &SteamLeaderboard::OnAvatarImageLoaded);
        }

        void runCallbacks() {
            if (!m_initialized) return;
            SteamAPI_RunCallbacks();
            for (size_t i = 0; i < m_posts.size();) {
                if (m_posts[i]->finished) m_posts.erase(m_posts.begin() + static_cast<long>(i));
                else ++i;
            }
        }

        void beginRun() {
            // fresh auth ticket per run
            if (SteamUser() && m_hAuthTicket != k_HAuthTicketInvalid) {
                SteamUser()->CancelAuthTicket(m_hAuthTicket);
                m_hAuthTicket = k_HAuthTicketInvalid;
            }
            m_ticketHex.clear();
            if (SteamUser()) {
                m_hAuthTicket = SteamUser()->GetAuthTicketForWebApi(rind::steam::kWebApiIdentity);
            }
            m_seed = randomHex(16); // token
            queueOrSend(false, m_seed, 0);
        }

        void uploadScore(int32 score) {
            if (m_seed.empty()) {
                // shouldn't happen
                m_seed = randomHex(16);
            }
            queueOrSend(true, m_seed, score);
        }

        void shutdown() {
            if (!m_initialized) return;
            if (SteamUser() && m_hAuthTicket != k_HAuthTicketInvalid) {
                SteamUser()->CancelAuthTicket(m_hAuthTicket);
                m_hAuthTicket = k_HAuthTicketInvalid;
            }
            m_ticketCb.Unregister();
            m_personaCb.Unregister();
            m_avatarCb.Unregister();
            SteamAPI_Shutdown();
            m_initialized = false;
        }

        void requestLeaderboard() {
            if (!SteamUserStats()) return;
            if (m_hLeaderboard != 0) { downloadTop(); return; }
            SteamAPICall_t call = SteamUserStats()->FindLeaderboard(rind::steam::kLeaderboardName);
            m_findCall.Set(call, this, &SteamLeaderboard::OnLbFound);
        }

        uint32_t leaderboardVersion() { return m_version; }

        LeaderboardSnapshot leaderboard() { return m_snapshot; }

        bool getAvatarRGBA(uint64_t steamId, std::vector<uint8_t>& rgba, int& width, int& height) {
            auto it = m_avatars.find(steamId);
            if (it == m_avatars.end()) return false;
            rgba = it->second.rgba;
            width = it->second.w;
            height = it->second.h;
            return true;
        }

    private:
        struct PendingEvent { bool isEnd; std::string seed; int32 score; };
        struct Entry { int32 rank; int32 score; uint64 steamId; };
        struct Avatar { std::vector<uint8_t> rgba; int w; int h; };

        void OnLbFound(LeaderboardFindResult_t* p, bool ioFailure) {
            if (ioFailure || p == nullptr || !p->m_bLeaderboardFound) return;
            m_hLeaderboard = p->m_hSteamLeaderboard;
            downloadTop();
        }

        void downloadTop() {
            if (!SteamUserStats() || m_hLeaderboard == 0) return;
            SteamAPICall_t call = SteamUserStats()->DownloadLeaderboardEntries(
                m_hLeaderboard, k_ELeaderboardDataRequestGlobal, 1, kRows);
            m_topCall.Set(call, this, &SteamLeaderboard::OnTopDownloaded);
        }

        void OnTopDownloaded(LeaderboardScoresDownloaded_t* p, bool ioFailure) {
            m_top.clear();
            if (!ioFailure && p != nullptr && SteamUserStats()) {
                int count = p->m_cEntryCount;
                if (count > kRows) count = kRows;
                for (int index = 0; index < count; ++index) {
                    LeaderboardEntry_t e;
                    if (SteamUserStats()->GetDownloadedLeaderboardEntry(
                            p->m_hSteamLeaderboardEntries, index, &e, nullptr, 0)) {
                        m_top.push_back({e.m_nGlobalRank, e.m_nScore, e.m_steamIDUser.ConvertToUint64()});
                    }
                }
            }
            downloadPlayer();
        }

        void downloadPlayer() {
            if (!SteamUserStats() || m_hLeaderboard == 0) { compose(); return; }
            SteamAPICall_t call = SteamUserStats()->DownloadLeaderboardEntries(
                m_hLeaderboard, k_ELeaderboardDataRequestGlobalAroundUser, 0, 0);
            m_playerCall.Set(call, this, &SteamLeaderboard::OnPlayerDownloaded);
        }

        void OnPlayerDownloaded(LeaderboardScoresDownloaded_t* p, bool ioFailure) {
            m_hasPlayer = false;
            if (!ioFailure && p != nullptr && p->m_cEntryCount > 0 && SteamUserStats()) {
                LeaderboardEntry_t e;
                if (SteamUserStats()->GetDownloadedLeaderboardEntry(
                        p->m_hSteamLeaderboardEntries, 0, &e, nullptr, 0)) {
                    m_player = {e.m_nGlobalRank, e.m_nScore, e.m_steamIDUser.ConvertToUint64()};
                    m_hasPlayer = true;
                }
            }
            compose();
        }

        void compose() {
            uint64 localId = SteamUser() ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
            std::vector<Entry> chosen;
            if (m_hasPlayer && m_player.rank <= kRows - 1) {
                chosen = m_top;
            } else if (m_hasPlayer) {
                for (size_t i = 0; i < m_top.size() && i < kRows - 1; ++i) chosen.push_back(m_top[i]);
                chosen.push_back(m_player);
            } else {
                chosen = m_top;
            }

            m_snapshot.rows.clear();
            for (const auto& e : chosen) {
                LeaderboardRow row;
                row.rank = e.rank;
                row.score = e.score;
                row.steamId = e.steamId;
                row.isPlayer = (e.steamId == localId);
                m_snapshot.rows.push_back(row);
            }
            m_snapshot.ready = true;
            ++m_version;
            resolveNamesAndAvatars();
        }

        // name looks unresolved when Steam hands back the placeholder
        static bool nameUnresolved(const char* n) {
            return n == nullptr || n[0] == '\0' || std::string(n) == "[unknown]";
        }

        void resolveNamesAndAvatars() {
            if (!SteamFriends()) return;
            for (auto& row : m_snapshot.rows) {
                CSteamID id(static_cast<uint64>(row.steamId));
                const char* n = SteamFriends()->GetFriendPersonaName(id);
                row.name = nameUnresolved(n) ? "" : n;
                bool haveAvatar = m_avatars.count(row.steamId) != 0;
                if (nameUnresolved(n) || !haveAvatar) {
                    SteamFriends()->RequestUserInformation(id, false);
                }
                tryLoadAvatar(row.steamId);
            }
        }

        // pull the medium avatar if Steam has it cached
        bool tryLoadAvatar(uint64 steamId) {
            if (!SteamFriends() || !SteamUtils()) return false;
            int img = SteamFriends()->GetMediumFriendAvatar(CSteamID(steamId));
            if (img <= 0) return false;
            uint32 w = 0, h = 0;
            if (!SteamUtils()->GetImageSize(img, &w, &h) || w == 0 || h == 0) return false;
            std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
            if (!SteamUtils()->GetImageRGBA(img, buf.data(), static_cast<int>(buf.size()))) return false;
            m_avatars[steamId] = {std::move(buf), static_cast<int>(w), static_cast<int>(h)};
            return true;
        }

        bool isOurRow(uint64 steamId) const {
            for (const auto& row : m_snapshot.rows) if (row.steamId == steamId) return true;
            return false;
        }

        void OnPersonaStateChange(PersonaStateChange_t* p) {
            if (p == nullptr || !isOurRow(p->m_ulSteamID)) return;
            bool changed = false;
            if (SteamFriends()) {
                const char* n = SteamFriends()->GetFriendPersonaName(CSteamID(p->m_ulSteamID));
                for (auto& row : m_snapshot.rows) {
                    if (row.steamId == p->m_ulSteamID) { row.name = nameUnresolved(n) ? "" : n; changed = true; }
                }
            }
            if (tryLoadAvatar(p->m_ulSteamID)) changed = true;
            if (changed) ++m_version;
        }

        void OnAvatarImageLoaded(AvatarImageLoaded_t* p) {
            if (p == nullptr || !SteamUtils()) return;
            uint64 id = p->m_steamID.ConvertToUint64();
            if (!isOurRow(id)) return;
            std::vector<uint8_t> buf(static_cast<size_t>(p->m_iWide) * p->m_iTall * 4);
            if (!SteamUtils()->GetImageRGBA(p->m_iImage, buf.data(), static_cast<int>(buf.size()))) return;
            m_avatars[id] = {std::move(buf), p->m_iWide, p->m_iTall};
            ++m_version;
        }

        void OnGetTicket(GetTicketForWebApiResponse_t* p) {
            if (p == nullptr || p->m_hAuthTicket != m_hAuthTicket) return;
            if (p->m_eResult != k_EResultOK || p->m_cubTicket <= 0) {
                std::cerr << "[steam] GetAuthTicketForWebApi failed (result="
                          << static_cast<int>(p->m_eResult) << ")" << std::endl;
                return;
            }
            m_ticketHex = toHex(p->m_rgubTicket, p->m_cubTicket);
            std::vector<PendingEvent> pending;
            pending.swap(m_pending);
            for (const auto& ev : pending) sendPost(ev.isEnd, ev.seed, ev.score);
        }

        void queueOrSend(bool isEnd, const std::string& seed, int32 score) {
            if (!m_initialized || m_url.empty()) return;
            if (m_ticketHex.empty()) {
                m_pending.push_back({isEnd, seed, score});
                return;
            }
            sendPost(isEnd, seed, score);
        }

        void sendPost(bool isEnd, const std::string& seed, int32 score) {
            ISteamHTTP* http = SteamHTTP();
            if (!http || m_url.empty() || m_ticketHex.empty()) return;

            std::string body = std::string("{\"event\":\"") + (isEnd ? "end" : "start")
                + "\",\"seed\":\"" + seed
                + "\",\"ticket\":\"" + m_ticketHex
                + "\",\"version\":\"" + rind::steam::kGameVersion + "\"";
            if (isEnd) body += ",\"score\":" + std::to_string(score);
            body += "}";

            HTTPRequestHandle req = http->CreateHTTPRequest(k_EHTTPMethodPOST, m_url.c_str());
            if (req == INVALID_HTTPREQUEST_HANDLE) return;
            if (!m_token.empty()) {
                http->SetHTTPRequestHeaderValue(req, "X-App-Token", m_token.c_str());
            }
            http->SetHTTPRequestRawPostBody(req, "application/json",
                reinterpret_cast<uint8*>(const_cast<char*>(body.data())),
                static_cast<uint32>(body.size()));

            SteamAPICall_t call = k_uAPICallInvalid;
            if (!http->SendHTTPRequest(req, &call)) {
                http->ReleaseHTTPRequest(req);
                return;
            }
            auto post = std::make_unique<HttpPost>();
            post->send(req, call);
            m_posts.push_back(std::move(post));
        }

        bool m_initialized = false;
        std::string m_url; // leaderboard backend URL (compiled in)
        std::string m_token; // optional soft token (compiled in)
        HAuthTicket m_hAuthTicket = k_HAuthTicketInvalid; // current run's auth ticket handle
        std::string m_ticketHex; // hex Web API auth ticket (current run)
        std::string m_seed; // current run's session token
        std::vector<PendingEvent> m_pending; // queued until the ticket arrives
        std::vector<std::unique_ptr<HttpPost>> m_posts; // in-flight POSTs
        CCallbackManual<SteamLeaderboard, GetTicketForWebApiResponse_t> m_ticketCb;

        SteamLeaderboard_t m_hLeaderboard = 0; // cached leaderboard handle
        std::vector<Entry> m_top; // downloaded top rows
        Entry m_player = {}; // the player's own row
        bool m_hasPlayer = false; // player has an entry on the board
        LeaderboardSnapshot m_snapshot; // composed display list
        std::unordered_map<uint64, Avatar> m_avatars; // steamId to rgba
        uint32_t m_version = 0; // bumps on any change
        CCallResult<SteamLeaderboard, LeaderboardFindResult_t> m_findCall;
        CCallResult<SteamLeaderboard, LeaderboardScoresDownloaded_t> m_topCall;
        CCallResult<SteamLeaderboard, LeaderboardScoresDownloaded_t> m_playerCall;
        CCallbackManual<SteamLeaderboard, PersonaStateChange_t> m_personaCb;
        CCallbackManual<SteamLeaderboard, AvatarImageLoaded_t> m_avatarCb;
    };

    SteamLeaderboard& instance() {
        static SteamLeaderboard s_instance;
        return s_instance;
    }

};

namespace rind::steam {

    bool restartAppIfNecessary() { return instance().restartAppIfNecessary(); }
    void init() { instance().init(); }
    void runCallbacks() { instance().runCallbacks(); }
    void beginRun() { instance().beginRun(); }
    void uploadScore(int32_t score) { instance().uploadScore(score); }
    void shutdown() { instance().shutdown(); }

    void requestLeaderboard() { instance().requestLeaderboard(); }
    uint32_t leaderboardVersion() { return instance().leaderboardVersion(); }
    LeaderboardSnapshot leaderboard() { return instance().leaderboard(); }
    bool getAvatarRGBA(uint64_t steamId, std::vector<uint8_t>& rgba, int& width, int& height) {
        return instance().getAvatarRGBA(steamId, rgba, width, height);
    }

};

#else

namespace rind::steam {

    bool restartAppIfNecessary() { return false; }
    void init() {}
    void runCallbacks() {}
    void beginRun() {}
    void uploadScore(int32_t) {}
    void shutdown() {}

    void requestLeaderboard() {}
    uint32_t leaderboardVersion() { return 0; }
    LeaderboardSnapshot leaderboard() { return {}; }
    bool getAvatarRGBA(uint64_t, std::vector<uint8_t>&, int&, int&) { return false; }

};

#endif
