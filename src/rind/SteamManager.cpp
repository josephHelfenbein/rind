#include <rind/SteamManager.h>

#if RIND_ENABLE_STEAM

#include "steam/steam_api.h"
#include "steam/isteamuser.h"
#include "steam/isteamhttp.h"

#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <iostream>

namespace {

    struct EnvConfig {
        std::string url; // RIND_LEADERBOARD_URL (backend endpoint, not secret)
        std::string token; // RIND_LEADERBOARD_TOKEN (optional soft gate, not security)
    };

    std::string trim(const std::string& s) {
        const size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        const size_t b = s.find_last_not_of(" \t\r\n");
        std::string out = s.substr(a, b - a + 1);
        if (out.size() >= 2 && (out.front() == '"' || out.front() == '\'') && out.back() == out.front()) {
            out = out.substr(1, out.size() - 2);
        }
        return out;
    }

    EnvConfig loadEnv() {
        EnvConfig cfg;
        std::ifstream f(".env");
        if (!f) return cfg;
        std::string line;
        while (std::getline(f, line)) {
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            const size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = trim(t.substr(0, eq));
            const std::string val = trim(t.substr(eq + 1));
            if (key == "RIND_LEADERBOARD_URL") cfg.url = val;
            else if (key == "RIND_LEADERBOARD_TOKEN") cfg.token = val;
        }
        return cfg;
    }

    std::string toHex(const uint8* data, int len) {
        static const char* H = "0123456789abcdef";
        std::string s;
        s.reserve(static_cast<size_t>(len) * 2);
        for (int i = 0; i < len; ++i) { s += H[data[i] >> 4]; s += H[data[i] & 0xF]; }
        return s;
    }

    std::string randomHex(int bytes) {
        static const char* H = "0123456789abcdef";
        std::random_device rd;
        std::uniform_int_distribution<int> d(0, 255);
        std::string s;
        s.reserve(static_cast<size_t>(bytes) * 2);
        for (int i = 0; i < bytes; ++i) { const int b = d(rd); s += H[b >> 4]; s += H[b & 0xF]; }
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

            m_cfg = loadEnv();
            if (!m_cfg.url.empty() && m_cfg.url.rfind("https://", 0) != 0) {
                std::cerr << "[steam] RIND_LEADERBOARD_URL must be https://; leaderboard submission disabled" << std::endl;
                m_cfg.url.clear();
            }
            if (m_cfg.url.empty()) {
                std::cerr << "[steam] no valid RIND_LEADERBOARD_URL in .env; leaderboard submission disabled" << std::endl;
            }
            m_ticketCb.Register(this, &SteamLeaderboard::OnGetTicket);
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
            SteamAPI_Shutdown();
            m_initialized = false;
        }

    private:
        struct PendingEvent { bool isEnd; std::string seed; int32 score; };

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
            if (!m_initialized || m_cfg.url.empty()) return;
            if (m_ticketHex.empty()) {
                m_pending.push_back({isEnd, seed, score});
                return;
            }
            sendPost(isEnd, seed, score);
        }

        void sendPost(bool isEnd, const std::string& seed, int32 score) {
            ISteamHTTP* http = SteamHTTP();
            if (!http || m_cfg.url.empty() || m_ticketHex.empty()) return;

            std::string body = std::string("{\"event\":\"") + (isEnd ? "end" : "start")
                + "\",\"seed\":\"" + seed
                + "\",\"ticket\":\"" + m_ticketHex
                + "\",\"version\":\"" + rind::steam::kGameVersion + "\"";
            if (isEnd) body += ",\"score\":" + std::to_string(score);
            body += "}";

            HTTPRequestHandle req = http->CreateHTTPRequest(k_EHTTPMethodPOST, m_cfg.url.c_str());
            if (req == INVALID_HTTPREQUEST_HANDLE) return;
            if (!m_cfg.token.empty()) {
                http->SetHTTPRequestHeaderValue(req, "X-App-Token", m_cfg.token.c_str());
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
        EnvConfig m_cfg;
        HAuthTicket m_hAuthTicket = k_HAuthTicketInvalid; // current run's auth ticket handle
        std::string m_ticketHex; // hex Web API auth ticket (current run)
        std::string m_seed; // current run's session token
        std::vector<PendingEvent> m_pending; // queued until the ticket arrives
        std::vector<std::unique_ptr<HttpPost>> m_posts; // in-flight POSTs
        CCallbackManual<SteamLeaderboard, GetTicketForWebApiResponse_t> m_ticketCb;
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

};

#else

namespace rind::steam {

    bool restartAppIfNecessary() { return false; }
    void init() {}
    void runCallbacks() {}
    void beginRun() {}
    void uploadScore(int32_t) {}
    void shutdown() {}

};

#endif
