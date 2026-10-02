# c:\ProjectHue\tools\fuzz\CMakeLists.txt
#
# libFuzzer harnesses (Clang only). Configure with HUE_FUZZ=ON on top of
# the sanitized preset so ASan/UBSan catch what the fuzzer shakes loose;
# hue::options adds -fsanitize=fuzzer-no-link to every engine library for
# coverage feedback, the harness links the fuzzer driver.

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "HUE_FUZZ requires Clang (libFuzzer)")
endif()

add_executable(fuzz_gltf fuzz_gltf.cpp)
target_link_libraries(fuzz_gltf PRIVATE hue::asset hue::options)
target_compile_options(fuzz_gltf PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_gltf PRIVATE -fsanitize=fuzzer)

add_executable(fuzz_animation_events fuzz_animation_events.cpp)
target_link_libraries(fuzz_animation_events PRIVATE hue::anim hue::options)
target_compile_options(fuzz_animation_events PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_animation_events PRIVATE -fsanitize=fuzzer)

add_executable(fuzz_scene fuzz_scene.cpp ${PROJECT_SOURCE_DIR}/game/src/config_json.cpp ${PROJECT_SOURCE_DIR}/game/src/scene_config.cpp)
target_include_directories(fuzz_scene PRIVATE ${PROJECT_SOURCE_DIR}/game/src)
target_link_libraries(fuzz_scene PRIVATE hue::core hue::options)
target_compile_options(fuzz_scene PRIVATE -fsanitize=fuzzer)
target_link_options(fuzz_scene PRIVATE -fsanitize=fuzzer)
