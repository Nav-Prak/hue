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

// Parses a .glb / .gltf blob from memory. kCorruptData for anything that
// fails validation, kUnsupported for well-formed features we do not load
// yet (non-triangle topology, external buffer files).
[[nodiscard]] Result<StaticMeshData> load_gltf(const void* bytes, std::size_t size) noexcept;

// Reads the file (size-capped) and delegates to load_gltf.
[[nodiscard]] Result<StaticMeshData> load_gltf_file(const char* path) noexcept;

} // namespace hue::asset
