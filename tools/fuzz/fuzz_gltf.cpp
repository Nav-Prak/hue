// tools/fuzz/fuzz_gltf.cpp
//
// Fuzz harness for the validated glTF import paths. Week 5 covered the
// static loader; Week 6 widened the surface with texture decode
// (stb_image) and skins/clips, so every input now runs through both entry
// points. Neither touches the file system, so the only observable
// outcomes are a Result error or valid data; crashes, hangs, and
// sanitizer reports are bugs. Seed corpus: tools/fuzz/corpus/gltf.

#include "hue/asset/gltf_loader.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto static_result = hue::asset::load_gltf(data, size);
    (void)static_result;
    const auto skinned_result = hue::asset::load_gltf_skinned(data, size);
    (void)skinned_result;
    return 0;
}
