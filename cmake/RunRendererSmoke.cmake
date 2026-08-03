# cmake/RunRendererSmoke.cmake
# Runs Hue with --require-renderer: the Vulkan renderer must initialize and
# present real frames. Machines with no Vulkan 1.3 device (Windows CI, the
# sanitized Linux job) exit with code 2, which the harness reports as a
# skipped test via SKIP_REGULAR_EXPRESSION rather than a failure.

if(NOT DEFINED HUE_GAME OR HUE_GAME STREQUAL "")
    message(FATAL_ERROR "HUE_GAME must name the game executable")
endif()

if(WIN32 OR APPLE OR NOT "$ENV{DISPLAY}" STREQUAL "")
    set(hue_smoke_command "${HUE_GAME}")
    set(hue_smoke_args --frames 60 --require-renderer)
else()
    find_program(HUE_XVFB_RUN_EXECUTABLE xvfb-run)
    if(NOT HUE_XVFB_RUN_EXECUTABLE)
        message(FATAL_ERROR "xvfb-run is required for the renderer smoke test on headless Linux")
    endif()
    set(hue_smoke_command "${HUE_XVFB_RUN_EXECUTABLE}")
    set(hue_smoke_args -a "${HUE_GAME}" --frames 60 --require-renderer)
endif()

execute_process(
    COMMAND "${hue_smoke_command}" ${hue_smoke_args}
    RESULT_VARIABLE hue_smoke_result
    OUTPUT_VARIABLE hue_smoke_stdout
    ERROR_VARIABLE hue_smoke_stderr
    TIMEOUT 60
)

if(hue_smoke_result EQUAL 2)
    message(STATUS "HUE_RENDERER_UNAVAILABLE: no Vulkan device on this machine, skipping")
    return()
endif()

if(NOT hue_smoke_result EQUAL 0)
    message(FATAL_ERROR
        "Hue renderer smoke test failed (${hue_smoke_result})\n"
        "stdout:\n${hue_smoke_stdout}\n"
        "stderr:\n${hue_smoke_stderr}")
endif()

set(hue_smoke_output "${hue_smoke_stdout}${hue_smoke_stderr}")
string(FIND "${hue_smoke_output}" "renderer initialized" hue_marker_position)
if(hue_marker_position EQUAL -1)
    message(FATAL_ERROR
        "Renderer smoke test exited 0 but never logged renderer initialization\n"
        "stdout:\n${hue_smoke_stdout}\n"
        "stderr:\n${hue_smoke_stderr}")
endif()

message(STATUS "Hue renderer smoke test passed\n${hue_smoke_output}")
