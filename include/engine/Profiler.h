#pragma once

#include <cstdint>

namespace engine {
namespace profiler {

// also update kZoneNames
enum class Zone : uint8_t {
    // root
    Setup, Cleanup, Throttle, WaitFences, DeferredVulkan, Acquire, BuildGraph, Update, Record, Submit, Present,
    // children
    Setup_PollEvents, Setup_BeginLambda, Setup_ScreenMode, Setup_Input, Setup_Scene, Setup_Attachments,
    Cleanup_Deletions, Cleanup_Additions, Cleanup_UI, Cleanup_ShadowMaps,
    DeferredVulkan_ClearObjects, DeferredVulkan_PostProcess, DeferredVulkan_ShadowMaps, DeferredVulkan_GrowParticleBuffer, DeferredVulkan_GrowVolumetricBuffer, DeferredVulkan_Irradiance,
    Update_Entities, Update_Audio, Update_Particles, Update_Volumetrics,
    Update_ParticlesBuffer, Update_VolumetricsBuffer, Update_Audio_Listener,
    Update_Entities_Transforms, Update_Entities_SpatialGrid, Update_Entities_DynamicColliders, Update_Entities_Update, Update_Entities_Animations, Update_Entities_LoadTextures,
    Update_Particles_Integrate, Update_Particles_Collision, Update_Particles_Compact,
    Count
};

};
};

#ifndef NDEBUG // debug build

