// engine/core/include/hue/core/jobs.h
//
// Job system (Week 3 spec, deliberately simplified): a fixed pool of worker
// threads pulling from one shared mutex-protected ring, plus parallel_for.
// Work-stealing deques are a stretch goal (plan item 4), not this week's
// rabbit hole. Design constraints:
//   - no exceptions/RTTI: creation returns Result, jobs are plain function
//     pointers + user data (no std::function, no per-job heap allocation)
//   - waiting threads help: wait() executes queued jobs instead of blocking,
//     so nested parallel_for cannot deadlock the pool

#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "hue/core/result.h"

namespace hue {

// A job processes the half-open index range [start, end).
using JobFunction = void (*)(void* user_data, std::uint32_t start, std::uint32_t end);

// Tracks outstanding jobs from one submission batch. Owned by the caller;
// must outlive every job that references it.
class JobCounter {
public:
    [[nodiscard]] bool done() const noexcept {
        return m_pending.load(std::memory_order_acquire) == 0;
    }

private:
    friend class JobSystem;
    std::atomic<std::uint32_t> m_pending{0};
};

class JobSystem {
public:
    // worker_count 0 picks (hardware threads - 1), clamped to [1, kMaxWorkers].
    [[nodiscard]] static Result<JobSystem> create(std::uint32_t worker_count = 0);

    JobSystem(JobSystem&& other) noexcept;
    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;
    ~JobSystem();

    // Enqueue one job for range [start, end). If the queue is full the job
    // runs inline on the calling thread (progress over backpressure errors).
    void submit(JobFunction fn, void* user_data, std::uint32_t start, std::uint32_t end,
                JobCounter* counter = nullptr);

    // Block until the counter drains, executing queued jobs while waiting.
    void wait(JobCounter& counter);

    // Split [0, count) into chunks of chunk_size, run them across the pool,
    // and block until all complete. The calling thread participates.
    void parallel_for(std::uint32_t count, std::uint32_t chunk_size, JobFunction fn,
                      void* user_data);

    [[nodiscard]] std::uint32_t worker_count() const noexcept { return m_worker_count; }

    static constexpr std::uint32_t kMaxWorkers = 32;
    static constexpr std::uint32_t kQueueCapacity = 1024; // power of two

private:
    JobSystem() = default;

    struct Job {
        JobFunction fn = nullptr;
        void* user_data = nullptr;
        std::uint32_t start = 0;
        std::uint32_t end = 0;
        JobCounter* counter = nullptr;
    };

    struct State; // defined in jobs.cpp; holds queue, mutex, condvar, threads

    [[nodiscard]] bool try_run_one_job();

    State* m_state = nullptr; // pool-independent lifetime, see jobs.cpp
    std::uint32_t m_worker_count = 0;
};

} // namespace hue
