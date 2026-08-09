# c:\ProjectHue\game\CMakeLists.txt
include(${CMAKE_SOURCE_DIR}/cmake/HueShaders.cmake)

add_executable(hue_game
    src/fly_camera.cpp
    src/fly_camera.h
    src/main.cpp
    src/test_meshes.cpp
    src/test_meshes.h
)
target_link_libraries(hue_game PRIVATE hue::core hue::asset hue::render)
set_target_properties(hue_game PROPERTIES OUTPUT_NAME "hue")
hue_target_runtime_deps(hue_game)
hue_target_shaders(hue_game
    ${CMAKE_SOURCE_DIR}/assets/shaders/triangle.vert
    ${CMAKE_SOURCE_DIR}/assets/shaders/triangle.frag
    ${CMAKE_SOURCE_DIR}/assets/shaders/mesh.vert
    ${CMAKE_SOURCE_DIR}/assets/shaders/mesh.frag
)
