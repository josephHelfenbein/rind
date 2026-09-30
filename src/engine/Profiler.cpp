#include <engine/Profiler.h>

#ifndef NDEBUG // debug build

#include <engine/Platform.h>
#include <engine/ShaderManager.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#if defined(_WIN32)
    using Socket = SOCKET;
    constexpr Socket kInvalidSocket = INVALID_SOCKET;
    constexpr int kSendFlags = 0;
#else
    using Socket = int;
    constexpr Socket kInvalidSocket = -1;
    constexpr int kSendFlags = MSG_NOSIGNAL;
#endif

    // perfetto auto-trusts a loopback origin, and browsers exempt it from mixed-content blocking
    constexpr const char* kPerfettoOrigin = "https://ui.perfetto.dev";
    constexpr uint16_t kPreferredPort = 9001;

    void closeSocket(Socket socket) {
#if defined(_WIN32)
        closesocket(socket);
#else
        ::close(socket);
#endif
    }

    bool sendAll(Socket socket, const std::string& data) {
        size_t offset = 0;
        while (offset < data.size()) {
            const size_t remaining = data.size() - offset;
            const int chunk = static_cast<int>(remaining > (1u << 20) ? (1u << 20) : remaining);
            const auto sent = ::send(socket, data.data() + offset, chunk, kSendFlags);
            if (sent <= 0) return false;
            offset += static_cast<size_t>(sent);
        }
        return true;
    }

}

bool engine::profiler::Profiler::openTraceInPerfettoUI(const std::filesystem::path& tracePath, int timeoutSeconds) {
    std::ifstream file(tracePath, std::ios::binary);
    const std::string trace{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (trace.empty()) {
        std::cerr << "Profiler: no trace to open at " << tracePath << std::endl;
        return false;
    }

#if defined(_WIN32)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "Profiler: failed to start winsock" << std::endl;
        return false;
    }
    struct WsaGuard { ~WsaGuard() { WSACleanup(); } } wsaGuard;
#endif

    const Socket listener = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listener == kInvalidSocket) {
        std::cerr << "Profiler: failed to open trace socket" << std::endl;
        return false;
    }

    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(kPreferredPort);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        address.sin_port = 0; // preferred port taken, let os pick one
        if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            std::cerr << "Profiler: failed to bind trace socket" << std::endl;
            closeSocket(listener);
            return false;
        }
    }
    socklen_t addressLength = sizeof(address);
    if (::getsockname(listener, reinterpret_cast<sockaddr*>(&address), &addressLength) != 0
        || ::listen(listener, 4) != 0) {
        std::cerr << "Profiler: failed to listen for perfetto" << std::endl;
        closeSocket(listener);
        return false;
    }

    const std::string target = "/" + tracePath.filename().string();
    const std::string requestLine = "GET " + target;
    const std::string url = std::string(kPerfettoOrigin) + "/#!/?url=http://127.0.0.1:"
        + std::to_string(ntohs(address.sin_port)) + target + "&referrer=rind";
    const std::string response = "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(trace.size()) + "\r\n"
        "Access-Control-Allow-Origin: " + kPerfettoOrigin + "\r\n"
        "Cache-Control: no-cache\r\n"
        "Connection: close\r\n\r\n" + trace;

    std::cout << "Profiler: opening " << url << std::endl;
    if (!Platform::openURL(url)) {
        std::cerr << "Profiler: could not launch a browser, open the url above manually" << std::endl;
    }

    // serve the trace to the first client that asks for it by name, then stop
    bool served = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    while (!served && std::chrono::steady_clock::now() < deadline) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval timeout{1, 0};
        if (::select(static_cast<int>(listener) + 1, &readable, nullptr, nullptr, &timeout) <= 0) continue;

        const Socket client = ::accept(listener, nullptr, nullptr);
        if (client == kInvalidSocket) continue;

        std::string head;
        char buffer[2048];
        while (head.find("\r\n\r\n") == std::string::npos && head.size() < 16u * 1024u) {
            const auto received = ::recv(client, buffer, sizeof(buffer), 0);
            if (received <= 0) break;
            head.append(buffer, static_cast<size_t>(received));
        }
        // ignore anything but the trace itself so a stray probe cannot consume
        const size_t targetEnd = requestLine.size();
        if (head.size() > targetEnd && head.compare(0, targetEnd, requestLine) == 0
            && (head[targetEnd] == ' ' || head[targetEnd] == '?')) {
            served = sendAll(client, response);
        }
        closeSocket(client);
    }

    closeSocket(listener);
    if (!served) {
        std::cerr << "Profiler: perfetto never fetched the trace, it is still at " << tracePath << std::endl;
    }
    return served;
}

std::string_view engine::profiler::Profiler::nodeName(uint8_t i) const {
    const auto& graph = renderer->getShaderManager()->getRenderGraph();
    return i < graph.size() ? std::string_view(graph[i].name) : "unknown";
}

#endif
