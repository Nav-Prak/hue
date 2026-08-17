// engine/asset/include/hue/asset/gltf_loader.h
//
// glTF import (Week 5 spec). Asset files are untrusted input: the loader
// takes raw bytes, never touches the file system on the memory path (GLB
// bin chunks and base64 data URIs only -- external .bin references are
// rejected), and validates everything before it lands in StaticMeshData:
// hard caps on counts, accessor offsets/counts checked against buffer
// sizes (cgltf_validate plus our own format checks), and every index
// bounds-checked against its primitive's vertex count so the GPU can
// never be handed an out-of-range fetch. This entry point is the fuzz
// target for the first harness.

#pragma once

#include <cstddef>
#include <cstdint>

#include "hue/asset/mesh_data.h"
#include "hue/core/result.h"

namespace hue::asset {

inline constexpr std::size_t kGltfMaxFileBytes = 64u * 1024u * 1024u;
inline constexpr std::uint32_t kGltfMaxVertices = 4u * 1024u * 1024u;
inline constexpr std::uint32_t kGltfMaxIndices = 16u * 1024u * 1024u;
inline constexpr std::uint32_t kGltfMaxPrimitives = 4096u;
inline constexpr std::uint32_t kGltfMaxInstances = 4096u;

// Week 6 surface: textures/materials (PNG/JPEG via stb_image, embedded
// buffer views only) and skins/clips. All caps are hard rejects.
inline constexpr std::uint32_t kGltfMaxTextures = 64u;
inline constexpr std::uint32_t kGltfMaxTextureDim = 4096u; // also STBI_MAX_DIMENSIONS
inline constexpr std::size_t kGltfMaxDecodedTextureBytes = 128u * 1024u * 1024u; // per file
inline constexpr std::uint32_t kGltfMaxMaterials = 256u;
inline constexpr std::uint32_t kGltfMaxJoints = 256u;
inline constexpr std::uint32_t kGltfMaxClips = 64u;
inline constexpr std::uint32_t kGltfMaxChannelsPerClip = 1024u;
inline constexpr std::uint32_t kGltfMaxKeyframes = 65536u;
inline constexpr float kGltfMaxClipSeconds = 600.0f;

// Parses a .glb / .gltf blob from memory. kCorruptData for anything that
// fails validation, kUnsupported for well-formed features we do not load
// yet (non-triangle topology, external buffer files, URI-referenced
// images). This entry point is a fuzz target.
[[nodiscard]] Result<StaticMeshData> load_gltf(const void* bytes, std::size_t size) noexcept;

// Reads the file (size-capped) and delegates to load_gltf.
[[nodiscard]] Result<StaticMeshData> load_gltf_file(const char* path) noexcept;

// Parses a rigged .glb / .gltf blob: first node carrying both a mesh and a
// skin, plus every animation clip that targets its joints. Same trust
// boundary as load_gltf, with additional validation for JOINTS_0 /
// WEIGHTS_0 (joint indices bounds-checked and remapped, weights finite and
// renormalized), inverse bind matrices, and keyframe tracks (finite,
// non-decreasing times; LINEAR interpolation only). Also a fuzz target.
[[nodiscard]] Result<SkinnedMeshData> load_gltf_skinned(const void* bytes,
                                                        std::size_t size) noexcept;

// Reads the file (size-capped) and delegates to load_gltf_skinned.
[[nodiscard]] Result<SkinnedMeshData> load_gltf_skinned_file(const char* path) noexcept;

} // namespace hue::asset
