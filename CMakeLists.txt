# c:\ProjectHue\CMakeLists.txt
cmake_minimum_required(VERSION 3.28)

project(Hue
    VERSION 0.1.0
    DESCRIPTION "A from-scratch C++20/Vulkan third-person combat runtime"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(HUE_SANITIZE "Build with ASan (+UBSan on Clang)" OFF)

include(cmake/HueOptions.cmake)

add_subdirectory(engine)
add_subdirectory(game)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
