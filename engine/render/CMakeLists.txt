# c:\ProjectHue\engine\render\CMakeLists.txt
#
# hue_render: Vulkan renderer (Week 4). No Vulkan SDK install required:
# headers come from Khronos via FetchContent, volk loads the driver at
# runtime (no vulkan-1 link dependency), and the GLSL -> SPIR-V toolchain
# (glslang + spirv-val) builds from pinned source. Everything shares one
# SDK-aligned tag so headers, loader glue, and tools cannot drift.

include(FetchContent)

set(HUE_VULKAN_SDK_TAG "vulkan-sdk-1.4.304.0")

FetchContent_Declare(
    vulkan-headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG ${HUE_VULKAN_SDK_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG ${HUE_VULKAN_SDK_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vulkan-headers)
# volk finds headers via this hint (the fetched repo has include/ at root,
# matching the install layout volk expects).
set(VULKAN_HEADERS_INSTALL_DIR "${vulkan-headers_SOURCE_DIR}" CACHE PATH "" FORCE)
FetchContent_MakeAvailable(volk vma)

# ------------------------------------------------------------------ shader toolchain
# Host build tools only: they never link into engine binaries, so they keep
# their own defaults (exceptions restored on MSVC; Hue strips /EHsc globally).

set(SPIRV_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SPIRV_WERROR OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)          # glslang without the optimizer
set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    spirv-headers
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Headers.git
    GIT_TAG ${HUE_VULKAN_SDK_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    spirv-tools
    GIT_REPOSITORY https://github.com/KhronosGroup/SPIRV-Tools.git
    GIT_TAG ${HUE_VULKAN_SDK_TAG}
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG ${HUE_VULKAN_SDK_TAG}
    GIT_SHALLOW TRUE
)

if(MSVC)
    set(hue_saved_cxx_flags "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

FetchContent_MakeAvailable(spirv-headers)
set(SPIRV-Headers_SOURCE_DIR "${spirv-headers_SOURCE_DIR}" CACHE PATH "" FORCE)
FetchContent_MakeAvailable(spirv-tools glslang)

if(MSVC)
    set(CMAKE_CXX_FLAGS "${hue_saved_cxx_flags}")
endif()

# ------------------------------------------------------------------ hue_render

add_library(hue_render STATIC
    include/hue/render/camera.h
    include/hue/render/mesh.h
    include/hue/render/renderer.h
    include/hue/render/spirv.h
    src/vk_types.h
    src/buffer.cpp
    src/context.cpp
    src/depth.cpp
    src/descriptors.cpp
    src/mesh.cpp
    src/pipeline.cpp
    src/renderer.cpp
    src/spirv.cpp
    src/swapchain.cpp
    src/texture.cpp
    src/vma_impl.cpp
)
add_library(hue::render ALIAS hue_render)

target_include_directories(hue_render PUBLIC include)
target_link_libraries(hue_render
    PUBLIC hue::core hue::asset hue::engine_flags
    PRIVATE volk_headers GPUOpen::VulkanMemoryAllocator glfw
)

# volk as header-only (VOLK_IMPLEMENTATION in vma_impl.cpp) so the loader
# glue compiles with engine flags instead of volk's defaults.
target_compile_definitions(hue_render PRIVATE VK_NO_PROTOTYPES)
