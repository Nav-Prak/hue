// tools/fuzz/fuzz_gltf.cpp
//
// First fuzz harness (Week 5 spec): throws arbitrary bytes at the full
// validated glTF import path. load_gltf never touches the file system, so
// the only observable outcomes are a Result error or a valid mesh; crashes,
// hangs, and sanitizer reports are bugs. Seed corpus: tools/fuzz/corpus/gltf.

#include "hue/asset/gltf_loader.h"

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto result = hue::asset::load_gltf(data, size);
    (void)result;
    return 0;
}
