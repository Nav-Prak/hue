// game/src/test_meshes.h
//
// Week 5 test content. Two meshes: a procedural ground-and-boxes scene
// (pure code, exercises primitives + instances) and a unit cube that
// round-trips through an in-memory GLB blob and the validated glTF
// loader -- the import path runs in the shipping loop, not only in tests.

#pragma once

#include "hue/asset/mesh_data.h"
#include "hue/core/result.h"

[[nodiscard]] hue::Result<hue::asset::StaticMeshData> build_ground_scene();
[[nodiscard]] hue::Result<hue::asset::StaticMeshData> build_gltf_cube();
