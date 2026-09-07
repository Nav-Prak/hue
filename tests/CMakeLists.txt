# c:\ProjectHue\tests\CMakeLists.txt
include(FetchContent)

FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.12
)
FetchContent_MakeAvailable(doctest)

add_executable(hue_tests
    main.cpp
    anim/test_animation.cpp
    combat/test_combat.cpp
    ecs/test_ecs.cpp
    physics/test_physics.cpp
    core/bench_math.cpp
    core/test_array.cpp
    core/test_checked_math.cpp
    core/test_debug_channel.cpp
    core/test_geometry.cpp
    core/test_hash_map.cpp
    core/test_jobs.cpp
    core/test_log.cpp
    core/test_math.cpp
    core/test_memory.cpp
    core/test_result.cpp
    core/test_spring.cpp
    core/test_string_id.cpp
    core/test_time.cpp
    render/test_spirv.cpp
    assets/test_gltf.cpp
)
# The no-exceptions flags propagate from hue_core, so run doctest in its
# supported no-exceptions mode (failed asserts terminate instead of throwing).
target_compile_definitions(hue_tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
# Week 6 loader tests read generated sample assets straight from the tree.
target_compile_definitions(hue_tests PRIVATE
    HUE_TEST_CORPUS_DIR="${CMAKE_SOURCE_DIR}/tools/fuzz/corpus/gltf"
    HUE_TEST_MODELS_DIR="${CMAKE_SOURCE_DIR}/assets/models")
target_link_libraries(hue_tests PRIVATE hue_core hue::asset hue::anim hue::combat hue::ecs hue::physics hue::render doctest::doctest hue::options)
if(MSVC)
    # VS 2026 warns on doctest 2.4.12's intentional std::tuple specialization.
    # Keep the suppression scoped to the third-party test harness consumer.
    target_compile_options(hue_tests PRIVATE /wd5285)
endif()
hue_target_runtime_deps(hue_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(hue_tests)

# Launch the real game for one frame. The wrapper uses a virtual X display on
# headless Linux and runs the executable directly on desktop hosts.
add_test(
    NAME platform_window_smoke
    COMMAND ${CMAKE_COMMAND}
        -DHUE_GAME=$<TARGET_FILE:hue_game>
        -P ${PROJECT_SOURCE_DIR}/cmake/RunWindowSmoke.cmake
)
set_tests_properties(platform_window_smoke PROPERTIES LABELS "platform;smoke" TIMEOUT 15)

# Vulkan bring-up proof: renderer must initialize and present with
# --require-renderer. Exit code 2 means "no Vulkan device here" and turns
# the test into a skip (Windows CI runners, sanitized Linux job).
add_test(
    NAME renderer_smoke
    COMMAND ${CMAKE_COMMAND}
        -DHUE_GAME=$<TARGET_FILE:hue_game>
        -P ${PROJECT_SOURCE_DIR}/cmake/RunRendererSmoke.cmake
)
set_tests_properties(renderer_smoke PROPERTIES
    LABELS "render;smoke"
    TIMEOUT 90
    SKIP_REGULAR_EXPRESSION "HUE_RENDERER_UNAVAILABLE")

# End-to-end debug channel test: Python drives a live game over TCP.
if(HUE_DEBUG_CHANNEL)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(
            NAME debug_channel_smoke
            COMMAND ${CMAKE_COMMAND}
                -DHUE_GAME=$<TARGET_FILE:hue_game>
                -DHUE_PYTHON=${Python3_EXECUTABLE}
                -DHUE_SMOKE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/debug_channel_smoke.py
                -P ${PROJECT_SOURCE_DIR}/cmake/RunDebugChannelSmoke.cmake
        )
        set_tests_properties(debug_channel_smoke PROPERTIES LABELS "platform;smoke" TIMEOUT 150)
    else()
        message(STATUS "Python3 not found: skipping debug_channel_smoke test")
    endif()
endif()
