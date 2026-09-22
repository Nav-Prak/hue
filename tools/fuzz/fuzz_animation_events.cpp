// tools/fuzz/fuzz_animation_events.cpp
// Memory-only bounded event-sidecar parser + semantic clip validation.
#include "hue/anim/animation.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto parsed = hue::anim::parse_animation_events(data, size);
    if (parsed) {
        hue::asset::SkinnedMeshData mesh;
        hue::asset::AnimationClip clip;
        std::strcpy(clip.name, "attack");
        clip.duration = 1.0f;
        if (mesh.clips.push_back(std::move(clip))) {
            const auto validated = hue::anim::validate_animation_events(mesh, parsed.value());
            (void)validated;
        }
    }
    return 0;
}
