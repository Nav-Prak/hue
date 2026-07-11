# c:\ProjectHue\engine\core\CMakeLists.txt
add_library(hue_core STATIC
    include/hue/core/input.h
    include/hue/core/log.h
    include/hue/core/result.h
    include/hue/core/time.h
    include/hue/core/version.h
    include/hue/core/window.h
    src/input.cpp
    src/log.cpp
    src/version.cpp
    src/window.cpp
)
add_library(hue::core ALIAS hue_core)

target_include_directories(hue_core PUBLIC include)
target_link_libraries(hue_core PUBLIC hue::engine_flags)
target_compile_definitions(hue_core PUBLIC
    HUE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    HUE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    HUE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

# GLFW (windowing/input) - allowed dependency per plan
include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE) # X11 only, keeps Linux deps small

FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw GIT_TAG 3.4)
FetchContent_MakeAvailable(glfw)

# PRIVATE: GLFW types never appear in hue_core public headers
target_link_libraries(hue_core PRIVATE glfw)
