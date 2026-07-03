# c:\ProjectHue\engine\core\CMakeLists.txt
add_library(hue_core STATIC
    include/hue/core/result.h
    include/hue/core/version.h
    src/version.cpp
)
add_library(hue::core ALIAS hue_core)

target_include_directories(hue_core PUBLIC include)
target_link_libraries(hue_core PUBLIC hue::engine_flags)
target_compile_definitions(hue_core PUBLIC
    HUE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    HUE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    HUE_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
