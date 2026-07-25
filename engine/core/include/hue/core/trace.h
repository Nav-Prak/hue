// engine/core/include/hue/core/trace.h
//
// Small engine-facing profiling surface. Keeping Tracy behind these macros
// makes call sites consistent and lets HUE_TRACY_ENABLE compile every zone to
// a no-op without changing engine code.

#pragma once

#include <tracy/Tracy.hpp>

#define HUE_PROFILE_ZONE(name) ZoneScopedN(name)
#define HUE_PROFILE_FRAME() FrameMark

// Names the calling thread in Tracy captures (workers show up as
// "hue_worker_N" instead of anonymous thread ids). Tracy copies the string.
#ifdef TRACY_ENABLE
#include <common/TracySystem.hpp>
#define HUE_PROFILE_THREAD(name) tracy::SetThreadName(name)
#else
#define HUE_PROFILE_THREAD(name) ((void)(name))
#endif
