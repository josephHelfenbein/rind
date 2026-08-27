#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {
    class ThreadPool {
    public:
        static constexpr size_t kMaxWorkRingSize = 16;
        static constexpr uint8_t kMaxSpins = 30;

        using ChunkFn = std::function<void(size_t /*beginIdx*/, size_t /*endIdx*/, size_t /*chunkIdx*/)>;

        static ThreadPool& global() {
            static ThreadPool instance;
            return instance;
        }

        size_t workerCount() const { return workers.size(); }

        size_t numChunks(size_t begin, size_t end, size_t minChunk) const {
            if (end <= begin) return 0;
            const size_t n = end - begin;
            const size_t maxChunks = workers.size() + 1;
            if (maxChunks <= 1 || minChunk == 0) return 1;
            const size_t byMin = (n + minChunk - 1) / minChunk;
            if (byMin <= 1) return 1;
            return std::min(byMin, maxChunks);
        }

        void parallelForChunks(size_t begin, size_t end, size_t minChunk, const ChunkFn& fn) {
            if (end <= begin) return;

            if (inParallel) {
                fn(begin, end, 0);
                return;
            }

            const size_t chunks = numChunks(begin, end, minChunk);
            if (chunks <= 1) {
                fn(begin, end, 0);
                return;
            }

            const size_t n = end - begin;
            const size_t chunkSize = (n + chunks - 1) / chunks;

            std::atomic<size_t> remaining{chunks - 1};
            inParallel = true;
            for (size_t c = 1; c < chunks; ++c) {
                const size_t b = begin + c * chunkSize;
                const size_t e = std::min(b + chunkSize, end);
                writeIdx = (writeIdx + 1) % kMaxWorkRingSize;
                while (workFlags[writeIdx].load(std::memory_order_acquire)) {
                    notification.notify_all();
                    std::this_thread::yield(); // spin until slot is free
                }
                workRing[writeIdx] = ChunkTask{&fn, b, e, c, &remaining};
                workFlags[writeIdx].store(true, std::memory_order_release);
                notification.notify_one();
            }
            fn(begin, std::min(begin + chunkSize, end), 0);
            while (remaining.load(std::memory_order_acquire) > 0) {
                notification.notify_all();
                std::this_thread::yield(); // spin until task done
            }
            inParallel = false;
        }

        ~ThreadPool() {
            {
                std::lock_guard<std::mutex> lock(sleeper);
                isDestroyed.store(true, std::memory_order_release);
            }
            notification.notify_all();
            for (auto& t : workers) {
                if (t.joinable()) t.join();
            }
        }
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

    private:
        ThreadPool() {
            const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
            // caller thread runs chunk 0 workers handle the rest, so spawn hw-1
            const size_t workerN = hw > 1 ? hw - 1 : 0;
            workers.reserve(workerN);
            for (size_t i = 0; i < workerN; ++i) {
                workers.emplace_back([this] { workerLoop(); });
            }
        }
        void workerLoop() {
            while (true) {
                inParallel = true;
                spins = 0;
                while (spins < kMaxSpins) {
                    if (isDestroyed.load(std::memory_order_acquire)) {
                        return;
                    }
                    bool hadWork = false;
                    for (size_t readIdx{}; readIdx < kMaxWorkRingSize; ++readIdx) {
                        if (workFlags[readIdx].exchange(false, std::memory_order_acquire)) {
                            ChunkTask hold = std::move(workRing[readIdx]);
                            workRing[readIdx] = ChunkTask{};
                            (*hold.fn)(hold.begin, hold.end, hold.chunkIdx);
                            hold.remaining->fetch_sub(1, std::memory_order_release);
                            hadWork = true;
                        }
                    }
                    if (hadWork) spins = 0;
                    else spins++;
                }
                std::unique_lock<std::mutex> lock(sleeper);
                notification.wait(lock, [this] {
                    if (isDestroyed.load(std::memory_order_acquire)) return true;
                    for (auto& flag : workFlags) {
                        if (flag.load(std::memory_order_acquire)) return true;
                    }
                    return false;
                });
            }
        }

        struct ChunkTask {
            const ChunkFn* fn;
            size_t begin;
            size_t end;
            size_t chunkIdx;
            std::atomic<size_t>* remaining;
        };

        inline static thread_local bool inParallel = false;
        std::vector<std::thread> workers;
        std::array<std::atomic<bool>, kMaxWorkRingSize> workFlags{};
        std::array<ChunkTask, kMaxWorkRingSize> workRing{};
        size_t writeIdx{};
        uint8_t spins{};
        std::mutex sleeper;
        std::condition_variable notification;
        std::atomic<bool> isDestroyed{false};
    };
}
