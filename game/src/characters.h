// game/src/characters.h
//
// Week 6 character import: loads a rigged GLB through the validated
// skinned glTF path (skeleton + inverse binds + clips), logs the import
// summary, and converts the bind pose into a static mesh so the character
// stands in the scene until GPU skinning lands in Week 7.

#pragma once

#include "hue/asset/mesh_data.h"
#include "hue/core/result.h"

// Loads `assets/models/<file_name>` relative to the executable.
[[nodiscard]] hue::Result<hue::asset::SkinnedMeshData> load_character(const char* file_name);

// Consumes the skinned data (textures/materials move over) and returns a
// drawable bind-pose preview.
[[nodiscard]] hue::Result<hue::asset::StaticMeshData>
bind_pose_preview(hue::asset::SkinnedMeshData&& skinned);
