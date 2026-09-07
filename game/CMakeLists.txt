# c:\ProjectHue\game\CMakeLists.txt
include(${CMAKE_SOURCE_DIR}/cmake/HueShaders.cmake)

add_executable(hue_game
    src/characters.cpp
    src/characters.h
    src/combat_character.cpp
    src/combat_character.h
    src/fly_camera.cpp
    src/fly_camera.h
    src/follow_camera.cpp
    src/follow_camera.h
    src/main.cpp
    src/test_meshes.cpp
    src/test_meshes.h
)
target_link_libraries(hue_game
    PRIVATE hue::core hue::asset hue::anim hue::combat hue::ecs hue::physics hue::render)
set_target_properties(hue_game PROPERTIES OUTPUT_NAME "hue")
hue_target_runtime_deps(hue_game)
hue_target_shaders(hue_game
    ${CMAKE_SOURCE_DIR}/assets/shaders/triangle.vert
    ${CMAKE_SOURCE_DIR}/assets/shaders/triangle.frag
    ${CMAKE_SOURCE_DIR}/assets/shaders/mesh.vert
    ${CMAKE_SOURCE_DIR}/assets/shaders/skinned_mesh.vert
    ${CMAKE_SOURCE_DIR}/assets/shaders/mesh.frag
)

# Character GLBs stage next to the executable, mirroring shaders.
add_custom_command(TARGET hue_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/assets/models
        $<TARGET_FILE_DIR:hue_game>/assets/models
    COMMENT "Staging character models"
)
