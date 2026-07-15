# cmake/RunWindowSmoke.cmake
# Runs Hue for one frame to prove that platform initialization and real GLFW
# window creation work. Headless Linux uses Xvfb; desktop hosts run directly.

if(NOT DEFINED HUE_GAME OR HUE_GAME STREQUAL "")
    message(FATAL_ERROR "HUE_GAME must name the game executable")
endif()

if(WIN32 OR APPLE OR NOT "$ENV{DISPLAY}" STREQUAL "")
    set(hue_smoke_command "${HUE_GAME}")
    set(hue_smoke_args --frames 1)
else()
    find_program(HUE_XVFB_RUN_EXECUTABLE xvfb-run)
    if(NOT HUE_XVFB_RUN_EXECUTABLE)
        message(FATAL_ERROR "xvfb-run is required for the window smoke test on headless Linux")
    endif()
    set(hue_smoke_command "${HUE_XVFB_RUN_EXECUTABLE}")
    set(hue_smoke_args -a "${HUE_GAME}" --frames 1)
endif()

execute_process(
    COMMAND "${hue_smoke_command}" ${hue_smoke_args}
    RESULT_VARIABLE hue_smoke_result
    OUTPUT_VARIABLE hue_smoke_stdout
    ERROR_VARIABLE hue_smoke_stderr
    TIMEOUT 10
)

if(NOT hue_smoke_result EQUAL 0)
    message(FATAL_ERROR
        "Hue window smoke test failed (${hue_smoke_result})\n"
        "stdout:\n${hue_smoke_stdout}\n"
        "stderr:\n${hue_smoke_stderr}")
endif()

message(STATUS "Hue window smoke test passed\n${hue_smoke_stderr}")
