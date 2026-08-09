# c:\ProjectHue\engine\asset\CMakeLists.txt
#
# hue_asset: asset import (Week 5). cgltf is the only parser dependency,
# pinned and compiled in one TU with warnings quarantined; everything the
# parser produces goes through validation before other modules see it.

include(FetchContent)

FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cgltf)

add_library(hue_asset STATIC
    include/hue/asset/gltf_loader.h
    include/hue/asset/mesh_data.h
    src/cgltf_impl.cpp
    src/gltf_loader.cpp
)

add_library(hue::asset ALIAS hue_asset)

target_include_directories(hue_asset PUBLIC include)
target_include_directories(hue_asset SYSTEM PRIVATE ${cgltf_SOURCE_DIR})

target_link_libraries(hue_asset
    PUBLIC
        hue::core
        hue::engine_flags
)
