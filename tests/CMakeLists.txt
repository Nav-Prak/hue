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
    core/test_log.cpp
    core/test_result.cpp
    core/test_time.cpp
)
# The no-exceptions flags propagate from hue_core, so run doctest in its
# supported no-exceptions mode (failed asserts terminate instead of throwing).
target_compile_definitions(hue_tests PRIVATE DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS)
target_link_libraries(hue_tests PRIVATE hue_core doctest::doctest hue::options)
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
