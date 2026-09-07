# Combat state machine (Week 9): pure gameplay logic, no allocation.
add_library(hue_combat STATIC
    include/hue/combat/combat.h
    src/combat.cpp
)

add_library(hue::combat ALIAS hue_combat)

target_include_directories(hue_combat PUBLIC include)
target_link_libraries(hue_combat PUBLIC hue::engine_flags)
