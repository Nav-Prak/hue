// engine/core/include/hue/core/memory.h
//
// Week 2 memory spine: tagged guarded heap allocations, frame-scoped linear
// arenas, and fixed-block pools. All allocation-size arithmetic is checked and
// the fixed snapshot feeds the later ImGui allocation overlay without allocating.

#pragma once

#include "hue/core/checked_math.h"
#include "hue/core/result.h"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace hue {

enum class MemoryTag : std::uint8_t {
    kCore,
    kFrame,
    kContainers,
    kRender,
    kAnimation,
    kPhysics,
    kAssets,
    kGame,
    kCount,
};

inline constexpr std::size_t kMemoryTagCount = static_cast<std::size_t>(MemoryTag::kCount);

struct AllocationStats {
    std::size_t current_bytes = 0;
    std::size_t peak_bytes = 0;
    std::size_t total_allocated_bytes = 0;
    std::size_t live_allocations = 0;
    std::size_t frame_requested_bytes = 0;
    std::size_t frame_allocation_count = 0;
};

struct AllocationSnapshot {
    AllocationStats tags[kMemoryTagCount];
};

[[nodiscard]] const char* memory_tag_name(MemoryTag tag) noexcept;

// Start a new overlay accounting interval. This resets only per-frame logical
// request counters; live/peak heap statistics are unaffected.
void memory_begin_frame() noexcept;
[[nodiscard]] AllocationSnapshot allocation_snapshot() noexcept;

[[nodiscard]] Result<void*> heap_allocate(std::size_t size,
                                          std::size_t alignment = alignof(std::max_align_t),
                                          MemoryTag tag = MemoryTag::kCore) noexcept;

// Canary failures return kCorruptData after the allocation is poisoned,
// untracked, and released. nullptr is a successful no-op.
[[nodiscard]] Result<void> heap_free(void* memory) noexcept;

class LinearArena {
public:
    [[nodiscard]] static Result<LinearArena> create(std::size_t capacity,
                                                    MemoryTag tag = MemoryTag::kFrame) noexcept;

    LinearArena(LinearArena&& other) noexcept;
    LinearArena(const LinearArena&) = delete;
    LinearArena& operator=(const LinearArena&) = delete;
    LinearArena& operator=(LinearArena&&) = delete;
    ~LinearArena();

    [[nodiscard]] Result<void*>
    allocate_bytes(std::size_t size, std::size_t alignment = alignof(std::max_align_t)) noexcept;

    template <typename T> [[nodiscard]] Result<T*> allocate(std::size_t count = 1) noexcept {
        std::size_t bytes = 0;
        if (!checked_mul(count, sizeof(T), bytes)) {
            return ErrorCode::kOutOfMemory;
        }
        auto allocation = allocate_bytes(bytes, alignof(T));
        if (!allocation) {
            return allocation.error();
        }
        return static_cast<T*>(allocation.value());
    }

    void reset() noexcept;

    [[nodiscard]] std::size_t used() const noexcept { return m_offset; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] std::size_t high_water_mark() const noexcept { return m_high_water_mark; }
    [[nodiscard]] std::size_t allocation_count() const noexcept { return m_allocation_count; }

private:
    LinearArena() = default;
    void release() noexcept;

    std::byte* m_buffer = nullptr;
    std::size_t m_capacity = 0;
    std::size_t m_offset = 0;
    std::size_t m_high_water_mark = 0;
    std::size_t m_allocation_count = 0;
    MemoryTag m_tag = MemoryTag::kFrame;
};

class PoolAllocator {
public:
    // Guard-page mode surrounds the entire backing region with inaccessible
    // pages; per-block canaries still detect local overruns precisely.
    [[nodiscard]] static Result<PoolAllocator>
    create(std::size_t block_size, std::size_t block_count,
           std::size_t alignment = alignof(std::max_align_t), MemoryTag tag = MemoryTag::kCore,
           bool guard_pages = false) noexcept;

    PoolAllocator(PoolAllocator&& other) noexcept;
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;
    ~PoolAllocator();

    [[nodiscard]] Result<void*> allocate() noexcept;
    [[nodiscard]] Result<void> deallocate(void* memory) noexcept;

    [[nodiscard]] std::size_t block_size() const noexcept { return m_block_size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_block_count; }
    [[nodiscard]] std::size_t available() const noexcept;
    [[nodiscard]] bool guard_pages_enabled() const noexcept { return m_guard_pages; }

private:
    PoolAllocator() = default;
    void release() noexcept;

    std::byte* m_backing = nullptr;
    std::size_t m_backing_bytes = 0;
    std::size_t m_block_size = 0;
    std::size_t m_block_count = 0;
    std::size_t m_alignment = 0;
    std::size_t m_user_offset = 0;
    std::size_t m_stride = 0;
    std::size_t m_free_count = 0;
    std::size_t* m_free_stack = nullptr;
    std::uint8_t* m_allocated = nullptr;
    void* m_guard_reservation = nullptr;
    std::size_t m_guard_reservation_size = 0;
    MemoryTag m_tag = MemoryTag::kCore;
    bool m_guard_pages = false;
    bool m_backing_from_heap = false;
    mutable std::mutex m_mutex;
};

} // namespace hue
