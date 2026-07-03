// engine/core/src/version.cpp

#include "hue/core/version.h"

namespace hue {

namespace {
#define HUE_STR_INNER(x) #x
#define HUE_STR(x) HUE_STR_INNER(x)
constexpr const char* kVersionString =
    "hue " HUE_STR(HUE_VERSION_MAJOR) "." HUE_STR(HUE_VERSION_MINOR) "." HUE_STR(HUE_VERSION_PATCH);
#undef HUE_STR
#undef HUE_STR_INNER
} // namespace

Version engine_version() {
    return Version{HUE_VERSION_MAJOR, HUE_VERSION_MINOR, HUE_VERSION_PATCH};
}

const char* engine_version_string() {
    return kVersionString;
}

} // namespace hue