#include <array>
#include <atomic>
#include <string_view>
#include <engine/Renderer.h>
#include <engine/IO.h>
#include <vulkan/vulkan.h>

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
        "Setup", "Cleanup", "Throttle", "WaitFences", "DeferredVulkan", "Acquire", "BuildGraph", "Update", "Record", "Submit", "Present",
        "Setup_PollEvents", "Setup_BeginLambda", "Setup_ScreenMode", "Setup_Input", "Setup_Scene", "Setup_Attachments",
        "Cleanup_Deletions", "Cleanup_Additions", "Cleanup_UI", "Cleanup_ShadowMaps",
        "DeferredVulkan_ClearObjects", "DeferredVulkan_PostProcess", "DeferredVulkan_ShadowMaps", "DeferredVulkan_GrowParticleBuffer", "DeferredVulkan_GrowVolumetricBuffer", "DeferredVulkan_Irradiance",
        "Update_Entities", "Update_Audio", "Update_Particles", "Update_Volumetrics",
        "Update_ParticlesBuffer", "Update_VolumetricsBuffer", "Update_Audio_Listener",
        "Update_Entities_Transforms", "Update_Entities_SpatialGrid", "Update_Entities_DynamicColliders", "Update_Entities_Update", "Update_Entities_Animations", "Update_Entities_LoadTextures",
        "Update_Particles_Integrate", "Update_Particles_Collision", "Update_Particles_Compact"
    };

    static constexpr size_t kMaxGpuSpans = 255;

    struct Span {
        uint32_t startNs = 0;
        uint32_t endNs = 0;
    };

    struct GpuSpan {
        uint8_t nodeIdx = 0;
        uint8_t subIdx = 0;
        uint8_t queue = 0; // 0 = graphics, 1 = compute
        Span span;
    };

    struct GpuFrameSlot {
        size_t ringIdx = SIZE_MAX;
        uint8_t passCount = 0;
        bool pending = false;
        std::array<uint8_t, profiler::kMaxGpuSpans> node{};
        std::array<uint8_t, profiler::kMaxGpuSpans> queue{};
    };

    struct FrameZones {
        uint64_t startNs = 0;
        uint32_t endNs = 0;
        std::array<Span, size_t(Zone::Count)> zones{};
        uint8_t gpuSpanCount = 0;
        std::array<GpuSpan, kMaxGpuSpans> gpuSpans{};
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
            return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW);
            #else // linux
            timespec ts;
            clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
            return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull + ts.tv_nsec;
            #endif
        }

        inline uint64_t hostToNs(uint64_t t) {
            #ifdef _WIN32
            static LARGE_INTEGER frequency;
            static bool initialized = false;
            if (!initialized) {
                QueryPerformanceFrequency(&frequency);
                initialized = true;
            }
            return t * 1000000000ull / static_cast<uint64_t>(frequency.QuadPart);
            #else // linux / macOS
            return t; // clock_gettime_nsec_np returns nanoseconds
            #endif
        }
    }

    template <Zone Z> class ScopedZone;

    class Profiler {
    public:
        Profiler(Renderer* renderer, std::string_view profileLocation)
            : renderer(renderer), profileLocation(profileLocation) {
                renderer->registerProfiler(this);
            }

        static constexpr size_t kMaxFrames = 120;

        void dumpFrames() const {
            const size_t frameIdx = currentFrameIndex.load(std::memory_order_acquire);
            const auto& copy = ring;
            const size_t oldest = (frameIdx + 1) % kMaxFrames;
            if (copy[oldest].endNs == 0) return; // ring not full

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
            meta("thread_name", processId, 100000000, "GPU Graphics");
            meta("thread_name", processId, 100000001, "GPU Compute");

            const uint64_t baseNs = copy[oldest].startNs;

            for (size_t i = 0; i < kMaxFrames; ++i) {
                const FrameZones& frame = copy[(oldest + i) % kMaxFrames];
                if (frame.endNs == 0) continue;
                const uint64_t frameStartNs = frame.startNs - baseNs;
                slice("Frame", processId, threadId, "frame", us(frameStartNs), us(frame.endNs));
                for (size_t z = 0; z < static_cast<size_t>(Zone::Count); ++z) {
                    const Span& span = frame.zones[z];
                    if (span.endNs == 0) continue;
                    slice(kZoneNames[z], processId, threadId, "cpu", us(frameStartNs + span.startNs), us(span.endNs));
                }
                for (size_t g = 0; g < frame.gpuSpanCount; ++g) {
                    const GpuSpan& span = frame.gpuSpans[g];
                    if (span.span.endNs == 0) continue;
                    slice(nodeName(span.nodeIdx), processId, 100000000 + span.queue, "gpu", us(frameStartNs + span.span.startNs), us(span.span.endNs));
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
            size_t frameIdx = (currentFrameIndex.load(std::memory_order_release) + 1) % kMaxFrames;
            currentFrameIndex.store(frameIdx, std::memory_order_release);
            auto& frame = ring[frameIdx];
            frame.startNs = Clock::Now();
            frame.endNs = 0;
            frame.zones.fill(Span{0, 0});
            frame.gpuSpans.fill(GpuSpan{0, 0, 0, Span{0, 0}});
            frame.gpuSpanCount = 0;
        }

        void endFrame() {
            auto& frame = ring[currentFrameIndex.load(std::memory_order_relaxed)];
            frame.endNs = static_cast<uint32_t>(Clock::Now() - frame.startNs);
        }

        template <Zone Z>
        void zoneEnter() {
            const auto now = Clock::Now();
            auto& frame = ring[currentFrameIndex.load(std::memory_order_relaxed)];
            frame.zones[size_t(Z)].startNs = static_cast<uint32_t>(now - frame.startNs);
        }

        template <Zone Z>
        void zoneExit() {
            const auto now = Clock::Now();
            auto& frame = ring[currentFrameIndex.load(std::memory_order_relaxed)];
            const auto startNs = frame.zones[size_t(Z)].startNs;
            frame.zones[size_t(Z)].endNs = static_cast<uint32_t>(now - startNs - frame.startNs);
        }

        void addGpuSpan(size_t ringIndex, const GpuSpan& span) {
            if (ringIndex >= kMaxFrames) return;
            auto& frame = ring[ringIndex];
            if (frame.gpuSpanCount < kMaxGpuSpans) {
                frame.gpuSpans[frame.gpuSpanCount++] = span;
            }
        }

        void setupGpuProfiling(
            VkDevice device,
            float tsPeriod,
            const std::vector<VkQueueFamilyProperties>& queueFamilies,
            std::optional<uint32_t> graphicsFamily,
            std::optional<uint32_t> computeFamily,
            bool hasAsyncComputeQueue
        ) {
            gpuTsPeriod = tsPeriod;
            calibratedTimestampsSupported &= gpuTsPeriod > 0.0f;
            if (!calibratedTimestampsSupported) return;
            gfxTsMask = queueFamilies[graphicsFamily.value()].timestampValidBits ?
                ((~0ull) >> (64 - queueFamilies[graphicsFamily.value()].timestampValidBits)) : 0;
            cmpTsMax = computeFamily.has_value() && queueFamilies[computeFamily.value()].timestampValidBits ?
                ((~0ull) >> (64 - queueFamilies[computeFamily.value()].timestampValidBits)) : 0;
            gfxTsEnabled = gpuTsPeriod > 0.0f && gfxTsMask != 0;
            cmpTsEnabled = gpuTsPeriod > 0.0f && hasAsyncComputeQueue && cmpTsMax != 0;
            VkQueryPoolCreateInfo queryPoolCreateInfo = {
                .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
                .queryType = VK_QUERY_TYPE_TIMESTAMP,
                .queryCount = 2 * profiler::kMaxGpuSpans * Renderer::MAX_FRAMES_IN_FLIGHT,
            };
            vkCreateQueryPool(device, &queryPoolCreateInfo, nullptr, &gpuQueryPool);
        }

        void gpuFrameReset() {
            if (!calibratedTimestampsSupported) return;
            auto& slot = gpuFrameSlots[renderer->getCurrentFrameIndex()];
            if (slot.pending && slot.passCount) {
                readbackGpuTimestamps(slot); // deferred readback
            }
            const uint32_t base = renderer->getCurrentFrameIndex() * 2 * kMaxGpuSpans;
            vkResetQueryPool(renderer->getDevice(), gpuQueryPool, base, 2 * kMaxGpuSpans);
            slot.ringIdx = currentFrameIndex;
            slot.passCount = 0;
            slot.pending = true;
        }

        int gpuZoneBegin(VkCommandBuffer cmd, uint8_t nodeIdx, uint8_t queue) {
            if (!gfxTsEnabled || (queue == 1 && !cmpTsEnabled)) return -1;
            auto& slot = gpuFrameSlots[renderer->getCurrentFrameIndex()];
            if (slot.passCount >= kMaxGpuSpans) return -1;
            const uint8_t passIdx = slot.passCount++;
            slot.node[passIdx] = nodeIdx;
            slot.queue[passIdx] = queue;
            const uint32_t base = renderer->getCurrentFrameIndex() * 2 * kMaxGpuSpans;
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuQueryPool, base + 2 * passIdx);
            return passIdx;
        }

        void gpuZoneEnd(VkCommandBuffer cmd, int passIdx) {
            if (passIdx < 0) return;
            const uint32_t base = renderer->getCurrentFrameIndex() * 2 * kMaxGpuSpans;
            vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpuQueryPool, base + 2 * passIdx + 1);
        }

        struct ScopedGpuZone {
            ScopedGpuZone(Profiler* profiler, VkCommandBuffer cmd, uint8_t nodeIdx, uint8_t queue)
                : profiler(profiler), cmd(cmd) {
                    if (profiler) {
                        passIdx = profiler->gpuZoneBegin(cmd, nodeIdx, queue);
                    }
                }

            ~ScopedGpuZone() {
                if (profiler) {
                    profiler->gpuZoneEnd(cmd, passIdx);
                }
            }
            Profiler* profiler;
            VkCommandBuffer cmd;
            int passIdx = -1;
        };

        void readbackGpuTimestamps(GpuFrameSlot& slot) {
            const uint32_t base = renderer->getCurrentFrameIndex() * 2 * kMaxGpuSpans;
            uint64_t r[2 * kMaxGpuSpans];
            vkGetQueryPoolResults(renderer->getDevice(), gpuQueryPool, base, 2 * slot.passCount, sizeof(uint64_t) * 2 * slot.passCount, r, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);

            sampleCalibration();
            auto gpuToCpuNs = [&](uint64_t gpuTicks) -> double {
                return static_cast<double>(calibrationHostNs) + (static_cast<double>(gpuTicks) - static_cast<double>(calibrationGpuTicks)) * gpuTsPeriod;
            };
            const uint64_t frameOrigin = ring[slot.ringIdx].startNs;

            for (uint8_t p = 0; p < slot.passCount; ++p) {
                const uint64_t mask = (slot.queue[p] == 0) ? gfxTsMask : cmpTsMax;
                const uint64_t startNs = gpuToCpuNs(r[2 * p] & mask);
                const uint64_t endNs = gpuToCpuNs(r[2 * p + 1] & mask);
                const double rel = static_cast<double>(startNs - static_cast<uint64_t>(frameOrigin));
                addGpuSpan(slot.ringIdx, {
                    .nodeIdx = slot.node[p],
                    .subIdx = 0,
                    .queue = slot.queue[p],
                    .span = {
                        static_cast<uint32_t>(rel > 0.0 ? rel : 0.0),
                        .endNs = static_cast<uint32_t>(endNs - startNs)
                    }
                });
            }
            slot.pending = false;
        }

        void sampleCalibration() {
            if (!calibratedTimestampsSupported || hostDomain == VK_TIME_DOMAIN_MAX_ENUM_KHR) return;
            VkCalibratedTimestampInfoKHR infos[2] = {
                {
                    .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                    .pNext = nullptr,
                    .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR
                },
                {
                    .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                    .pNext = nullptr,
                    .timeDomain = hostDomain
                }
            };
            uint64_t timestamps[2];
            uint64_t maxDeviation;
            renderer->getFpGetCalibratedTimestamps()(renderer->getDevice(), 2, infos, timestamps, &maxDeviation);
            calibrationGpuTicks = timestamps[0];
            calibrationHostNs = Clock::hostToNs(timestamps[1]);
        }

        void setCalibratedTimestampsSupported(bool supported, VkTimeDomainKHR domain) {
            calibratedTimestampsSupported = supported;
            hostDomain = domain;
        }

    private:
        std::array<FrameZones, kMaxFrames> ring{};
        std::array<GpuFrameSlot, Renderer::MAX_FRAMES_IN_FLIGHT> gpuFrameSlots;
        VkQueryPool gpuQueryPool = VK_NULL_HANDLE;
        float gpuTsPeriod = 0.0f; // ns per tick
        uint64_t gfxTsMask = 0; // (1<<validBits)-1 per queue family
        uint64_t cmpTsMax = 0;
        bool gfxTsEnabled = false; // graphics queue supports timestamp queries
        bool cmpTsEnabled = false; // compute queue supports timestamp queries
        uint64_t calibrationGpuTicks = 0;
        uint64_t calibrationHostNs = 0;
        bool calibratedTimestampsSupported = false;
        VkTimeDomainKHR hostDomain = VK_TIME_DOMAIN_MAX_ENUM_KHR;
        Renderer* renderer;
        std::atomic<size_t> currentFrameIndex{};
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

        std::string_view nodeName(uint8_t i) const {
            const auto& graph = renderer->getShaderManager()->getRenderGraph();
            return i < graph.size() ? std::string_view(graph[i].name) : "unknown";
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

    #define PROFILER_GPU_RESET(profiler) \
        do { if (profiler) profiler->gpuFrameReset(); } while (0)

    #define PROFILER_ZONE(profiler, Z) \
        ::engine::profiler::ScopedZone<Z> \
            PROFILER_CONCAT(prof_zone_, __LINE__){ (profiler) }

    #define PROFILER_GPU_ZONE(profiler, cmd, nodeIdx, queue) \
        ::engine::profiler::Profiler::ScopedGpuZone \
            PROFILER_CONCAT(prof_gpu_zone_, __LINE__){ (profiler), (cmd), (nodeIdx), (queue) }

};
};
};

#else

#define PROFILER_FRAME(profiler) \
    do {} while (0)

#define PROFILER_ZONE(profiler, Z) \
    do {} while (0)

#define PROFILER_GPU_RESET(profiler) \
    do {} while (0)

#define PROFILER_GPU_ZONE(profiler, cmd, nodeIdx, queue) \
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