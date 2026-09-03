# Week 8 physics: Jolt for static arena collision, the capsule character
# controller, and camera collision probes. Jolt builds from source via
# FetchContent, pinned to a release tag, with exceptions/RTTI off (its
# default) to match engine constraints.

include(FetchContent)

set(JPH_BUILD_SHARED_LIBS OFF)

# Keep Jolt on the same SSE4.2 baseline as the engine's own SIMD.
set(USE_SSE4_1 ON CACHE BOOL "" FORCE)
set(USE_SSE4_2 ON CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)

# Respect the parent toolchain: no flag overrides, dynamic MSVC runtime to
# match every other target, no LTO (build time), no samples/tests/viewers.
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)

# Trim subsystems the engine does not use.
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    joltphysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG v5.6.0
    SOURCE_SUBDIR Build
)
FetchContent_MakeAvailable(joltphysics)

if(MSVC AND HUE_SANITIZE)
    # MSVC ASan annotates STL containers and the linker requires every
    # object in the image to agree (LNK2038 annotate_* mismatch), so the
    # sanitized config instruments Jolt too.
    target_compile_options(Jolt PRIVATE /fsanitize=address)
endif()

add_library(hue_physics STATIC
    include/hue/physics/physics.h
    src/physics.cpp
)

add_library(hue::physics ALIAS hue_physics)

target_include_directories(hue_physics PUBLIC include)
target_link_libraries(hue_physics
    PUBLIC hue::core hue::engine_flags
    PRIVATE Jolt
)
