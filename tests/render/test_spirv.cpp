// tests/render/test_spirv.cpp
//
// validate_spirv_bytes guards the runtime shader.reload path, where .spv
// files on disk are untrusted input. These are boundary tests; semantic
// SPIR-V correctness is the build-time spirv-val gate.

#include "hue/render/spirv.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

constexpr std::uint32_t kMagic = 0x07230203u;

std::vector<std::uint8_t> make_module(std::uint32_t magic, std::size_t words) {
    std::vector<std::uint8_t> bytes(words * 4, 0);
    std::memcpy(bytes.data(), &magic, sizeof(magic));
    return bytes;
}

} // namespace

TEST_CASE("spirv: minimal well-formed header passes") {
    const auto module = make_module(kMagic, 5); // header is exactly 5 words
    CHECK(hue::validate_spirv_bytes(module.data(), module.size()).has_value());
}

TEST_CASE("spirv: byte-swapped magic is accepted (other-endian producer)") {
    const auto module = make_module(0x03022307u, 5);
    CHECK(hue::validate_spirv_bytes(module.data(), module.size()).has_value());
}

TEST_CASE("spirv: null and empty input rejected") {
    CHECK(!hue::validate_spirv_bytes(nullptr, 20).has_value());
    const auto module = make_module(kMagic, 5);
    CHECK(!hue::validate_spirv_bytes(module.data(), 0).has_value());
}

TEST_CASE("spirv: size must be a multiple of four") {
    const auto module = make_module(kMagic, 6);
    CHECK(!hue::validate_spirv_bytes(module.data(), module.size() - 1).has_value());
}

TEST_CASE("spirv: truncated header rejected") {
    const auto module = make_module(kMagic, 4); // one word short of a header
    CHECK(!hue::validate_spirv_bytes(module.data(), module.size()).has_value());
}

TEST_CASE("spirv: oversized module rejected") {
    // No allocation needed: the size check runs before any read past word 0.
    const auto module = make_module(kMagic, 5);
    CHECK(!hue::validate_spirv_bytes(module.data(), hue::kSpirvMaxBytes + 4).has_value());
}

TEST_CASE("spirv: wrong magic rejected") {
    const auto module = make_module(0xDEADBEEFu, 5);
    CHECK(!hue::validate_spirv_bytes(module.data(), module.size()).has_value());
}
