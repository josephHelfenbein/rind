#include <engine/ThreadPool.h>

#include <algorithm>
#include <atomic>

namespace {
    thread_local bool g_inParallel = false;
}

engine::ThreadPool& engine::ThreadPool::global() {
    static ThreadPool instance;
    return instance;
}

engine::ThreadPool::ThreadPool() {
    const size_t hw = std::max<size_t>(1, std::thread::hardware_concurrency());
    // Caller thread runs chunk 0
    // workers handle the rest, so spawn hw-1
    const size_t workerN = hw > 1 ? hw - 1 : 0;
    workers.reserve(workerN);
    for (size_t i = 0; i < workerN; ++i) {
        workers.emplace_back([this] { workerLoop(); });
    }
}

engine::ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(m);
        running = false;
    }
    cv.notify_all();
    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }
}

void engine::ThreadPool::workerLoop() {
    g_inParallel = true;
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [this] { return !tasks.empty() || !running; });
            if (!running && tasks.empty()) return;
            task = std::move(tasks.front());
            tasks.pop_front();
        }
        task();
    }
}

void engine::ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lk(m);
        tasks.push_back(std::move(task));
    }
    cv.notify_one();
}

size_t engine::ThreadPool::numChunks(size_t begin, size_t end, size_t minChunk) const {
    if (end <= begin) return 0;
    const size_t n = end - begin;
    const size_t maxChunks = workers.size() + 1;
    if (maxChunks <= 1 || minChunk == 0) return 1;
    const size_t byMin = (n + minChunk - 1) / minChunk;
    if (byMin <= 1) return 1;
    return std::min(byMin, maxChunks);
}

void engine::ThreadPool::parallel_for_chunks(size_t begin, size_t end, size_t minChunk, const ChunkFn& fn) {
    if (end <= begin) return;

    if (g_inParallel) {
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
    g_inParallel = true;
    for (size_t c = 1; c < chunks; ++c) {
        const size_t b = begin + c * chunkSize;
        const size_t e = std::min(b + chunkSize, end);
        submit([&fn, &remaining, b, e, c] {
            fn(b, e, c);
            remaining.fetch_sub(1, std::memory_order_release);
        });
    }
    fn(begin, std::min(begin + chunkSize, end), 0);
    while (remaining.load(std::memory_order_acquire) > 0) {
        std::this_thread::yield();
    }
    g_inParallel = false;
}
