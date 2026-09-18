# Combat state machine (Week 9) + enemy brain (Week 11): pure gameplay
# logic, no allocation, no physics.
add_library(hue_combat STATIC
    include/hue/combat/combat.h
    include/hue/combat/ai.h
    src/combat.cpp
    src/ai.cpp
)

add_library(hue::combat ALIAS hue_combat)

target_include_directories(hue_combat PUBLIC include)
target_link_libraries(hue_combat PUBLIC hue::core hue::engine_flags)
