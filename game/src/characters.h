// game/src/characters.h
//
// Character asset helpers: load a rigged GLB and its bounded animation-event
// sidecar relative to the executable, independent of the process cwd.

#pragma once

#include "hue/asset/mesh_data.h"
#include "hue/anim/animation.h"
#include "hue/core/result.h"

// Loads `assets/models/<file_name>` relative to the executable.
[[nodiscard]] hue::Result<hue::asset::SkinnedMeshData> load_character(const char* file_name);

// Loads `assets/models/<file_name>` through the bounded Week 7 JSON parser.
[[nodiscard]] hue::Result<hue::anim::AnimationEventTrack>
load_character_events(const char* file_name);
