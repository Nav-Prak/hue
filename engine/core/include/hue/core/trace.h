// engine/core/include/hue/core/trace.h
//
// Small engine-facing profiling surface. Keeping Tracy behind these macros
// makes call sites consistent and lets HUE_TRACY_ENABLE compile every zone to
// a no-op without changing engine code.

#pragma once

#include <tracy/Tracy.hpp>

#define HUE_PROFILE_ZONE(name) ZoneScopedN(name)
#define HUE_PROFILE_FRAME() FrameMark
