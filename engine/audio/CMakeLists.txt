# engine/audio/CMakeLists.txt
#
# hue_audio: bounded WAV parsing, a fixed-voice mixer and an output device.
# miniaudio (approved dependency) is pinned and compiled in one TU with all
# of its decoders disabled; Hue parses sound files itself.

include(FetchContent)

FetchContent_Declare(
    miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG 350784a9467a79d0fa65802132668e5afbcf3777 # 0.11.22
    # Sources only: pointing at a directory with no CMakeLists.txt keeps
    # miniaudio's own project (examples, tools, extras) out of the build.
    SOURCE_SUBDIR hue-sources-only
)
FetchContent_MakeAvailable(miniaudio)

add_library(hue_audio STATIC
    include/hue/audio/audio.h
    src/device.cpp
    src/miniaudio_impl.cpp
    src/mixer.cpp
    src/wav.cpp
)
add_library(hue::audio ALIAS hue_audio)

target_include_directories(hue_audio PUBLIC include)
target_include_directories(hue_audio SYSTEM PRIVATE ${miniaudio_SOURCE_DIR})
target_link_libraries(hue_audio PUBLIC hue::core hue::engine_flags)
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(hue_audio PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()
