# c:\ProjectHue\engine\asset\CMakeLists.txt
#
# hue_asset: asset import (Weeks 5-6). Two parser dependencies, both pinned
# and compiled in single TUs with warnings quarantined: cgltf for glTF
# structure, stb_image for PNG/JPEG texture payloads. Everything the
# parsers produce goes through validation before other modules see it.

include(FetchContent)

FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG v1.14
    GIT_SHALLOW TRUE
)
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG 5c205738c191bcb0abc65c4febfa9bd25ff35234
)
FetchContent_MakeAvailable(cgltf stb)

add_library(hue_asset STATIC
    include/hue/asset/gltf_loader.h
    include/hue/asset/mesh_data.h
    src/cgltf_impl.cpp
    src/gltf_loader.cpp
    src/stb_impl.cpp
)

add_library(hue::asset ALIAS hue_asset)

target_include_directories(hue_asset PUBLIC include)
target_include_directories(hue_asset SYSTEM PRIVATE ${cgltf_SOURCE_DIR} ${stb_SOURCE_DIR})

target_link_libraries(hue_asset
    PUBLIC
        hue::core
        hue::engine_flags
)
