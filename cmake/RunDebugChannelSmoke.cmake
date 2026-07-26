# cmake/RunDebugChannelSmoke.cmake
# Drives a real game process over the debug channel TCP socket using the
# Python smoke script. Headless Linux wraps the game in Xvfb via xvfb-run;
# the Python script itself needs no display.

if(NOT DEFINED HUE_GAME OR HUE_GAME STREQUAL "")
    message(FATAL_ERROR "HUE_GAME must name the game executable")
endif()
if(NOT DEFINED HUE_PYTHON OR HUE_PYTHON STREQUAL "")
    message(FATAL_ERROR "HUE_PYTHON must name the Python interpreter")
endif()
if(NOT DEFINED HUE_SMOKE_SCRIPT OR HUE_SMOKE_SCRIPT STREQUAL "")
    message(FATAL_ERROR "HUE_SMOKE_SCRIPT must name the smoke test script")
endif()

if(WIN32 OR APPLE OR NOT "$ENV{DISPLAY}" STREQUAL "")
    set(hue_smoke_command "${HUE_PYTHON}" "${HUE_SMOKE_SCRIPT}" "${HUE_GAME}")
else()
    find_program(HUE_XVFB_RUN_EXECUTABLE xvfb-run)
    if(NOT HUE_XVFB_RUN_EXECUTABLE)
        message(FATAL_ERROR "xvfb-run is required for the debug channel smoke test on headless Linux")
    endif()
    # xvfb-run wraps the whole python+game pair so the spawned game finds a display.
    set(hue_smoke_command "${HUE_XVFB_RUN_EXECUTABLE}" -a
        "${HUE_PYTHON}" "${HUE_SMOKE_SCRIPT}" "${HUE_GAME}")
endif()

execute_process(
    COMMAND ${hue_smoke_command}
    RESULT_VARIABLE hue_smoke_result
    OUTPUT_VARIABLE hue_smoke_stdout
    ERROR_VARIABLE hue_smoke_stderr
    TIMEOUT 120
)

if(NOT hue_smoke_result EQUAL 0)
    message(FATAL_ERROR
        "Hue debug channel smoke test failed (${hue_smoke_result})\n"
        "stdout:\n${hue_smoke_stdout}\n"
        "stderr:\n${hue_smoke_stderr}")
endif()

message(STATUS "Hue debug channel smoke test passed\n${hue_smoke_stdout}")
