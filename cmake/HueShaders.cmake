# cmake/HueShaders.cmake
#
# GLSL -> SPIR-V toolchain (Week 4 spec). Every shader is compiled with the
# source-built glslang and then gated through spirv-val: a shader that does
# not validate fails the build, not the runtime. Compiled .spv files land in
# <target dir>/shaders/ next to the executable so the runtime loads them
# relative to its own location.

function(hue_target_shaders target)
    set(output_dir "$<TARGET_FILE_DIR:${target}>/shaders")
    set(spv_outputs "")

    foreach(shader_source IN LISTS ARGN)
        get_filename_component(shader_name "${shader_source}" NAME)
        set(spv_file "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader_name}.spv")

        add_custom_command(
            OUTPUT "${spv_file}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND $<TARGET_FILE:glslang-standalone>
                -V --target-env vulkan1.3
                -o "${spv_file}"
                "${shader_source}"
            COMMAND $<TARGET_FILE:spirv-val>
                --target-env vulkan1.3
                "${spv_file}"
            DEPENDS "${shader_source}" glslang-standalone spirv-val
            COMMENT "Compiling + validating shader ${shader_name}"
            VERBATIM
        )
        list(APPEND spv_outputs "${spv_file}")
    endforeach()

    add_custom_target(${target}_shaders DEPENDS ${spv_outputs})
    add_dependencies(${target} ${target}_shaders)

    # Stage compiled shaders next to the executable after every build.
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_BINARY_DIR}/shaders" "${output_dir}"
        COMMENT "Staging shaders for ${target}"
        VERBATIM
    )
endfunction()
