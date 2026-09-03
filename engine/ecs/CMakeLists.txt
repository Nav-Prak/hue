# Header-only sparse-set ECS (Week 8).
add_library(hue_ecs INTERFACE)
add_library(hue::ecs ALIAS hue_ecs)

target_include_directories(hue_ecs INTERFACE include)
target_link_libraries(hue_ecs INTERFACE hue::core)
