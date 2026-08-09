// tests/assets/test_gltf.cpp
//
// Boundary tests for the validated glTF import path. Asset bytes are
// untrusted input; every rejection case here is a trust-boundary check
// the fuzz harness will then hammer with random inputs.

#include "hue/asset/gltf_loader.h"
#include "hue/core/array.h"
#include "hue/core/memory.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

using hue::Array;
using hue::ErrorCode;
using hue::MemoryTag;
using hue::asset::kGltfMaxFileBytes;
using hue::asset::load_gltf;

// Tiny in-memory GLB builder for tests. Layout: 12-byte header, JSON chunk,
// BIN chunk. JSON is padded with spaces; BIN with zeros.
struct GlbBuilder {
    Array<std::uint8_t> bytes{MemoryTag::kAssets};

    void push_u32(std::uint32_t value) {
        std::uint8_t raw[4];
        std::memcpy(raw, &value, 4);
        for (std::uint8_t b : raw) {
            REQUIRE(bytes.push_back(b));
        }
    }

    void push_raw(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            REQUIRE(bytes.push_back(p[i]));
        }
    }

    void finish(const char* json, std::size_t json_len, const void* bin, std::uint32_t bin_len) {
        const std::uint32_t json_padded = (static_cast<std::uint32_t>(json_len) + 3u) & ~3u;
        const std::uint32_t bin_padded = (bin_len + 3u) & ~3u;
        const std::uint32_t total = 12u + 8u + json_padded + 8u + bin_padded;
        REQUIRE(bytes.reserve(total));

        push_u32(0x46546C67u); // 'glTF'
        push_u32(2u);
        push_u32(total);
        push_u32(json_padded);
        push_u32(0x4E4F534Au); // 'JSON'
        push_raw(json, json_len);
        for (std::uint32_t i = static_cast<std::uint32_t>(json_len); i < json_padded; ++i) {
            REQUIRE(bytes.push_back(static_cast<std::uint8_t>(' ')));
        }
        push_u32(bin_padded);
        push_u32(0x004E4942u); // 'BIN\0'
        if (bin_len > 0) {
            push_raw(bin, bin_len);
        }
        for (std::uint32_t i = bin_len; i < bin_padded; ++i) {
            REQUIRE(bytes.push_back(0u));
        }
        REQUIRE(bytes.size() == total);
    }
};

// One triangle: 3 float3 positions + 3 uint32 indices.
[[nodiscard]] GlbBuilder make_triangle_glb(bool out_of_range_index = false) {
    float positions[9] = {
        0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f,
    };
    std::uint32_t indices[3] = {0u, 1u, out_of_range_index ? 99u : 2u};

    std::uint8_t bin[9 * 4 + 3 * 4];
    std::memcpy(bin, positions, sizeof(positions));
    std::memcpy(bin + sizeof(positions), indices, sizeof(indices));

    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5125,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12}],"
        "\"buffers\":[{\"byteLength\":48}]}";

    GlbBuilder glb;
    glb.finish(json, std::strlen(json), bin, static_cast<std::uint32_t>(sizeof(bin)));
    return glb;
}

} // namespace

TEST_CASE("gltf: null empty and oversized input rejected") {
    CHECK(load_gltf(nullptr, 0).error() == ErrorCode::kCorruptData);
    const char empty = 0;
    CHECK(load_gltf(&empty, 0).error() == ErrorCode::kCorruptData);

    // Size check runs before any parse; no allocation of the huge blob.
    const char tiny = 'x';
    CHECK(load_gltf(&tiny, kGltfMaxFileBytes + 1).error() == ErrorCode::kCorruptData);
}

TEST_CASE("gltf: garbage bytes rejected") {
    const char garbage[] = "this is not a glTF file at all!!!!";
    CHECK(load_gltf(garbage, sizeof(garbage)).error() == ErrorCode::kCorruptData);
}

