#pragma once

#include <cstdint>

namespace engine {
namespace profiler {

// also update kZoneNames
enum class Zone : uint8_t {
    // root
    Throttle, WaitFences, Acquire, BuildGraph, Update, Record, Submit, Present,
    // update children
    Update_Entities, Update_Audio, Update_Particles, Update_Volumetrics,
    Update_Particles_Buffer, Update_Volumetrics_Buffer, Update_Audio_Listener,
    Count
};

};
};

#ifndef NDEBUG // debug build

#include <array>
#include <string_view>
#include <engine/Renderer.h>
#include <engine/IO.h>

#ifdef _WIN32
#include <windows.h>
#elif __APPLE__
#include <unistd.h>
#include <pthread.h>
#else // linux
#include <unistd.h>
#include <sys/syscall.h>
#endif

namespace engine {
namespace profiler {

    // parent-child relationship defined by underscore prefixes
    inline constexpr std::array<std::string_view, static_cast<size_t>(Zone::Count)> kZoneNames = {
        "Throttle", "WaitFences", "Acquire", "BuildGraph", "Update", "Record", "Submit", "Present",
        "Update_Entities", "Update_Audio", "Update_Particles", "Update_Volumetrics",
        "Update_Particles_Buffer", "Update_Volumetrics_Buffer", "Update_Audio_Listener"
    };

    struct Span {
        uint32_t startNs = 0;
        uint32_t endNs = 0;
    };

    struct FrameZones {
        uint64_t startNs = 0;
        uint32_t endNs = 0;
        std::array<Span, size_t(Zone::Count)> zones{};
    };

    namespace Clock {
        inline uint64_t Now() {
            #ifdef _WIN32
            static LARGE_INTEGER frequency;
            static bool initialized = false;
            if (!initialized) {
                QueryPerformanceFrequency(&frequency);
                initialized = true;
            }
            LARGE_INTEGER counter;
            QueryPerformanceCounter(&counter);
            return static_cast<uint64_t>(counter.QuadPart * 1000000000 / frequency.QuadPart);
            #elif __APPLE__
            return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
            #else // linux
            timespec ts;
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
            return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
            #endif
        }
    }

    template <Zone Z> class ScopedZone;

    class Profiler {
    public:
        Profiler(Renderer* renderer, std::string_view profileLocation)
            : profileLocation(profileLocation) {
                renderer->registerProfiler(this);
            }

        static constexpr size_t kMaxFrames = 120;

        void dumpFrames() const {
            const size_t oldest = (currentFrameIndex + 1) % kMaxFrames;
            if (ring[oldest].endNs == 0) return; // ring not full

            std::filesystem::path path = getConfigDirectory(profileLocation) / "profile.json";
            
            std::string out;
            // estimated 128 chars per zone, 256 chars for header
            out.reserve(kMaxFrames * static_cast<size_t>(Zone::Count) * 128 + 256);
            out += "{\"displayTimeUnit\":\"ns\",\"traceEvents\":[\n";
            auto us = [](uint64_t ns) { return static_cast<double>(ns) / 1000.0; };
            auto meta = [&](const char* name, int pid, uint64_t tid, const char* value) {
                char buf[256];
                out.append(buf, std::snprintf(buf, sizeof(buf),
                    "{\"ph\":\"M\",\"pid\":%d,\"tid\":%llu,\"name\":\"%s\",\"args\":{\"name\":\"%s\"}},\n",
                    pid, tid, name, value
                ));
            };
            auto slice = [&](std::string_view name, int pid, uint64_t tid, const char* cat, double ts, double dur) {
                char buf[256];
                out.append(buf, std::snprintf(buf, sizeof(buf),
                    "{\"ph\":\"X\",\"pid\":%d,\"tid\":%llu,\"cat\":\"%s\",\"ts\":%.3f,\"dur\":%.3f,\"name\":\"%.*s\"},\n",
                    pid, tid, cat, ts, dur, static_cast<int>(name.size()), name.data()
                ));
            };
            meta("process_name", processId, 1, "Rind");
            meta("thread_name", processId, threadId, "CPU Main");

            const uint64_t baseNs = ring[oldest].startNs;

            for (size_t i = 0; i < kMaxFrames; ++i) {
                const FrameZones& frame = ring[(oldest + i) % kMaxFrames];
                if (frame.endNs == 0) continue;
                const uint64_t frameStartNs = frame.startNs - baseNs;
                char fname[32];
                std::snprintf(fname, sizeof(fname), "Frame %zu", i);
                slice(fname, processId, threadId, "frame", us(frameStartNs), us(frame.endNs));
                for (size_t z = 0; z < static_cast<size_t>(Zone::Count); ++z) {
                    const Span& span = frame.zones[z];
                    if (span.endNs == 0) continue;
                    slice(kZoneNames[z], processId, threadId, "cpu", us(frameStartNs + span.startNs), us(span.endNs));
                }
            }
            if (out.ends_with(",\n")) { // trim comma
                out.resize(out.size() - 2);
            }
            out += "\n]}\n";
            engine::writeFile(path, out);
        }

