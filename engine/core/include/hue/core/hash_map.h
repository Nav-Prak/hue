// engine/core/include/hue/core/hash_map.h
//
// Open-addressed linear-probing hash map backed by the tagged heap. Capacity
// stays a power of two and growth is fallible; tombstones preserve probe chains.

#pragma once

#include "hue/core/checked_math.h"
#include "hue/core/memory.h"
#include "hue/core/result.h"
#include "hue/core/trace.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>

namespace hue {

template <typename Key> struct DefaultHash {
    [[nodiscard]] std::size_t operator()(const Key& key) const noexcept {
        if constexpr (requires { key.hash(); }) {
            return static_cast<std::size_t>(key.hash());
        } else {
            static_assert(std::is_trivially_copyable_v<Key>,
                          "DefaultHash requires a trivially copyable key or key.hash()");
            const auto* bytes = reinterpret_cast<const std::uint8_t*>(&key);
            std::uint64_t hash = 14695981039346656037ull;
            for (std::size_t i = 0; i < sizeof(Key); ++i) {
                hash ^= bytes[i];
                hash *= 1099511628211ull;
            }
            return static_cast<std::size_t>(hash);
        }
    }
};

template <typename Key, typename Value, typename Hasher = DefaultHash<Key>> class HashMap {
    static_assert(std::is_nothrow_move_constructible_v<Key>,
                  "HashMap keys must be nothrow move constructible");
    static_assert(std::is_nothrow_move_constructible_v<Value>,
                  "HashMap values must be nothrow move constructible");

public:
    HashMap() = default;

    HashMap(HashMap&& other) noexcept
        : m_buckets(other.m_buckets), m_capacity(other.m_capacity), m_size(other.m_size),
          m_tombstones(other.m_tombstones), m_hasher(std::move(other.m_hasher)) {
        other.m_buckets = nullptr;
        other.m_capacity = 0;
        other.m_size = 0;
        other.m_tombstones = 0;
    }

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;
    HashMap& operator=(HashMap&&) = delete;

    ~HashMap() { destroy_buckets(m_buckets, m_capacity); }

    [[nodiscard]] Result<void> reserve(std::size_t desired_elements) {
        std::size_t capacity = 8;
        while (capacity - capacity / 4 < desired_elements) {
            std::size_t doubled = 0;
            if (!checked_mul(capacity, std::size_t{2}, doubled)) {
                return ErrorCode::kOutOfMemory;
            }
            capacity = doubled;
        }
        return capacity > m_capacity ? rehash(capacity) : Result<void>{};
    }

    [[nodiscard]] Result<void> insert(Key key, Value value) {
        HUE_PROFILE_ZONE("HashMap::insert");
        auto capacity_result = ensure_insert_capacity();
        if (!capacity_result) {
            return capacity_result.error();
        }
        insert_without_grow(std::move(key), std::move(value));
        return {};
    }

    [[nodiscard]] Value* find(const Key& key) noexcept {
        HUE_PROFILE_ZONE("HashMap::find");
        Bucket* bucket = find_bucket(key);
        return bucket ? bucket->value() : nullptr;
    }

    [[nodiscard]] const Value* find(const Key& key) const noexcept {
        HUE_PROFILE_ZONE("HashMap::find");
        const Bucket* bucket = find_bucket(key);
        return bucket ? bucket->value() : nullptr;
    }

    [[nodiscard]] bool contains(const Key& key) const noexcept { return find(key) != nullptr; }

    [[nodiscard]] bool erase(const Key& key) noexcept {
        HUE_PROFILE_ZONE("HashMap::erase");
        Bucket* bucket = find_bucket(key);
        if (!bucket) {
            return false;
        }
        std::destroy_at(bucket->key());
        std::destroy_at(bucket->value());
        bucket->state = BucketState::kTombstone;
        --m_size;
        ++m_tombstones;
        return true;
    }

    void clear() noexcept {
        for (std::size_t i = 0; i < m_capacity; ++i) {
            Bucket& bucket = m_buckets[i];
            if (bucket.state == BucketState::kOccupied) {
                std::destroy_at(bucket.key());
                std::destroy_at(bucket.value());
            }
            bucket.state = BucketState::kEmpty;
        }
        m_size = 0;
        m_tombstones = 0;
    }

    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

private:
    enum class BucketState : std::uint8_t { kEmpty, kOccupied, kTombstone };

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324) // intentional padding keeps raw Key/Value storage aligned
#endif
    struct Bucket {
        BucketState state = BucketState::kEmpty;
        alignas(Key) std::byte key_storage[sizeof(Key)];
        alignas(Value) std::byte value_storage[sizeof(Value)];

        Key* key() noexcept { return std::launder(reinterpret_cast<Key*>(key_storage)); }
        const Key* key() const noexcept {
            return std::launder(reinterpret_cast<const Key*>(key_storage));
        }
        Value* value() noexcept { return std::launder(reinterpret_cast<Value*>(value_storage)); }
        const Value* value() const noexcept {
            return std::launder(reinterpret_cast<const Value*>(value_storage));
        }
    };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

