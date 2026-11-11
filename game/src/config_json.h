// game/src/config_json.h
// Bounded, allocation-free JSON subset for flat game configuration schemas.
// No nesting, escapes or arbitrary paths: schema consumers whitelist every key.
#pragma once
#include "hue/core/result.h"
#include <cstddef>

inline constexpr std::size_t kConfigMaxBytes = 4096;
struct ConfigField {
    char key[48]{};
    char string[48]{};
    float numbers[4]{};
    unsigned count = 0;
    bool is_string = false;
    bool is_array = false;
};
struct ConfigObject {
    ConfigField fields[32]{};
    unsigned count = 0;
    const ConfigField* find(const char* key) const noexcept;
};
[[nodiscard]] hue::Result<ConfigObject> parse_config_json(const void* data, std::size_t size);

// One extra level for scene files: a root object whose values may also be
// arrays of flat records. Records never nest. Every count is capped and the
// result lives in caller-provided fixed storage, so parsing never allocates.
inline constexpr std::size_t kDocumentMaxBytes = 16384;
inline constexpr unsigned kDocumentMaxLists = 4;
inline constexpr unsigned kDocumentMaxRecords = 16;
inline constexpr unsigned kRecordMaxFields = 8;
struct ConfigRecord {
    ConfigField fields[kRecordMaxFields]{};
    unsigned count = 0;
    const ConfigField* find(const char* key) const noexcept;
};
struct ConfigList {
    char key[48]{};
    ConfigRecord items[kDocumentMaxRecords]{};
    unsigned count = 0;
};
struct ConfigDocument {
    ConfigObject root{};
    ConfigList lists[kDocumentMaxLists]{};
    unsigned list_count = 0;
    const ConfigList* list(const char* key) const noexcept;
};
// `out` is only meaningful on success; a failed parse may leave it partly written.
[[nodiscard]] hue::Result<void> parse_config_document(const void* data, std::size_t size,
                                                      ConfigDocument& out);
