# c:\ProjectHue\game\CMakeLists.txt
add_executable(hue_game
    src/main.cpp
)
target_link_libraries(hue_game PRIVATE hue::core)
set_target_properties(hue_game PROPERTIES OUTPUT_NAME "hue")
hue_target_runtime_deps(hue_game)
