// tests/render/test_spirv.cpp
//
// Runtime reload validates both SPIR-V semantics and the fixed engine ABI.

#include "hue/render/spirv.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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

TEST_CASE("shader contract: shipped shaders satisfy fixed engine ABI") {
    for (const char* name :
         {"triangle.vert.spv", "triangle.frag.spv", "mesh.vert.spv", "mesh.frag.spv",
          "skinned_mesh.vert.spv", "hud.vert.spv", "hud.frag.spv"}) {
        INFO(name);
        const auto path = std::string(HUE_TEST_SHADERS_DIR) + "/" + name;
        auto* f = std::fopen(path.c_str(), "rb");
        REQUIRE(f);
        std::fseek(f, 0, SEEK_END);
        const auto length = std::ftell(f);
        std::rewind(f);
        REQUIRE(length > 0);
        std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
        REQUIRE(std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size());
        std::fclose(f);
        CHECK(hue::validate_shader_contract(bytes.data(), bytes.size(), name));
        auto unaligned = bytes;
        unaligned.insert(unaligned.begin(), 0);
        CHECK(hue::validate_shader_contract(unaligned.data() + 1, bytes.size(), name));
        if (std::strcmp(name, "mesh.vert.spv") == 0) {
            // Change a descriptor binding without changing its valid SPIR-V type.
            // OpDecorate=71, Binding=33; semantic validation alone allows this.
            bool changed = false;
            for (std::size_t word = 5; word < bytes.size() / 4;) {
                std::uint32_t instruction = 0;
                std::memcpy(&instruction, bytes.data() + word * 4, 4);
                const auto count = instruction >> 16;
                REQUIRE(count > 0);
                if ((instruction & 0xFFFFu) == 71 && count == 4) {
                    std::uint32_t decoration = 0;
                    std::memcpy(&decoration, bytes.data() + (word + 2) * 4, 4);
                    if (decoration == 33) {
                        auto wrong_binding = bytes;
                        const std::uint32_t binding = 99;
                        std::memcpy(wrong_binding.data() + (word + 3) * 4, &binding, 4);
                        CHECK_FALSE(hue::validate_shader_contract(wrong_binding.data(),
                                                                  wrong_binding.size(), name));
                        changed = true;
                        break;
                    }
                }
                word += count;
            }
            CHECK(changed);
        }
        CHECK_FALSE(hue::validate_shader_contract(bytes.data(), bytes.size(), "unknown.spv"));
        CHECK_FALSE(hue::validate_shader_contract(bytes.data(), bytes.size() - 4, name));
        // A well-formed header must not be mistaken for an executable module.
        CHECK_FALSE(hue::validate_shader_contract(bytes.data(), 20, name));
        std::uint32_t bad_bound = 0xFFFFFFFFu;
        std::memcpy(bytes.data() + 12, &bad_bound, 4);
        CHECK_FALSE(hue::validate_shader_contract(bytes.data(), bytes.size(), name));
    }
}

TEST_CASE("shader contract: oversized and unaligned inputs fail safely") {
    const auto module = make_module(kMagic, 6);
    CHECK_FALSE(
        hue::validate_shader_contract(module.data(), hue::kSpirvMaxBytes + 4, "mesh.vert.spv"));
    CHECK_FALSE(hue::validate_shader_contract(module.data() + 1, 20, "mesh.vert.spv"));
}
