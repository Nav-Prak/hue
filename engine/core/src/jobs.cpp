// engine/core/src/jobs.cpp

#include "hue/core/jobs.h"

#include "hue/core/memory.h"
#include "hue/core/trace.h"

#include <cassert>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <new>

namespace hue {

struct JobSystem::State {
    Job queue[kQueueCapacity];
    std::uint32_t head = 0; // next pop slot
    std::uint32_t count = 0;
    std::mutex mutex;
    std::condition_variable work_available;
    bool stop = false;
    std::thread workers[kMaxWorkers];
    std::uint32_t worker_count = 0;
};

namespace {

// Shared by workers and helping waiters; counter decrement is the completion
// signal, so it must happen after the job body (release pairs with the
// acquire load in JobCounter::done()).
void run_job(JobFunction fn, void* user_data, std::uint32_t start, std::uint32_t end,
             std::atomic<std::uint32_t>* pending) {
    {
        HUE_PROFILE_ZONE("job");
        fn(user_data, start, end);
    }
    if (pending != nullptr) {
        pending->fetch_sub(1, std::memory_order_release);
    }
}

} // namespace

Result<JobSystem> JobSystem::create(std::uint32_t worker_count) {
    if (worker_count == 0) {
        const unsigned hardware = std::thread::hardware_concurrency();
        worker_count = hardware > 1 ? static_cast<std::uint32_t>(hardware - 1) : 1u;
    }
    if (worker_count > kMaxWorkers) {
        worker_count = kMaxWorkers;
    }

    auto allocation = heap_allocate(sizeof(State), alignof(State), MemoryTag::kCore);
    if (!allocation) {
        return allocation.error();
    }
    State* state = ::new (allocation.value()) State();
    state->worker_count = worker_count;

    for (std::uint32_t i = 0; i < worker_count; ++i) {
        state->workers[i] = std::thread([state, i] {
            char thread_name[32];
            std::snprintf(thread_name, sizeof(thread_name), "hue_worker_%u", i);
            HUE_PROFILE_THREAD(thread_name);

            for (;;) {
                Job job;
                {
                    std::unique_lock<std::mutex> lock(state->mutex);
                    state->work_available.wait(
                        lock, [state] { return state->stop || state->count > 0; });
                    if (state->count == 0) {
                        return; // stop requested and queue drained
                    }
                    job = state->queue[state->head];
                    state->head = (state->head + 1) % kQueueCapacity;
                    --state->count;
                }
                run_job(job.fn, job.user_data, job.start, job.end,
                        job.counter != nullptr ? &job.counter->m_pending : nullptr);
            }
        });
    }

    JobSystem system;
    system.m_state = state;
    system.m_worker_count = worker_count;
    return system;
}

JobSystem::JobSystem(JobSystem&& other) noexcept
    : m_state(other.m_state), m_worker_count(other.m_worker_count) {
    other.m_state = nullptr;
    other.m_worker_count = 0;
}

JobSystem::~JobSystem() {
    if (m_state == nullptr) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        m_state->stop = true;
    }
    m_state->work_available.notify_all();
    for (std::uint32_t i = 0; i < m_state->worker_count; ++i) {
        if (m_state->workers[i].joinable()) {
            m_state->workers[i].join();
        }
    }
    m_state->~State();
    const auto freed = heap_free(m_state);
    assert(freed.has_value());
    (void)freed;
    m_state = nullptr;
}

void JobSystem::submit(JobFunction fn, void* user_data, std::uint32_t start, std::uint32_t end,
                       JobCounter* counter) {
    assert(m_state != nullptr && fn != nullptr);
    if (counter != nullptr) {
        counter->m_pending.fetch_add(1, std::memory_order_relaxed);
    }
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        if (m_state->count < kQueueCapacity) {
            const std::uint32_t slot = (m_state->head + m_state->count) % kQueueCapacity;
            m_state->queue[slot] = Job{fn, user_data, start, end, counter};
            ++m_state->count;
            m_state->work_available.notify_one();
            return;
        }
    }
    // Queue saturated: run inline. The caller makes progress and the pending
    // count still balances, so wait() semantics are unchanged.
    run_job(fn, user_data, start, end, counter != nullptr ? &counter->m_pending : nullptr);
}

bool JobSystem::try_run_one_job() {
    Job job;
    {
        std::lock_guard<std::mutex> lock(m_state->mutex);
        if (m_state->count == 0) {
            return false;
        }
        job = m_state->queue[m_state->head];
        m_state->head = (m_state->head + 1) % kQueueCapacity;
        --m_state->count;
    }
    run_job(job.fn, job.user_data, job.start, job.end,
            job.counter != nullptr ? &job.counter->m_pending : nullptr);
    return true;
}

void JobSystem::wait(JobCounter& counter) {
    assert(m_state != nullptr);
    while (!counter.done()) {
        if (!try_run_one_job()) {
            std::this_thread::yield();
        }
    }
}

void JobSystem::parallel_for(std::uint32_t count, std::uint32_t chunk_size, JobFunction fn,
                             void* user_data) {
    assert(m_state != nullptr && fn != nullptr);
    if (count == 0) {
        return;
    }
    if (chunk_size == 0) {
        chunk_size = 1;
    }
    JobCounter counter;
    for (std::uint32_t begin = 0; begin < count; begin += chunk_size) {
        const std::uint32_t end = begin + chunk_size < count ? begin + chunk_size : count;
        submit(fn, user_data, begin, end, &counter);
    }
    wait(counter);
}

} // namespace hue
