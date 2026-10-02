// tools/fuzz/fuzz_shader_contract.cpp
// CPU-only semantic/reflection validator; never creates Vulkan objects.
#include "hue/render/spirv.h"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 128u * 1024u)
        return 0;
    for (const char* name :
         {"triangle.vert.spv", "triangle.frag.spv", "mesh.vert.spv", "mesh.frag.spv",
          "skinned_mesh.vert.spv", "hud.vert.spv", "hud.frag.spv"})
        (void)hue::validate_shader_contract(data, size, name);
    return 0;
}
