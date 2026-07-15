// engine/core/include/hue/core/array.h
//
// Contiguous engine array backed by the tagged heap. Growth is explicit and
// fallible; indexed access asserts in Debug/Sanitized builds and at() is the
// always-checked boundary API.

#pragma once

#include "hue/core/checked_math.h"
#include "hue/core/memory.h"
#include "hue/core/result.h"
#include "hue/core/trace.h"

#include <cassert>
#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

namespace hue {

template <typename T> class Array {
    static_assert(std::is_nothrow_move_constructible_v<T>,
                  "Array elements must be nothrow move constructible");

public:
    Array() = default;

    explicit Array(MemoryTag tag) : m_tag(tag) {}

    Array(Array&& other) noexcept
        : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity),
          m_tag(other.m_tag) {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;
    Array& operator=(Array&&) = delete;

    ~Array() {
        clear();
        if (m_data) {
            (void)heap_free(m_data);
        }
    }

    [[nodiscard]] Result<void> reserve(std::size_t requested_capacity) {
        HUE_PROFILE_ZONE("Array::reserve");
        if (requested_capacity <= m_capacity) {
            return {};
        }

        std::size_t bytes = 0;
        if (!checked_mul(requested_capacity, sizeof(T), bytes)) {
            return ErrorCode::kOutOfMemory;
        }
        auto allocation = heap_allocate(bytes, alignof(T), m_tag);
        if (!allocation) {
            return allocation.error();
        }
        T* next = static_cast<T*>(allocation.value());

        for (std::size_t i = 0; i < m_size; ++i) {
            std::construct_at(next + i, std::move(m_data[i]));
            std::destroy_at(m_data + i);
        }
        if (m_data) {
            (void)heap_free(m_data);
        }
        m_data = next;
        m_capacity = requested_capacity;
        return {};
    }

    [[nodiscard]] Result<void> push_back(const T& value) {
        HUE_PROFILE_ZONE("Array::push_back");
        auto grown = grow_for_one();
        if (!grown) {
            return grown.error();
        }
        std::construct_at(m_data + m_size, value);
        ++m_size;
        return {};
    }

    [[nodiscard]] Result<void> push_back(T&& value) {
        HUE_PROFILE_ZONE("Array::push_back");
        auto grown = grow_for_one();
        if (!grown) {
            return grown.error();
        }
        std::construct_at(m_data + m_size, std::move(value));
        ++m_size;
        return {};
    }

    [[nodiscard]] bool pop_back() noexcept {
        if (m_size == 0) {
            return false;
        }
        --m_size;
        std::destroy_at(m_data + m_size);
        return true;
    }

    void clear() noexcept {
        for (std::size_t i = 0; i < m_size; ++i) {
            std::destroy_at(m_data + i);
        }
        m_size = 0;
    }

    [[nodiscard]] T* at(std::size_t index) noexcept {
        return index < m_size ? m_data + index : nullptr;
    }

    [[nodiscard]] const T* at(std::size_t index) const noexcept {
        return index < m_size ? m_data + index : nullptr;
    }

    T& operator[](std::size_t index) noexcept {
        assert(index < m_size && "Array index out of bounds");
        return m_data[index];
    }

    const T& operator[](std::size_t index) const noexcept {
        assert(index < m_size && "Array index out of bounds");
        return m_data[index];
    }

    [[nodiscard]] T* data() noexcept { return m_data; }
    [[nodiscard]] const T* data() const noexcept { return m_data; }
    [[nodiscard]] std::size_t size() const noexcept { return m_size; }
    [[nodiscard]] std::size_t capacity() const noexcept { return m_capacity; }
    [[nodiscard]] bool empty() const noexcept { return m_size == 0; }

private:
    [[nodiscard]] Result<void> grow_for_one() {
        if (m_size < m_capacity) {
            return {};
        }
        std::size_t next_capacity = 8;
        if (m_capacity != 0 && !checked_mul(m_capacity, std::size_t{2}, next_capacity)) {
            return ErrorCode::kOutOfMemory;
        }
        return reserve(next_capacity);
    }

    T* m_data = nullptr;
    std::size_t m_size = 0;
    std::size_t m_capacity = 0;
    MemoryTag m_tag = MemoryTag::kContainers;
};

} // namespace hue
