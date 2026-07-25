// tests/core/test_jobs.cpp

#include <doctest/doctest.h>

#include "hue/core/jobs.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace {

struct FlagContext {
    std::vector<std::uint8_t>* flags;
    std::atomic<std::uint32_t>* processed;
};

void mark_indices(void* user_data, std::uint32_t start, std::uint32_t end) {
    auto* context = static_cast<FlagContext*>(user_data);
    for (std::uint32_t i = start; i < end; ++i) {
        // Ranges never overlap, so a plain read-modify-write is safe.
        (*context->flags)[i] = static_cast<std::uint8_t>((*context->flags)[i] + 1);
    }
    context->processed->fetch_add(end - start, std::memory_order_relaxed);
}

struct ThreadTracker {
    std::mutex mutex;
    std::vector<std::thread::id> seen;

    void record() {
        const std::thread::id id = std::this_thread::get_id();
        std::lock_guard<std::mutex> lock(mutex);
        for (const std::thread::id& existing : seen) {
            if (existing == id) {
                return;
            }
        }
        seen.push_back(id);
    }
};

void busy_work(void* user_data, std::uint32_t start, std::uint32_t end) {
    auto* tracker = static_cast<ThreadTracker*>(user_data);
    tracker->record();
    // ~100k iterations of un-optimizable work per chunk keeps each job alive
    // long enough that sleeping workers reliably wake up and participate.
    volatile std::uint32_t sink = start + end;
    for (std::uint32_t i = 0; i < 100000; ++i) {
        sink = sink * 1664525u + 1013904223u;
    }
}

} // namespace

TEST_CASE("jobs: create picks a sane worker count") {
    auto system = hue::JobSystem::create(2);
    REQUIRE(system.has_value());
    CHECK(system.value().worker_count() == 2);

    auto defaulted = hue::JobSystem::create();
    REQUIRE(defaulted.has_value());
    CHECK(defaulted.value().worker_count() >= 1);
    CHECK(defaulted.value().worker_count() <= hue::JobSystem::kMaxWorkers);
}

TEST_CASE("jobs: parallel_for touches every index exactly once") {
    auto system_result = hue::JobSystem::create(2);
    REQUIRE(system_result.has_value());
    hue::JobSystem system = std::move(system_result).value();

    constexpr std::uint32_t kCount = 100000;
    std::vector<std::uint8_t> flags(kCount, 0);
    std::atomic<std::uint32_t> processed{0};
    FlagContext context{&flags, &processed};

    system.parallel_for(kCount, 64, &mark_indices, &context);

    CHECK(processed.load() == kCount);
    std::uint32_t touched_once = 0;
    for (std::uint8_t flag : flags) {
        touched_once += flag == 1 ? 1u : 0u;
    }
    CHECK(touched_once == kCount);
}

TEST_CASE("jobs: tiny chunks overflow the queue and still complete") {
    auto system_result = hue::JobSystem::create(2);
    REQUIRE(system_result.has_value());
    hue::JobSystem system = std::move(system_result).value();

    // 20k chunks vs. queue capacity 1024 forces the run-inline fallback path.
    constexpr std::uint32_t kCount = 20000;
    std::vector<std::uint8_t> flags(kCount, 0);
    std::atomic<std::uint32_t> processed{0};
    FlagContext context{&flags, &processed};

    system.parallel_for(kCount, 1, &mark_indices, &context);

    CHECK(processed.load() == kCount);
    std::uint32_t touched_once = 0;
    for (std::uint8_t flag : flags) {
        touched_once += flag == 1 ? 1u : 0u;
    }
    CHECK(touched_once == kCount);
}

TEST_CASE("jobs: submit and wait drain an explicit counter") {
    auto system_result = hue::JobSystem::create(2);
    REQUIRE(system_result.has_value());
    hue::JobSystem system = std::move(system_result).value();

    std::vector<std::uint8_t> flags(300, 0);
    std::atomic<std::uint32_t> processed{0};
    FlagContext context{&flags, &processed};

    hue::JobCounter counter;
    system.submit(&mark_indices, &context, 0, 100, &counter);
    system.submit(&mark_indices, &context, 100, 200, &counter);
    system.submit(&mark_indices, &context, 200, 300, &counter);
    system.wait(counter);

    CHECK(counter.done());
    CHECK(processed.load() == 300);
}

TEST_CASE("jobs: parallel_for uses multiple threads") {
    auto system_result = hue::JobSystem::create(2);
    REQUIRE(system_result.has_value());
    hue::JobSystem system = std::move(system_result).value();

    ThreadTracker tracker;
    // 128 chunks of real work: the calling thread alone would need to churn
    // through all of them, so workers always grab a share.
    system.parallel_for(128, 1, &busy_work, &tracker);

    CHECK(tracker.seen.size() >= 2);
}

namespace {

struct NestedContext {
    hue::JobSystem* system;
    std::atomic<std::uint32_t>* inner_total;
};

void count_indices(void* user_data, std::uint32_t start, std::uint32_t end) {
    auto* total = static_cast<std::atomic<std::uint32_t>*>(user_data);
    total->fetch_add(end - start, std::memory_order_relaxed);
}

void outer_job(void* user_data, std::uint32_t start, std::uint32_t end) {
    auto* context = static_cast<NestedContext*>(user_data);
    for (std::uint32_t i = start; i < end; ++i) {
        context->system->parallel_for(1000, 100, &count_indices, context->inner_total);
    }
}

} // namespace

TEST_CASE("jobs: nested parallel_for does not deadlock") {
    auto system_result = hue::JobSystem::create(2);
    REQUIRE(system_result.has_value());
    hue::JobSystem system = std::move(system_result).value();

    std::atomic<std::uint32_t> inner_total{0};
    NestedContext context{&system, &inner_total};

    // 8 outer jobs each running an inner parallel_for on the same pool.
    // Helping waits keep this from deadlocking even with only 2 workers.
    system.parallel_for(8, 1, &outer_job, &context);

    CHECK(inner_total.load() == 8u * 1000u);
}
