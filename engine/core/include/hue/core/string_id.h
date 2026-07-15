// engine/core/include/hue/core/string_id.h
//
// Allocation-free 64-bit FNV-1a identifiers for stable engine names. The
// original strings remain content/debug data; runtime systems store StringId.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace hue {

class StringId {
public:
    constexpr StringId() = default;
    explicit constexpr StringId(std::uint64_t value) : m_value(value) {}

    [[nodiscard]] static constexpr StringId from_string(std::string_view text) noexcept {
        std::uint64_t hash = kOffsetBasis;
        for (const char c : text) {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= kPrime;
        }
        return StringId(hash);
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept { return m_value; }
    [[nodiscard]] constexpr std::uint64_t hash() const noexcept { return m_value; }
    explicit constexpr operator bool() const noexcept { return m_value != 0; }

    friend constexpr bool operator==(StringId left, StringId right) noexcept {
        return left.m_value == right.m_value;
    }

private:
    static constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    static constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t m_value = 0;
};

namespace literals {

[[nodiscard]] constexpr StringId operator""_sid(const char* text, std::size_t length) noexcept {
    return StringId::from_string(std::string_view(text, length));
}

} // namespace literals
} // namespace hue
