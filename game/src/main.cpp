// game/src/main.cpp
//
// Entry point. Week 1 replaces this stub with the platform layer:
// GLFW window, input abstraction, fixed-timestep loop.

#include "hue/core/version.h"

#include <cstdio>

int main() {
    std::printf("%s\n", hue::engine_version_string());
    return 0;
}
