#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <condition_variable>

#include <engine/Renderer.h>
#include <engine/Profiler.h>

namespace engine {
    class WorkManager {
    public:
        WorkManager(Renderer* renderer) { // offload thread
            renderer->registerWorkManager(this);
            profiler::Profiler* profiler = renderer->getProfiler();
            PROFILER_REGISTER_OFFLOAD(profiler);
        }

        ~WorkManager() { // offload thread
            bool done = false;
            while (!done) {
                done = true;
                for (size_t i = 0; i < kMaxWorkRingSize; ++i) {
                    if (workFlags[i].load(std::memory_order_acquire)) {
                        workRing[i]();
                        done = false;
                        break;
                    }
                }
            }
        }

        static constexpr size_t kMaxWorkRingSize = 16;
        static constexpr uint8_t kMaxSpins = 30;

        void pushWork(std::function<void(void)>&& work) { // main thread
            writeIdx = (writeIdx + 1) % kMaxWorkRingSize;
            while (workFlags[writeIdx].load(std::memory_order_acquire)) {
                std::this_thread::yield(); // spin until slot is free
            }
            workRing[writeIdx] = std::move(work);
            workFlags[writeIdx].store(true, std::memory_order_release);
            notification.notify_one();
        }

        void spinRead() { // offload thread
            spins = 0;
            while (spins < kMaxSpins) {
                if (isDestroyed.load(std::memory_order_acquire)) {
                    delete this;
                    return;
                }
                bool hadWork = false;
                for (size_t readIdx{}; readIdx < kMaxWorkRingSize; ++readIdx) {
                    if (workFlags[readIdx].load(std::memory_order_acquire)) {
                        std::function<void(void)> hold = std::move(workRing[readIdx]);
                        workRing[readIdx] = nullptr;
                        workFlags[readIdx].store(false, std::memory_order_release);
                        hold();
                        hadWork = true;
                    }
                }
                if (hadWork) spins = 0;
                else spins++;
            }
            std::unique_lock<std::mutex> lock(sleeper);
            notification.wait(lock);
            spinRead();
        }

        void destroy() { // main thread
            isDestroyed.store(true, std::memory_order_release);
            notification.notify_one();
        }

    private:
        std::array<std::atomic<bool>, kMaxWorkRingSize> workFlags{};
        std::array<std::function<void(void)>, kMaxWorkRingSize> workRing{};
        size_t writeIdx{};
        uint8_t spins{};
        std::mutex sleeper;
        std::condition_variable notification;
        std::atomic<bool> isDestroyed{false};
    };
};