// engine/asset/include/hue/asset/mesh_data.h
//
// Validated asset-side mesh/material/skin data. Everything here has already
// passed the glTF loader's trust boundary: indices are bounds-checked,
// texture pixels are decoded RGBA8 within caps, joint hierarchies are
// topologically ordered (parent index < child index, roots are -1).

#pragma once

#include <cstdint>

#include "hue/core/array.h"
#include "hue/core/geometry.h"
#include "hue/core/math.h"
#include "hue/core/memory.h"

namespace hue::asset {

struct StaticVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
};

// Skinned vertex format (Week 6 spec): 4 joint influences, float weights
// normalized at import. GPU skinning consumes this in Week 7.
struct SkinnedVertex {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    std::uint16_t joints[4] = {};
    float weights[4] = {};
};

// Decoded image, always tightly packed RGBA8. srgb marks color textures
// (base color); data textures (metallic-roughness) stay linear.
struct TextureData {
    Array<std::uint8_t> pixels{MemoryTag::kAssets};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool srgb = false;
};

// Metallic-roughness material (glTF pbrMetallicRoughness). Texture indices
// point into the owning mesh's textures array; -1 means "none" and the
// renderer binds its 1x1 white fallback.
struct MaterialData {
    Vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic_factor = 1.0f;
    float roughness_factor = 1.0f;
    std::int32_t base_color_texture = -1;
    std::int32_t metallic_roughness_texture = -1;
};

struct MeshPrimitive {
    std::uint32_t index_count = 0;
    std::uint32_t first_index = 0;
    std::int32_t vertex_offset = 0;
    std::int32_t material_index = -1; // into materials; -1 = default
};

struct MeshInstance {
    std::uint32_t primitive_index = 0;
    Mat4 transform;
};

struct StaticMeshData {
    Array<StaticVertex> vertices{MemoryTag::kAssets};
    Array<std::uint32_t> indices{MemoryTag::kAssets};
    Array<MeshPrimitive> primitives{MemoryTag::kAssets};
    Array<MeshInstance> instances{MemoryTag::kAssets};
    Array<TextureData> textures{MemoryTag::kAssets};
    Array<MaterialData> materials{MemoryTag::kAssets};

    Aabb bounds;
};

// ------------------------------------------------------------------ skins

inline constexpr std::uint32_t kJointNameCapacity = 64;
inline constexpr std::uint32_t kClipNameCapacity = 64;

// One skeleton joint. Joints are stored topologically: parent < index for
// every non-root, so pose propagation is a single forward pass.
struct Joint {
    Mat4 inverse_bind = Mat4::identity();
    Vec3 rest_translation{};
    Quat rest_rotation{};
    Vec3 rest_scale{1.0f, 1.0f, 1.0f};
    std::int32_t parent = -1;
    char name[kJointNameCapacity] = {};
};

enum class AnimationPath : std::uint8_t {
    kTranslation, // 3 floats per key
    kRotation,    // 4 floats per key (quaternion xyzw)
    kScale,       // 3 floats per key
};

// One sampled track targeting one joint. Times are validated finite and
// non-decreasing; values hold 3 or 4 floats per key depending on path.
struct AnimationChannel {
    std::uint32_t joint = 0;
    AnimationPath path = AnimationPath::kTranslation;
    Array<float> times{MemoryTag::kAssets};
    Array<float> values{MemoryTag::kAssets};
};

struct AnimationClip {
    char name[kClipNameCapacity] = {};
    float duration = 0.0f;
    Array<AnimationChannel> channels{MemoryTag::kAssets};
};

// Everything Week 7's animation runtime needs, imported and validated in
// Week 6: skinned geometry, ordered skeleton, and sampled clips.
struct SkinnedMeshData {
    Array<SkinnedVertex> vertices{MemoryTag::kAssets};
    Array<std::uint32_t> indices{MemoryTag::kAssets};
    Array<MeshPrimitive> primitives{MemoryTag::kAssets};
    Array<TextureData> textures{MemoryTag::kAssets};
    Array<MaterialData> materials{MemoryTag::kAssets};
    Array<Joint> joints{MemoryTag::kAssets};
    Array<AnimationClip> clips{MemoryTag::kAssets};

    Aabb bounds; // bind pose
};

} // namespace hue::asset
