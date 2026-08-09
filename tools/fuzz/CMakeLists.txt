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
