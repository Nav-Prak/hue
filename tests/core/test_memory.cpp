// tests/core/test_memory.cpp

#include <doctest/doctest.h>

#include "hue/core/memory.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

TEST_CASE("tagged heap: tracks aligned allocations by tag") {
    hue::memory_begin_frame();
    const auto before =
        hue::allocation_snapshot().tags[static_cast<std::size_t>(hue::MemoryTag::kGame)];

    auto allocation = hue::heap_allocate(96, 64, hue::MemoryTag::kGame);
    REQUIRE(allocation);
    CHECK(reinterpret_cast<std::uintptr_t>(allocation.value()) % 64 == 0);

    const auto during =
        hue::allocation_snapshot().tags[static_cast<std::size_t>(hue::MemoryTag::kGame)];
    CHECK(during.current_bytes == before.current_bytes + 96);
    CHECK(during.live_allocations == before.live_allocations + 1);
    CHECK(during.frame_requested_bytes == 96);
    CHECK(during.frame_allocation_count == 1);

    CHECK(hue::heap_free(allocation.value()));
    const auto after =
        hue::allocation_snapshot().tags[static_cast<std::size_t>(hue::MemoryTag::kGame)];
    CHECK(after.current_bytes == before.current_bytes);
    CHECK(after.live_allocations == before.live_allocations);
}

TEST_CASE("tagged heap: detects a deliberately corrupted tail canary") {
    auto allocation = hue::heap_allocate(16);
    REQUIRE(allocation);
    auto* bytes = static_cast<std::uint8_t*>(allocation.value());
    bytes[16] ^= 0xFF;

    auto released = hue::heap_free(bytes);
    REQUIRE_FALSE(released);
    CHECK(released.error() == hue::ErrorCode::kCorruptData);
}

TEST_CASE("linear arena: aligns, exhausts, resets, and records frame requests") {
    auto created = hue::LinearArena::create(256);
    REQUIRE(created);
    hue::LinearArena arena = std::move(created.value());

    hue::memory_begin_frame();
    auto first = arena.allocate_bytes(24, 64);
    REQUIRE(first);
    CHECK(reinterpret_cast<std::uintptr_t>(first.value()) % 64 == 0);
    CHECK(arena.used() >= 24);
    CHECK(arena.used() <= 87); // at most 63 bytes of alignment padding

    auto second = arena.allocate<std::uint32_t>(8);
    REQUIRE(second);
    CHECK(arena.allocation_count() == 2);
    CHECK(arena.high_water_mark() == arena.used());

    auto too_large = arena.allocate_bytes(1024);
    CHECK_FALSE(too_large);
    CHECK(too_large.error() == hue::ErrorCode::kOutOfMemory);

    const auto frame =
        hue::allocation_snapshot().tags[static_cast<std::size_t>(hue::MemoryTag::kFrame)];
    CHECK(frame.frame_requested_bytes == 56);
    CHECK(frame.frame_allocation_count == 2);

    arena.reset();
    CHECK(arena.used() == 0);
    CHECK(arena.allocation_count() == 0);
}

TEST_CASE("linear arena: empty frame resets perform zero heap allocations") {
    auto created = hue::LinearArena::create(1024);
    REQUIRE(created);
    hue::LinearArena arena = std::move(created.value());
    const auto before =
        hue::allocation_snapshot().tags[static_cast<std::size_t>(hue::MemoryTag::kFrame)];

    for (int frame = 0; frame < 1000; ++frame) {
        hue::memory_begin_frame();
        arena.reset();
    }

    const auto after =
        hue::allocation_snapshot().tags[static_cast<std::size_t>(hue::MemoryTag::kFrame)];
    CHECK(after.current_bytes == before.current_bytes);
    CHECK(after.live_allocations == before.live_allocations);
    CHECK(after.frame_requested_bytes == 0);
    CHECK(after.frame_allocation_count == 0);
}

TEST_CASE("pool allocator: reuses fixed blocks and poisons freed payloads") {
    auto created = hue::PoolAllocator::create(32, 4, 16);
    REQUIRE(created);
    hue::PoolAllocator pool = std::move(created.value());

    auto allocation = pool.allocate();
    REQUIRE(allocation);
    auto* bytes = static_cast<std::uint8_t*>(allocation.value());
    std::memset(bytes, 0x5A, 32);
    CHECK(pool.deallocate(bytes));
    for (std::size_t i = 0; i < 32; ++i) {
        CAPTURE(i);
        CHECK(bytes[i] == 0xDD);
    }
    CHECK(pool.available() == 4);

    auto double_free = pool.deallocate(bytes);
    REQUIRE_FALSE(double_free);
    CHECK(double_free.error() == hue::ErrorCode::kInvalidArgument);
}

TEST_CASE("pool allocator: detects a deliberately corrupted block canary") {
    auto created = hue::PoolAllocator::create(16, 2);
    REQUIRE(created);
    hue::PoolAllocator pool = std::move(created.value());
    auto allocation = pool.allocate();
    REQUIRE(allocation);
    auto* bytes = static_cast<std::uint8_t*>(allocation.value());
    bytes[16] ^= 0xFF;

    auto released = pool.deallocate(bytes);
    REQUIRE_FALSE(released);
    CHECK(released.error() == hue::ErrorCode::kCorruptData);
}

TEST_CASE("pool allocator: optional guard-page backing remains usable") {
    constexpr std::size_t kGuardedAlignment = 8192;
    auto created =
        hue::PoolAllocator::create(64, 8, kGuardedAlignment, hue::MemoryTag::kCore, true);
    REQUIRE(created);
    hue::PoolAllocator pool = std::move(created.value());
    CHECK(pool.guard_pages_enabled());
    auto allocation = pool.allocate();
    REQUIRE(allocation);
    CHECK(reinterpret_cast<std::uintptr_t>(allocation.value()) % kGuardedAlignment == 0);
    CHECK(pool.deallocate(allocation.value()));
}
