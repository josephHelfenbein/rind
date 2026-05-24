#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {
    class ThreadPool {
    public:
        using ChunkFn = std::function<void(size_t /*beginIdx*/, size_t /*endIdx*/, size_t /*chunkIdx*/)>;

        static ThreadPool& global();

        size_t workerCount() const { return workers.size(); }

        size_t numChunks(size_t begin, size_t end, size_t minChunk) const;

        void parallel_for_chunks(size_t begin, size_t end, size_t minChunk, const ChunkFn& fn);

        ~ThreadPool();
        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

    private:
        ThreadPool();
        void workerLoop();
        void submit(std::function<void()> task);

        std::vector<std::thread> workers;
        std::deque<std::function<void()>> tasks;
        std::mutex m;
        std::condition_variable cv;
        bool running = true;
    };
}
