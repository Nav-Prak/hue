# cmake/RunEncounterSmoke.cmake
# Runs real gameplay/render submission where available; no-Vulkan remains a
# gameplay-only run and is explicitly reported, not a rendering acceptance.
if(UNIX AND NOT APPLE)
    find_program(XVFB_RUN xvfb-run REQUIRED)
    set(command ${XVFB_RUN} -a ${HUE_GAME} --encounter-smoke)
else()
    set(command ${HUE_GAME} --encounter-smoke)
endif()
execute_process(COMMAND ${command} RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 170)
message("${output}${errors}")
if(NOT result EQUAL 0 OR NOT "${output}${errors}" MATCHES "encounter_smoke PASS")
    message(FATAL_ERROR "Encounter smoke failed: ${result}")
endif()
