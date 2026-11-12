// engine/audio/src/miniaudio_impl.cpp
//
// Single TU housing miniaudio, built as an output device only. Every decoder,
// encoder, generator and the high-level engine are compiled out: sound files
// are parsed by Hue (wav.cpp), so no untrusted bytes reach third-party code.
// Third-party code compiles with warnings quarantined.

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
