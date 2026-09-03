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
#include <cstdio>
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

// ------------------------------------------------------------- Week 6
//
// Texture/material and skin/clip coverage. Sample GLBs come from the
// checked-in generator output (tools/assets/generate_characters.py), read
// straight from the source tree; rejection cases build corrupt containers
// in place.

namespace {

[[nodiscard]] std::vector<std::uint8_t> read_sample(const char* directory, const char* name) {
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s", directory, name);
    std::FILE* file = std::fopen(path, "rb");
    REQUIRE_MESSAGE(file != nullptr, path);
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    REQUIRE(size > 0);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    REQUIRE(std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size());
    std::fclose(file);
    return bytes;
}

} // namespace

TEST_CASE("gltf: textured triangle decodes PNG and material factors") {
    const auto bytes = read_sample(HUE_TEST_CORPUS_DIR, "textured_min.glb");
    auto mesh = load_gltf(bytes.data(), bytes.size());
    REQUIRE(mesh);

    REQUIRE(mesh.value().textures.size() == 1);
    const hue::asset::TextureData& texture = mesh.value().textures[0];
    CHECK(texture.width == 4);
    CHECK(texture.height == 4);
    CHECK(texture.pixels.size() == 4u * 4u * 4u);
    CHECK(texture.srgb); // referenced as base color -> sRGB

    REQUIRE(mesh.value().materials.size() == 1);
    const hue::asset::MaterialData& material = mesh.value().materials[0];
    CHECK(material.base_color_texture == 0);
    CHECK(material.metallic_roughness_texture == -1);
    CHECK(material.metallic_factor == doctest::Approx(0.5f));
    CHECK(material.roughness_factor == doctest::Approx(0.5f));

    REQUIRE(mesh.value().primitives.size() == 1);
    CHECK(mesh.value().primitives[0].material_index == 0);
}

TEST_CASE("gltf: corrupt image bytes rejected") {
    // Material references a texture whose buffer view holds garbage
    // instead of PNG/JPEG bytes; stb_image must fail cleanly.
    std::uint8_t bin[64];
    std::memset(bin, 0xAB, sizeof(bin));
    float positions[9] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    std::uint8_t full_bin[36 + 64];
    std::memcpy(full_bin, positions, 36);
    std::memcpy(full_bin + 36, bin, 64);

    const char* json =
        "{\"asset\":{\"version\":\"2.0\"},"
        "\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0}],"
        "\"meshes\":[{\"primitives\":[{\"attributes\":{\"POSITION\":0},\"material\":0}]}],"
        "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],"
        "\"textures\":[{\"source\":0}],"
        "\"images\":[{\"bufferView\":1,\"mimeType\":\"image/png\"}],"
        "\"accessors\":["
        "{\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\","
        "\"min\":[0,0,0],\"max\":[1,1,0]}],"
        "\"bufferViews\":["
        "{\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},"
        "{\"buffer\":0,\"byteOffset\":36,\"byteLength\":64}],"
        "\"buffers\":[{\"byteLength\":100}]}";

    GlbBuilder glb;
    glb.finish(json, std::strlen(json), full_bin, static_cast<std::uint32_t>(sizeof(full_bin)));
    auto mesh = load_gltf(glb.bytes.data(), glb.bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kCorruptData);
}

TEST_CASE("gltf skinned: minimal skinned triangle loads") {
    const auto bytes = read_sample(HUE_TEST_CORPUS_DIR, "skinned_min.glb");
    auto mesh = hue::asset::load_gltf_skinned(bytes.data(), bytes.size());
    REQUIRE(mesh);

    // Skeleton: two joints, topologically ordered (root first).
    REQUIRE(mesh.value().joints.size() == 2);
    CHECK(mesh.value().joints[0].parent == -1);
    CHECK(mesh.value().joints[1].parent == 0);

    // Geometry: weights normalized, joint indices in range.
    REQUIRE(mesh.value().vertices.size() == 3);
    for (std::size_t v = 0; v < 3; ++v) {
        const hue::asset::SkinnedVertex& vertex = mesh.value().vertices[v];
        float sum = 0.0f;
        for (float weight : vertex.weights) {
            sum += weight;
        }
        CHECK(sum == doctest::Approx(1.0f));
        for (std::uint16_t joint : vertex.joints) {
            CHECK(joint < 2);
        }
    }

    // Clip: one rotation channel targeting the tip joint.
    REQUIRE(mesh.value().clips.size() == 1);
    const hue::asset::AnimationClip& clip = mesh.value().clips[0];
    CHECK(std::strcmp(clip.name, "wave") == 0);
    CHECK(clip.duration == doctest::Approx(1.0f));
    REQUIRE(clip.channels.size() == 1);
    CHECK(clip.channels[0].path == hue::asset::AnimationPath::kRotation);
    CHECK(clip.channels[0].joint == 1);
    CHECK(clip.channels[0].times.size() == 2);
    CHECK(clip.channels[0].values.size() == 8); // 2 keys * 4 components
}

TEST_CASE("gltf skinned: out-of-range joint index rejected") {
    const auto bytes = read_sample(HUE_TEST_CORPUS_DIR, "skinned_bad_joint.glb");
    auto mesh = hue::asset::load_gltf_skinned(bytes.data(), bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kCorruptData);
}

TEST_CASE("gltf skinned: character imports with skeleton clips and texture") {
    const auto bytes = read_sample(HUE_TEST_MODELS_DIR, "player.glb");
    auto mesh = hue::asset::load_gltf_skinned(bytes.data(), bytes.size());
    REQUIRE(mesh);

    CHECK(mesh.value().joints.size() == 19);
    CHECK(mesh.value().joints[0].parent == -1); // hips is the only root
    for (std::size_t j = 1; j < mesh.value().joints.size(); ++j) {
        CHECK(mesh.value().joints[j].parent >= 0);
        CHECK(mesh.value().joints[j].parent < static_cast<std::int32_t>(j));
    }

    // Smooth skinning: the rig must carry genuinely blended vertices (two
    // influences), not only rigid single-joint binds.
    std::size_t blended_vertices = 0;
    for (std::size_t v = 0; v < mesh.value().vertices.size(); ++v) {
        const hue::asset::SkinnedVertex& vertex = mesh.value().vertices[v];
        std::uint32_t influences = 0;
        for (float weight : vertex.weights) {
            if (weight > 0.0f) {
                ++influences;
            }
        }
        if (influences >= 2) {
            ++blended_vertices;
        }
    }
    CHECK(blended_vertices > 0);

    REQUIRE(mesh.value().clips.size() == 7);
    const char* expected_clips[7] = {"locomotion", "attack", "dodge", "hit_react",
                                     "death",      "idle",   "walk"};
    for (std::size_t c = 0; c < 7; ++c) {
        CHECK(std::strcmp(mesh.value().clips[c].name, expected_clips[c]) == 0);
        CHECK(mesh.value().clips[c].duration > 0.0f);
        CHECK(mesh.value().clips[c].channels.size() > 0);
    }

    REQUIRE(mesh.value().textures.size() == 1);
    CHECK(mesh.value().textures[0].width == 64);
    CHECK(mesh.value().textures[0].srgb);
    REQUIRE(mesh.value().materials.size() == 1);
    CHECK(mesh.value().materials[0].base_color_texture == 0);
}

TEST_CASE("gltf skinned: static-only file rejected by skinned path") {
    const GlbBuilder glb = make_triangle_glb();
    auto mesh = hue::asset::load_gltf_skinned(glb.bytes.data(), glb.bytes.size());
    REQUIRE(!mesh);
    CHECK(mesh.error() == ErrorCode::kCorruptData);
}
