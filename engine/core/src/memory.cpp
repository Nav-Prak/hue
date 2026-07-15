// engine/core/src/memory.cpp

#include "hue/core/memory.h"

#include "hue/core/trace.h"

#include <atomic>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace hue {
namespace {

constexpr std::uint64_t kHeaderCanary = 0x4855454845414445ull; // "HUEHEADE"
constexpr std::uint64_t kTailCanary = 0x4855455441494C21ull;   // "HUETAIL!"
constexpr std::uint8_t kAllocatedPattern = 0xCD;
constexpr std::uint8_t kFreedPattern = 0xDD;

struct alignas(std::max_align_t) HeapHeader {
    void* base = nullptr;
    std::size_t requested_size = 0;
    MemoryTag tag = MemoryTag::kCore;
    std::uint64_t canary = 0;
};

struct StatsSlot {
    std::atomic<std::size_t> current_bytes{0};
    std::atomic<std::size_t> peak_bytes{0};
    std::atomic<std::size_t> total_allocated_bytes{0};
    std::atomic<std::size_t> live_allocations{0};
    std::atomic<std::size_t> frame_requested_bytes{0};
    std::atomic<std::size_t> frame_allocation_count{0};
};

StatsSlot g_stats[kMemoryTagCount];

[[nodiscard]] constexpr bool valid_tag(MemoryTag tag) noexcept {
    return static_cast<std::size_t>(tag) < kMemoryTagCount;
}

[[nodiscard]] constexpr bool valid_alignment(std::size_t alignment) noexcept {
    return alignment != 0 && (alignment & (alignment - 1)) == 0;
}

StatsSlot& stats_for(MemoryTag tag) noexcept {
    return g_stats[static_cast<std::size_t>(tag)];
}

void record_heap_allocate(MemoryTag tag, std::size_t bytes) noexcept {
    StatsSlot& stats = stats_for(tag);
    const std::size_t current =
        stats.current_bytes.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::size_t peak = stats.peak_bytes.load(std::memory_order_relaxed);
    while (current > peak &&
           !stats.peak_bytes.compare_exchange_weak(peak, current, std::memory_order_relaxed)) {
    }
    stats.total_allocated_bytes.fetch_add(bytes, std::memory_order_relaxed);
    stats.live_allocations.fetch_add(1, std::memory_order_relaxed);
    stats.frame_requested_bytes.fetch_add(bytes, std::memory_order_relaxed);
    stats.frame_allocation_count.fetch_add(1, std::memory_order_relaxed);
}

void record_heap_free(MemoryTag tag, std::size_t bytes) noexcept {
    StatsSlot& stats = stats_for(tag);
    stats.current_bytes.fetch_sub(bytes, std::memory_order_relaxed);
    stats.live_allocations.fetch_sub(1, std::memory_order_relaxed);
}

void record_logical_frame_allocation(MemoryTag tag, std::size_t bytes) noexcept {
    StatsSlot& stats = stats_for(tag);
    stats.frame_requested_bytes.fetch_add(bytes, std::memory_order_relaxed);
    stats.frame_allocation_count.fetch_add(1, std::memory_order_relaxed);
}

void write_canary(void* destination, std::uint64_t canary) noexcept {
    std::memcpy(destination, &canary, sizeof(canary));
}

[[nodiscard]] bool check_canary(const void* source, std::uint64_t expected) noexcept {
    std::uint64_t actual = 0;
    std::memcpy(&actual, source, sizeof(actual));
    return actual == expected;
}

struct GuardedRegion {
    void* reservation = nullptr;
    std::size_t reservation_size = 0;
    std::byte* data = nullptr;
    std::size_t data_size = 0;
};

[[nodiscard]] Result<GuardedRegion> allocate_guarded_region(std::size_t requested_size,
                                                            std::size_t alignment) noexcept {
    std::size_t page_size = 0;
#if defined(_WIN32)
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    page_size = static_cast<std::size_t>(info.dwPageSize);
#else
    const long queried_page_size = sysconf(_SC_PAGESIZE);
    if (queried_page_size <= 0) {
        return ErrorCode::kUnsupported;
    }
    page_size = static_cast<std::size_t>(queried_page_size);
#endif

    std::size_t data_size = 0;
    if (!checked_align_up(requested_size, page_size, data_size)) {
        return ErrorCode::kOutOfMemory;
    }
    const std::size_t region_alignment = alignment < page_size ? page_size : alignment;
    std::size_t guard_bytes = 0;
    std::size_t reservation_size = 0;
    if (!checked_mul(page_size, std::size_t{2}, guard_bytes) ||
        !checked_add(data_size, guard_bytes, reservation_size) ||
        !checked_add(reservation_size, region_alignment, reservation_size)) {
        return ErrorCode::kOutOfMemory;
    }

#if defined(_WIN32)
    void* reservation = VirtualAlloc(nullptr, reservation_size, MEM_RESERVE, PAGE_NOACCESS);
    if (!reservation) {
        return ErrorCode::kOutOfMemory;
    }
    std::size_t data_address = 0;
    const std::size_t first_data_address = reinterpret_cast<std::size_t>(reservation) + page_size;
    if (!checked_align_up(first_data_address, region_alignment, data_address)) {
        VirtualFree(reservation, 0, MEM_RELEASE);
        return ErrorCode::kOutOfMemory;
    }
    auto* data = reinterpret_cast<std::byte*>(data_address);
    if (!VirtualAlloc(data, data_size, MEM_COMMIT, PAGE_READWRITE)) {
        VirtualFree(reservation, 0, MEM_RELEASE);
        return ErrorCode::kOutOfMemory;
    }
#else
    void* reservation =
        mmap(nullptr, reservation_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (reservation == MAP_FAILED) {
        return ErrorCode::kOutOfMemory;
    }
    std::size_t data_address = 0;
    const std::size_t first_data_address = reinterpret_cast<std::size_t>(reservation) + page_size;
    if (!checked_align_up(first_data_address, region_alignment, data_address)) {
        munmap(reservation, reservation_size);
        return ErrorCode::kOutOfMemory;
    }
    auto* data = reinterpret_cast<std::byte*>(data_address);
    if (mprotect(data, data_size, PROT_READ | PROT_WRITE) != 0) {
        munmap(reservation, reservation_size);
        return ErrorCode::kOutOfMemory;
    }
#endif

    GuardedRegion region;
    region.reservation = reservation;
    region.reservation_size = reservation_size;
    region.data = data;
    region.data_size = data_size;
    return region;
}

void free_guarded_region(void* reservation, std::size_t reservation_size) noexcept {
    if (!reservation) {
        return;
    }
#if defined(_WIN32)
    (void)reservation_size;
    VirtualFree(reservation, 0, MEM_RELEASE);
#else
    munmap(reservation, reservation_size);
#endif
}

} // namespace

const char* memory_tag_name(MemoryTag tag) noexcept {
    switch (tag) {
    case MemoryTag::kCore:
        return "core";
    case MemoryTag::kFrame:
        return "frame";
    case MemoryTag::kContainers:
        return "containers";
    case MemoryTag::kRender:
        return "render";
    case MemoryTag::kAnimation:
        return "animation";
    case MemoryTag::kPhysics:
        return "physics";
    case MemoryTag::kAssets:
        return "assets";
    case MemoryTag::kGame:
        return "game";
    case MemoryTag::kCount:
        break;
    }
    return "unknown";
}

void memory_begin_frame() noexcept {
    HUE_PROFILE_ZONE("memory_begin_frame");
    for (std::size_t i = 0; i < kMemoryTagCount; ++i) {
        g_stats[i].frame_requested_bytes.store(0, std::memory_order_relaxed);
        g_stats[i].frame_allocation_count.store(0, std::memory_order_relaxed);
    }
}

AllocationSnapshot allocation_snapshot() noexcept {
    AllocationSnapshot snapshot;
    for (std::size_t i = 0; i < kMemoryTagCount; ++i) {
        const StatsSlot& source = g_stats[i];
        AllocationStats& destination = snapshot.tags[i];
        destination.current_bytes = source.current_bytes.load(std::memory_order_relaxed);
        destination.peak_bytes = source.peak_bytes.load(std::memory_order_relaxed);
        destination.total_allocated_bytes =
            source.total_allocated_bytes.load(std::memory_order_relaxed);
        destination.live_allocations = source.live_allocations.load(std::memory_order_relaxed);
        destination.frame_requested_bytes =
            source.frame_requested_bytes.load(std::memory_order_relaxed);
        destination.frame_allocation_count =
            source.frame_allocation_count.load(std::memory_order_relaxed);
    }
    return snapshot;
}

Result<void*> heap_allocate(std::size_t size, std::size_t alignment, MemoryTag tag) noexcept {
    HUE_PROFILE_ZONE("heap_allocate");
    if (size == 0 || !valid_alignment(alignment) || !valid_tag(tag)) {
        return ErrorCode::kInvalidArgument;
    }

    const std::size_t effective_alignment =
        alignment < alignof(HeapHeader) ? alignof(HeapHeader) : alignment;
    std::size_t header_and_padding = 0;
    std::size_t payload_and_tail = 0;
    std::size_t total_size = 0;
    if (!checked_add(sizeof(HeapHeader), effective_alignment - 1, header_and_padding) ||
        !checked_add(size, sizeof(kTailCanary), payload_and_tail) ||
        !checked_add(header_and_padding, payload_and_tail, total_size)) {
        return ErrorCode::kOutOfMemory;
    }

    void* base = std::malloc(total_size);
    if (!base) {
        return ErrorCode::kOutOfMemory;
    }

    const auto start = reinterpret_cast<std::uintptr_t>(base) + sizeof(HeapHeader);
    const auto aligned = (start + effective_alignment - 1) & ~(effective_alignment - 1);
    auto* user = reinterpret_cast<std::byte*>(aligned);
    auto* header = reinterpret_cast<HeapHeader*>(user - sizeof(HeapHeader));
    header->base = base;
    header->requested_size = size;
    header->tag = tag;
    header->canary = kHeaderCanary;
    write_canary(user + size, kTailCanary);
    std::memset(user, kAllocatedPattern, size);
    record_heap_allocate(tag, size);
    return static_cast<void*>(user);
}

Result<void> heap_free(void* memory) noexcept {
    HUE_PROFILE_ZONE("heap_free");
    if (!memory) {
        return {};
    }

    auto* user = static_cast<std::byte*>(memory);
    auto* header = reinterpret_cast<HeapHeader*>(user - sizeof(HeapHeader));
    if (header->canary != kHeaderCanary || !valid_tag(header->tag) || !header->base) {
        return ErrorCode::kCorruptData;
    }

    const bool tail_valid = check_canary(user + header->requested_size, kTailCanary);
    const std::size_t requested_size = header->requested_size;
    const MemoryTag tag = header->tag;
    void* base = header->base;
    std::memset(user, kFreedPattern, requested_size);
    header->canary = 0;
    record_heap_free(tag, requested_size);
    std::free(base);
    return tail_valid ? Result<void>{} : Result<void>{ErrorCode::kCorruptData};
}

Result<LinearArena> LinearArena::create(std::size_t capacity, MemoryTag tag) noexcept {
    if (capacity == 0 || !valid_tag(tag)) {
        return ErrorCode::kInvalidArgument;
    }
    auto allocation = heap_allocate(capacity, alignof(std::max_align_t), tag);
    if (!allocation) {
        return allocation.error();
    }
    LinearArena arena;
    arena.m_buffer = static_cast<std::byte*>(allocation.value());
    arena.m_capacity = capacity;
    arena.m_tag = tag;
    return arena;
}

LinearArena::LinearArena(LinearArena&& other) noexcept
    : m_buffer(other.m_buffer), m_capacity(other.m_capacity), m_offset(other.m_offset),
      m_high_water_mark(other.m_high_water_mark), m_allocation_count(other.m_allocation_count),
      m_tag(other.m_tag) {
    other.m_buffer = nullptr;
    other.m_capacity = 0;
    other.m_offset = 0;
    other.m_high_water_mark = 0;
    other.m_allocation_count = 0;
}

LinearArena::~LinearArena() {
    release();
}

Result<void*> LinearArena::allocate_bytes(std::size_t size, std::size_t alignment) noexcept {
    HUE_PROFILE_ZONE("LinearArena::allocate_bytes");
    if (size == 0 || !valid_alignment(alignment)) {
        return ErrorCode::kInvalidArgument;
    }
    const std::size_t base_address = reinterpret_cast<std::size_t>(m_buffer);
    std::size_t current_address = 0;
    std::size_t aligned_address = 0;
    std::size_t end = 0;
    if (!checked_add(base_address, m_offset, current_address) ||
        !checked_align_up(current_address, alignment, aligned_address)) {
        return ErrorCode::kOutOfMemory;
    }
    const std::size_t aligned_offset = aligned_address - base_address;
    if (!checked_add(aligned_offset, size, end)) {
        return ErrorCode::kOutOfMemory;
    }
    if (end > m_capacity) {
        return ErrorCode::kOutOfMemory;
    }
    void* result = m_buffer + aligned_offset;
    m_offset = end;
    if (m_offset > m_high_water_mark) {
        m_high_water_mark = m_offset;
    }
    ++m_allocation_count;
    record_logical_frame_allocation(m_tag, size);
    return result;
}

void LinearArena::reset() noexcept {
    HUE_PROFILE_ZONE("LinearArena::reset");
    if (m_buffer && m_offset != 0) {
        std::memset(m_buffer, kFreedPattern, m_offset);
    }
    m_offset = 0;
    m_allocation_count = 0;
}

void LinearArena::release() noexcept {
    if (m_buffer) {
        (void)heap_free(m_buffer);
        m_buffer = nullptr;
    }
}

Result<PoolAllocator> PoolAllocator::create(std::size_t block_size, std::size_t block_count,
                                            std::size_t alignment, MemoryTag tag,
                                            bool guard_pages) noexcept {
    if (block_size == 0 || block_count == 0 || !valid_alignment(alignment) || !valid_tag(tag)) {
        return ErrorCode::kInvalidArgument;
    }

    PoolAllocator pool;
    pool.m_block_size = block_size;
    pool.m_block_count = block_count;
    pool.m_alignment = alignment < alignof(std::uint64_t) ? alignof(std::uint64_t) : alignment;
    pool.m_tag = tag;
    pool.m_guard_pages = guard_pages;

    std::size_t slot_end = 0;
    if (!checked_align_up(sizeof(kHeaderCanary), pool.m_alignment, pool.m_user_offset) ||
        !checked_add(pool.m_user_offset, block_size, slot_end) ||
        !checked_add(slot_end, sizeof(kTailCanary), slot_end) ||
        !checked_align_up(slot_end, pool.m_alignment, pool.m_stride) ||
        !checked_mul(pool.m_stride, block_count, pool.m_backing_bytes)) {
        return ErrorCode::kOutOfMemory;
    }

    if (guard_pages) {
        auto region = allocate_guarded_region(pool.m_backing_bytes, pool.m_alignment);
        if (!region) {
            return region.error();
        }
        pool.m_guard_reservation = region.value().reservation;
        pool.m_guard_reservation_size = region.value().reservation_size;
        pool.m_backing = region.value().data;
        record_heap_allocate(tag, pool.m_backing_bytes);
    } else {
        auto backing = heap_allocate(pool.m_backing_bytes, pool.m_alignment, tag);
        if (!backing) {
            return backing.error();
        }
        pool.m_backing = static_cast<std::byte*>(backing.value());
        pool.m_backing_from_heap = true;
    }

    std::size_t stack_bytes = 0;
    if (!checked_mul(block_count, sizeof(std::size_t), stack_bytes)) {
        return ErrorCode::kOutOfMemory;
    }
    auto stack = heap_allocate(stack_bytes, alignof(std::size_t), tag);
    if (!stack) {
        return stack.error();
    }
    pool.m_free_stack = static_cast<std::size_t*>(stack.value());

    auto states = heap_allocate(block_count, alignof(std::uint8_t), tag);
    if (!states) {
        return states.error();
    }
    pool.m_allocated = static_cast<std::uint8_t*>(states.value());
    std::memset(pool.m_allocated, 0, block_count);
    for (std::size_t i = 0; i < block_count; ++i) {
        pool.m_free_stack[i] = block_count - 1 - i;
    }
    pool.m_free_count = block_count;
    return pool;
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : m_backing(other.m_backing), m_backing_bytes(other.m_backing_bytes),
      m_block_size(other.m_block_size), m_block_count(other.m_block_count),
      m_alignment(other.m_alignment), m_user_offset(other.m_user_offset), m_stride(other.m_stride),
      m_free_count(other.m_free_count), m_free_stack(other.m_free_stack),
      m_allocated(other.m_allocated), m_guard_reservation(other.m_guard_reservation),
      m_guard_reservation_size(other.m_guard_reservation_size), m_tag(other.m_tag),
      m_guard_pages(other.m_guard_pages), m_backing_from_heap(other.m_backing_from_heap) {
    other.m_backing = nullptr;
    other.m_backing_bytes = 0;
    other.m_block_count = 0;
    other.m_free_count = 0;
    other.m_free_stack = nullptr;
    other.m_allocated = nullptr;
    other.m_guard_reservation = nullptr;
    other.m_guard_reservation_size = 0;
    other.m_backing_from_heap = false;
}

PoolAllocator::~PoolAllocator() {
    release();
}

Result<void*> PoolAllocator::allocate() noexcept {
    HUE_PROFILE_ZONE("PoolAllocator::allocate");
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_free_count == 0) {
        return ErrorCode::kOutOfMemory;
    }
    const std::size_t index = m_free_stack[--m_free_count];
    m_allocated[index] = 1;
    std::byte* slot = m_backing + index * m_stride;
    std::byte* user = slot + m_user_offset;
    write_canary(slot, kHeaderCanary);
    write_canary(user + m_block_size, kTailCanary);
    std::memset(user, kAllocatedPattern, m_block_size);
    record_logical_frame_allocation(m_tag, m_block_size);
    return static_cast<void*>(user);
}

Result<void> PoolAllocator::deallocate(void* memory) noexcept {
    HUE_PROFILE_ZONE("PoolAllocator::deallocate");
    if (!memory) {
        return {};
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_backing) {
        return ErrorCode::kInvalidArgument;
    }

    auto* user = static_cast<std::byte*>(memory);
    std::byte* first_user = m_backing + m_user_offset;
    if (user < first_user) {
        return ErrorCode::kInvalidArgument;
    }
    const std::size_t delta = static_cast<std::size_t>(user - first_user);
    if (delta >= m_backing_bytes || delta % m_stride != 0) {
        return ErrorCode::kInvalidArgument;
    }
    const std::size_t index = delta / m_stride;
    if (index >= m_block_count || m_allocated[index] == 0) {
        return ErrorCode::kInvalidArgument;
    }

    std::byte* slot = m_backing + index * m_stride;
    const bool guards_valid =
        check_canary(slot, kHeaderCanary) && check_canary(user + m_block_size, kTailCanary);
    std::memset(user, kFreedPattern, m_block_size);
    m_allocated[index] = 0;
    m_free_stack[m_free_count++] = index;
    return guards_valid ? Result<void>{} : Result<void>{ErrorCode::kCorruptData};
}

std::size_t PoolAllocator::available() const noexcept {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_free_count;
}

void PoolAllocator::release() noexcept {
    if (m_allocated) {
        (void)heap_free(m_allocated);
        m_allocated = nullptr;
    }
    if (m_free_stack) {
        (void)heap_free(m_free_stack);
        m_free_stack = nullptr;
    }
    if (m_backing_from_heap && m_backing) {
        (void)heap_free(m_backing);
    } else if (m_guard_reservation) {
        free_guarded_region(m_guard_reservation, m_guard_reservation_size);
        record_heap_free(m_tag, m_backing_bytes);
    }
    m_backing = nullptr;
    m_guard_reservation = nullptr;
    m_guard_reservation_size = 0;
    m_free_count = 0;
}

} // namespace hue
