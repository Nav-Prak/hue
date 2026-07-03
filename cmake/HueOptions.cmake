# c:\ProjectHue\cmake\HueOptions.cmake
# Shared compile options. Two interface targets:
#   hue::options      - warnings (as errors for conversion/sign/shadow), hardening, sanitizers
#   hue::engine_flags - additionally: no exceptions, no RTTI (engine + game code)
# Tests link hue::options only, so doctest can keep exceptions.

add_library(hue_options INTERFACE)
add_library(hue::options ALIAS hue_options)

add_library(hue_engine_flags INTERFACE)
add_library(hue::engine_flags ALIAS hue_engine_flags)
target_link_libraries(hue_engine_flags INTERFACE hue_options)

if(MSVC)
    # CMake injects /EHsc by default; engine_flags replaces it with /EHs-c-.
    # Strip it here to avoid D9025 override warnings on every file.
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(hue_options INTERFACE
        /W4
        /permissive-
        /utf-8
        # warnings-as-errors: conversion / sign / shadowing
        /we4244 /we4245 /we4267 /we4305   # narrowing & sign conversions
        /we4456 /we4457 /we4458 /we4459   # shadowing
    )
    # Hardened release flags (Security Engineering Directives)
    target_compile_options(hue_options INTERFACE $<$<CONFIG:Release>:/guard:cf /GS>)
    target_link_options(hue_options INTERFACE $<$<CONFIG:Release>:/guard:cf /DYNAMICBASE>)

    if(HUE_SANITIZE)
        target_compile_options(hue_options INTERFACE /fsanitize=address /Zi)
        # ASan on MSVC is incompatible with incremental linking and edit-and-continue
        target_link_options(hue_options INTERFACE /INCREMENTAL:NO)
        # MSVC 14.38+ always links the *dynamic* ASan runtime, which lives in the
        # toolset bin dir and is only on PATH inside a VS developer prompt.
        # Stash its location so executables can copy it next to themselves.
        get_filename_component(hue_msvc_bin_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB HUE_ASAN_RUNTIME_DLLS "${hue_msvc_bin_dir}/clang_rt.asan*dynamic-x86_64.dll")
    endif()

    target_compile_options(hue_engine_flags INTERFACE /EHs-c- /GR-)
    target_compile_definitions(hue_engine_flags INTERFACE _HAS_EXCEPTIONS=0)
else()
    target_compile_options(hue_options INTERFACE
        -Wall -Wextra
        -Werror=conversion -Werror=sign-conversion -Werror=shadow
    )
    target_compile_options(hue_options INTERFACE
        $<$<CONFIG:Release>:-fstack-protector-strong>)
    target_compile_definitions(hue_options INTERFACE
        $<$<CONFIG:Release>:_FORTIFY_SOURCE=2>)
    if(NOT APPLE)
        target_link_options(hue_options INTERFACE
            $<$<CONFIG:Release>:-Wl,-z,relro,-z,now>)
    endif()

    if(HUE_SANITIZE)
        target_compile_options(hue_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -g)
        target_link_options(hue_options INTERFACE -fsanitize=address,undefined)
    endif()

    target_compile_options(hue_engine_flags INTERFACE -fno-exceptions -fno-rtti)
endif()

# Call on every executable target so sanitized builds run outside a VS dev prompt.
function(hue_target_runtime_deps target)
    if(MSVC AND HUE_SANITIZE AND HUE_ASAN_RUNTIME_DLLS)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${HUE_ASAN_RUNTIME_DLLS} $<TARGET_FILE_DIR:${target}>
            VERBATIM)
    endif()
endfunction()
