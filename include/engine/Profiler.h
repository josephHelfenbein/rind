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

#ifdef NDEBUG

#include <array>
#include <string_view>
#include <engine/Renderer.h>

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
            return rdtsc() * 1000000000 / get_cpu_frequency();
            #endif
        }
    }

    template <Zone Z> class ScopedZone;

    class Profiler {
    public:
        Profiler(Renderer* renderer) {
            renderer->registerProfiler(this);
        }

        static constexpr size_t kMaxFrames = 120;

        std::array<FrameZones, kMaxFrames> dumpFrames() const { return ring; }

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