TEST_CASE("gltf: minimal triangle GLB loads") {
    const GlbBuilder glb = make_triangle_glb();
    auto mesh = load_gltf(glb.bytes.data(), glb.bytes.size());
    REQUIRE(mesh);
    CHECK(mesh.value().vertices.size() == 3);
    CHECK(mesh.value().indices.size() == 3);
    CHECK(mesh.value().primitives.size() == 1);
    CHECK(mesh.value().instances.size() == 1);
    CHECK(mesh.value().indices[0] == 0u);
    CHECK(mesh.value().indices[1] == 1u);
    CHECK(mesh.value().indices[2] == 2u);
}

TEST_CASE("gltf: out-of-range index rejected") {
    const GlbBuilder glb = make_triangle_glb(/*out_of_range_index=*/true);
    auto mesh = load_gltf(glb.bytes.data(), glb.bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kCorruptData);
}

TEST_CASE("gltf: truncated BIN chunk rejected") {
    // JSON claims 48 BIN bytes; we only supply 12 so cgltf_load_buffers
    // fails before validate (our loader surfaces that as kUnsupported).
    float positions[3] = {0.0f, 0.0f, 0.0f}; // deliberately short
    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"indices\":1}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5125,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12}],"
        "\"buffers\":[{\"byteLength\":48}]}";

    GlbBuilder glb;
    glb.finish(json, std::strlen(json), positions, static_cast<std::uint32_t>(sizeof(positions)));
    auto mesh = load_gltf(glb.bytes.data(), glb.bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kUnsupported);
}

TEST_CASE("gltf: external buffer URI rejected") {
    // Well-formed glTF JSON that points at an external .bin; memory path
    // refuses to touch the filesystem.
    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0}}]}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]}],"
        "\"bufferViews\":[{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36}],"
        "\"buffers\":[{\"byteLength\":36,\"uri\":\"evil.bin\"}]}";

    // glTF JSON-only (no BIN chunk): still a valid container for this case.
    const std::uint32_t json_len = static_cast<std::uint32_t>(std::strlen(json));
    const std::uint32_t json_padded = (json_len + 3u) & ~3u;
    const std::uint32_t total = 12u + 8u + json_padded;

    Array<std::uint8_t> bytes{MemoryTag::kAssets};
    REQUIRE(bytes.reserve(total));
    auto push_u32 = [&](std::uint32_t value) {
        std::uint8_t raw[4];
        std::memcpy(raw, &value, 4);
        for (std::uint8_t b : raw) {
            REQUIRE(bytes.push_back(b));
        }
    };
    push_u32(0x46546C67u);
    push_u32(2u);
    push_u32(total);
    push_u32(json_padded);
    push_u32(0x4E4F534Au);
    for (std::uint32_t i = 0; i < json_len; ++i) {
        REQUIRE(bytes.push_back(static_cast<std::uint8_t>(json[i])));
    }
    for (std::uint32_t i = json_len; i < json_padded; ++i) {
        REQUIRE(bytes.push_back(static_cast<std::uint8_t>(' ')));
    }

    auto mesh = load_gltf(bytes.data(), bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kUnsupported);
}

TEST_CASE("gltf: non-triangle topology rejected") {
    float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::uint32_t indices[3] = {0, 1, 2};
    std::uint8_t bin[48];
    std::memcpy(bin, positions, 36);
    std::memcpy(bin + 36, indices, 12);

    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"mode\":1,\"attributes\":{\"POSITION\":0},"
        "\"indices\":1}]}]," // mode 1 = LINES
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]},"
        "{\"bufferView\":1,\"componentType\":5125,\"count\":3,\"type\":\"SCALAR\"}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":12}],"
        "\"buffers\":[{\"byteLength\":48}]}";

    GlbBuilder glb;
    glb.finish(json, std::strlen(json), bin, 48);
    auto mesh = load_gltf(glb.bytes.data(), glb.bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kUnsupported);
}

TEST_CASE("gltf: empty scene with no drawable meshes rejected") {
    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[]}],\"nodes\":[],"
        "\"buffers\":[]}";

    GlbBuilder glb;
    glb.finish(json, std::strlen(json), nullptr, 0);
    auto mesh = load_gltf(glb.bytes.data(), glb.bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kCorruptData);
}
