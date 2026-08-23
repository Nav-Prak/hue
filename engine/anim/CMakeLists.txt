add_library(hue_anim STATIC
    include/hue/anim/animation.h
    src/animation.cpp
)

add_library(hue::anim ALIAS hue_anim)

target_include_directories(hue_anim PUBLIC include)
target_link_libraries(hue_anim PUBLIC hue::core hue::asset hue::engine_flags)
