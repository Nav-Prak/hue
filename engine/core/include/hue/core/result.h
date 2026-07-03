// engine/core/include/hue/core/result.h
//
// Result<T, E>: engine-wide error propagation without exceptions.
// Engine code returns Result instead of throwing (AI Directive 5: no
// exceptions, no RTTI). Deliberately minimal: value-or-error union with
// explicit checks; no monadic combinators until a real call site wants one.

#pragma once

#include <cassert>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace hue {

// Default error type. Subsystems may define richer error enums and use
// Result<T, TheirError>; values here cover cross-cutting failures.
enum class ErrorCode : std::uint32_t {
    kUnknown = 1,
    kInvalidArgument,
    kOutOfMemory,
    kNotFound,
    kUnsupported,
    kCorruptData, // untrusted input failed validation
};

template <typename T, typename E = ErrorCode>
class [[nodiscard]] Result {
    static_assert(!std::is_same_v<T, E>, "value and error types must differ");

public:
    Result(T value) : m_has_value(true) { ::new (&m_storage.value) T(std::move(value)); }
    Result(E error) : m_has_value(false) { ::new (&m_storage.error) E(std::move(error)); }

    Result(Result&& other) noexcept : m_has_value(other.m_has_value) {
        if (m_has_value) {
            ::new (&m_storage.value) T(std::move(other.m_storage.value));
        } else {
            ::new (&m_storage.error) E(std::move(other.m_storage.error));
        }
    }

    Result(const Result&) = delete;
    Result& operator=(const Result&) = delete;
    Result& operator=(Result&&) = delete;

    ~Result() {
        if (m_has_value) {
            m_storage.value.~T();
        } else {
            m_storage.error.~E();
        }
    }

    bool has_value() const { return m_has_value; }
    explicit operator bool() const { return m_has_value; }

    T& value() & {
        assert(m_has_value);
        return m_storage.value;
    }
    const T& value() const& {
        assert(m_has_value);
        return m_storage.value;
    }
    T&& value() && {
        assert(m_has_value);
        return std::move(m_storage.value);
    }

    const E& error() const {
        assert(!m_has_value);
        return m_storage.error;
    }

    T value_or(T fallback) const& { return m_has_value ? m_storage.value : std::move(fallback); }

private:
    union Storage {
        Storage() {}
        ~Storage() {}
        T value;
        E error;
    } m_storage;
    bool m_has_value;
};

// Result<void, E>: success/failure with no payload.
template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    Result() : m_has_value(true) {}
    Result(E error) : m_has_value(false), m_error(std::move(error)) {}

    bool has_value() const { return m_has_value; }
    explicit operator bool() const { return m_has_value; }

    const E& error() const {
        assert(!m_has_value);
        return m_error;
    }

private:
    bool m_has_value;
    E m_error{};
};

} // namespace hue