    [[nodiscard]] Result<void> ensure_insert_capacity() {
        if (m_capacity == 0) {
            return rehash(8);
        }
        const std::size_t threshold = m_capacity - m_capacity / 4;
        if (m_size + m_tombstones + 1 < threshold) {
            return {};
        }
        std::size_t doubled = 0;
        if (!checked_mul(m_capacity, std::size_t{2}, doubled)) {
            return ErrorCode::kOutOfMemory;
        }
        return rehash(doubled);
    }

    [[nodiscard]] Result<void> rehash(std::size_t new_capacity) {
        HUE_PROFILE_ZONE("HashMap::rehash");
        std::size_t bytes = 0;
        if (!checked_mul(new_capacity, sizeof(Bucket), bytes)) {
            return ErrorCode::kOutOfMemory;
        }
        auto allocation = heap_allocate(bytes, alignof(Bucket), MemoryTag::kContainers);
        if (!allocation) {
            return allocation.error();
        }
        auto* next = static_cast<Bucket*>(allocation.value());
        for (std::size_t i = 0; i < new_capacity; ++i) {
            std::construct_at(next + i);
        }

        Bucket* old_buckets = m_buckets;
        const std::size_t old_capacity = m_capacity;
        m_buckets = next;
        m_capacity = new_capacity;
        m_size = 0;
        m_tombstones = 0;

        for (std::size_t i = 0; i < old_capacity; ++i) {
            Bucket& old = old_buckets[i];
            if (old.state == BucketState::kOccupied) {
                insert_without_grow(std::move(*old.key()), std::move(*old.value()));
                std::destroy_at(old.key());
                std::destroy_at(old.value());
            }
            std::destroy_at(old_buckets + i);
        }
        if (old_buckets) {
            (void)heap_free(old_buckets);
        }
        return {};
    }

    void insert_without_grow(Key key, Value value) {
        const std::size_t mask = m_capacity - 1;
        std::size_t index = m_hasher(key) & mask;
        std::size_t first_tombstone = m_capacity;

        for (std::size_t probe = 0; probe < m_capacity; ++probe) {
            Bucket& bucket = m_buckets[index];
            if (bucket.state == BucketState::kOccupied) {
                if (*bucket.key() == key) {
                    std::destroy_at(bucket.value());
                    std::construct_at(bucket.value(), std::move(value));
                    return;
                }
            } else if (bucket.state == BucketState::kTombstone) {
                if (first_tombstone == m_capacity) {
                    first_tombstone = index;
                }
            } else {
                const std::size_t destination =
                    first_tombstone == m_capacity ? index : first_tombstone;
                Bucket& target = m_buckets[destination];
                if (target.state == BucketState::kTombstone) {
                    --m_tombstones;
                }
                std::construct_at(target.key(), std::move(key));
                std::construct_at(target.value(), std::move(value));
                target.state = BucketState::kOccupied;
                ++m_size;
                return;
            }
            index = (index + 1) & mask;
        }

        if (first_tombstone != m_capacity) {
            Bucket& target = m_buckets[first_tombstone];
            --m_tombstones;
            std::construct_at(target.key(), std::move(key));
            std::construct_at(target.value(), std::move(value));
            target.state = BucketState::kOccupied;
            ++m_size;
        }
    }

    Bucket* find_bucket(const Key& key) noexcept {
        return const_cast<Bucket*>(static_cast<const HashMap*>(this)->find_bucket(key));
    }

    const Bucket* find_bucket(const Key& key) const noexcept {
        if (m_capacity == 0) {
            return nullptr;
        }
        const std::size_t mask = m_capacity - 1;
        std::size_t index = m_hasher(key) & mask;
        for (std::size_t probe = 0; probe < m_capacity; ++probe) {
            const Bucket& bucket = m_buckets[index];
            if (bucket.state == BucketState::kEmpty) {
                return nullptr;
            }
            if (bucket.state == BucketState::kOccupied && *bucket.key() == key) {
                return &bucket;
            }
            index = (index + 1) & mask;
        }
        return nullptr;
    }

    void destroy_buckets(Bucket* buckets, std::size_t capacity) noexcept {
        if (!buckets) {
            return;
        }
        for (std::size_t i = 0; i < capacity; ++i) {
            Bucket& bucket = buckets[i];
            if (bucket.state == BucketState::kOccupied) {
                std::destroy_at(bucket.key());
                std::destroy_at(bucket.value());
            }
            std::destroy_at(buckets + i);
        }
        (void)heap_free(buckets);
    }

    Bucket* m_buckets = nullptr;
    std::size_t m_capacity = 0;
    std::size_t m_size = 0;
    std::size_t m_tombstones = 0;
    Hasher m_hasher;
};

} // namespace hue
