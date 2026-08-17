// engine/asset/src/stb_impl.cpp
//
// Single TU housing stb_image, restricted to the two formats glTF allows
// (PNG, JPEG) with no file I/O: image bytes always arrive from a validated
// buffer view. Dimension cap enforced inside the decoder; allocations run
// through the tagged heap so decode memory is tracked and canary-guarded.

#include "hue/core/memory.h"

#include <cstring>

namespace {

void* stb_malloc(std::size_t size) {
    auto allocation = hue::heap_allocate(size > 0 ? size : 1, alignof(std::max_align_t),
                                         hue::MemoryTag::kAssets);
    return allocation ? allocation.value() : nullptr;
}

void stb_free(void* memory) {
    if (memory != nullptr) {
        const auto freed = hue::heap_free(memory);
        (void)freed;
    }
}

void* stb_realloc_sized(void* memory, std::size_t old_size, std::size_t new_size) {
    void* next = stb_malloc(new_size);
    if (next == nullptr) {
        return nullptr;
    }
    if (memory != nullptr) {
        std::memcpy(next, memory, old_size < new_size ? old_size : new_size);
        stb_free(memory);
    }
    return next;
}

} // namespace

#define STBI_MALLOC(size) stb_malloc(size)
#define STBI_FREE(pointer) stb_free(pointer)
#define STBI_REALLOC_SIZED(pointer, old_size, new_size) \
    stb_realloc_sized(pointer, old_size, new_size)

#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 4096

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(_MSC_VER)
#pragma warning(push, 0)
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif
