// engine/asset/src/cgltf_impl.cpp
//
// Single TU housing the cgltf implementation. Third-party code compiles
// with warnings quarantined; our strict flags stay on for the loader that
// wraps it.

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
