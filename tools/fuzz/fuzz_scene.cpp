// tools/fuzz/fuzz_scene.cpp
#include "scene_config.h"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    auto parsed = parse_scene(data, size);
    if (parsed && !valid_scene(parsed.value()))
        std::abort();
    return 0;
}