        struct ScopedFrame {
            ScopedFrame(Profiler* profiler) : profiler(profiler) {
                if (profiler) {
                    profiler->beginFrame();
                }
            }
            Profiler* profiler;
            ~ScopedFrame() { if (profiler) profiler->endFrame(); }
            ScopedFrame(ScopedFrame&& o) : profiler(o.profiler) { o.profiler = nullptr; }
            ScopedFrame(const ScopedFrame&) = delete;
        };
        [[nodiscard]] ScopedFrame scopedFrame() { beginFrame(); return {this}; }

        void beginFrame() {
            currentFrameIndex = (currentFrameIndex + 1) % kMaxFrames;
            ring[currentFrameIndex].startNs = Clock::Now();
            ring[currentFrameIndex].endNs = 0;
            ring[currentFrameIndex].zones.fill(Span{0, 0});
        }

        void endFrame() {
            auto& frame = ring[currentFrameIndex];
            frame.endNs = static_cast<uint32_t>(Clock::Now() - frame.startNs);
        }

        template <Zone Z>
        void zoneEnter() {
            const auto now = Clock::Now();
            auto& frame = ring[currentFrameIndex];
            frame.zones[size_t(Z)].startNs = static_cast<uint32_t>(now - frame.startNs);
        }

        template <Zone Z>
        void zoneExit() {
            const auto now = Clock::Now();
            auto& frame = ring[currentFrameIndex];
            const auto startNs = frame.zones[size_t(Z)].startNs;
            frame.zones[size_t(Z)].endNs = static_cast<uint32_t>(now - startNs - frame.startNs);
        }

    private:
        std::array<FrameZones, kMaxFrames> ring{};
        size_t currentFrameIndex = 0;
        std::string profileLocation;
        int processId = currentProcessId();
        uint64_t threadId = currentThreadId();

        inline uint64_t currentThreadId() {
            thread_local const uint64_t tid = []() -> uint64_t {
                #ifdef _WIN32
                return static_cast<uint64_t>(GetCurrentThreadId());
                #elif __APPLE__
                uint64_t t = 0;
                pthread_threadid_np(nullptr, &t);
                return t;
                #else // linux
                return static_cast<uint64_t>(::syscall(SYS_gettid));
                #endif
            }();
            return tid;
        }

        inline int currentProcessId() {
            #ifdef _WIN32
            return GetCurrentProcessId();
            #else // linux / macOS
            return ::getpid();
            #endif
        }
    };

    template <Zone Z>
    class ScopedZone {
    public:
        explicit ScopedZone(Profiler* profiler) : profiler(profiler) {
            if (profiler) {
                profiler->zoneEnter<Z>();
            }
        }
        ~ScopedZone() {
            if (profiler) {
                profiler->zoneExit<Z>();
            }
        }
    private:
        Profiler* profiler;
    };
    
namespace {

    #define PROFILER_CONCAT_(a,b) a##b
    #define PROFILER_CONCAT(a,b) PROFILER_CONCAT_(a,b)

    #define PROFILER_FRAME(profiler) \
        ::engine::profiler::Profiler::ScopedFrame \
            PROFILER_CONCAT(prof_frame_, __LINE__){ (profiler) }

    #define PROFILER_ZONE(profiler, Z) \
        ::engine::profiler::ScopedZone<Z> \
            PROFILER_CONCAT(prof_zone_, __LINE__){ (profiler) }

};
};
};

#else

#define PROFILER_FRAME(profiler) \
    do {} while (0)

#define PROFILER_ZONE(profiler, Z) \
    do {} while (0)

namespace engine {
namespace profiler {
    class Profiler {
    public:
        Profiler(Renderer* renderer) {}
        ~Profiler() {}
    };
};
};

#endif