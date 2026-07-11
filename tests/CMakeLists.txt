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
hue_target_runtime_deps(hue_tests)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(hue_tests)
