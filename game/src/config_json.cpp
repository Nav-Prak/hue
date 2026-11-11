// game/src/config_json.cpp
#include "config_json.h"
#include "hue/core/trace.h"
#include <charconv>
#include <cmath>
#include <cstring>

namespace {
class Reader {
public:
    const char* p;
    const char* end;
    void space() {
        while (p != end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t'))
            ++p;
    }
    bool take(char c) {
        space();
        if (p == end || *p != c)
            return false;
        ++p;
        return true;
    }
    bool string(char (&out)[48]) {
        if (!take('"'))
            return false;
        unsigned n = 0;
        while (p != end && *p != '"') {
            const unsigned char c = static_cast<unsigned char>(*p++);
            if (c < 32 || c > 126 || c == '\\' || n >= 47)
                return false;
            out[n++] = static_cast<char>(c);
        }
        out[n] = 0;
        return take('"');
    }
    bool number(float& out) {
        space();
        const char* begin = p;
        if (p != end && *p == '-')
            ++p;
        if (p == end)
            return false;
        if (*p == '0')
            ++p;
        else {
            if (*p < '1' || *p > '9')
                return false;
            while (p != end && *p >= '0' && *p <= '9')
                ++p;
        }
        if (p != end && *p == '.') {
            ++p;
            const char* digits = p;
            while (p != end && *p >= '0' && *p <= '9')
                ++p;
            if (p == digits)
                return false;
        }
        if (p != end && (*p == 'e' || *p == 'E')) {
            ++p;
            if (p != end && (*p == '+' || *p == '-'))
                ++p;
            const char* digits = p;
            while (p != end && *p >= '0' && *p <= '9')
                ++p;
            if (p == digits)
                return false;
        }
        const auto parsed = std::from_chars(begin, p, out);
        return parsed.ec == std::errc{} && parsed.ptr == p && std::isfinite(out);
    }
    // String, number, or one to four numbers. The opening '[' of an array may
    // already have been consumed by a caller that had to look past it.
    bool value(ConfigField& field, bool array_open = false) {
        space();
        if (!array_open && p != end && *p == '"') {
            field.is_string = true;
            return string(field.string);
        }
        if (array_open || take('[')) {
            field.is_array = true;
            do {
                if (field.count == 4 || !number(field.numbers[field.count++]))
                    return false;
            } while (take(','));
            return take(']');
        }
        field.count = 1;
        return number(field.numbers[0]);
    }
    // Reads `"key":` into fields[count], rejecting overflow and duplicates.
    bool key(ConfigField* fields, unsigned count, unsigned capacity, ConfigField*& out) {
        if (count >= capacity)
            return false;
        out = &fields[count];
        *out = ConfigField{};
        if (!string(out->key))
            return false;
        for (unsigned i = 0; i < count; ++i)
            if (std::strcmp(fields[i].key, out->key) == 0)
                return false;
        return take(':');
    }
};
const ConfigField* find_field(const ConfigField* fields, unsigned count, const char* key) noexcept {
    for (unsigned i = 0; i < count; ++i)
        if (std::strcmp(fields[i].key, key) == 0)
            return &fields[i];
    return nullptr;
}
} // namespace
const ConfigField* ConfigObject::find(const char* key) const noexcept {
    return find_field(fields, count, key);
}
const ConfigField* ConfigRecord::find(const char* key) const noexcept {
    return find_field(fields, count, key);
}
const ConfigList* ConfigDocument::list(const char* key) const noexcept {
    for (unsigned i = 0; i < list_count; ++i)
        if (std::strcmp(lists[i].key, key) == 0)
            return &lists[i];
    return nullptr;
}
hue::Result<ConfigObject> parse_config_json(const void* data, std::size_t size) {
    HUE_PROFILE_ZONE("game::parse_config_json");
    if (!data || size == 0 || size > kConfigMaxBytes)
        return hue::ErrorCode::kCorruptData;
    const auto* bytes = static_cast<const char*>(data);
    Reader r{bytes, bytes + size};
    ConfigObject result;
    if (!r.take('{'))
        return hue::ErrorCode::kCorruptData;
    if (!r.take('}')) {
        do {
            ConfigField* field = nullptr;
            if (!r.key(result.fields, result.count, 32, field) || !r.value(*field))
                return hue::ErrorCode::kCorruptData;
            ++result.count;
        } while (r.take(','));
        if (!r.take('}'))
            return hue::ErrorCode::kCorruptData;
    }
    r.space();
    if (r.p != r.end)
        return hue::ErrorCode::kCorruptData;
    return result;
}
hue::Result<void> parse_config_document(const void* data, std::size_t size, ConfigDocument& out) {
    HUE_PROFILE_ZONE("game::parse_config_document");
    if (!data || size == 0 || size > kDocumentMaxBytes)
        return hue::ErrorCode::kCorruptData;
    const auto* bytes = static_cast<const char*>(data);
    Reader r{bytes, bytes + size};
    out.root.count = 0;
    out.list_count = 0;
    if (!r.take('{'))
        return hue::ErrorCode::kCorruptData;
    if (!r.take('}')) {
        do {
            // The key is read into root storage first and moves to a list when
            // the value turns out to be an array of records.
            ConfigField* field = nullptr;
            if (!r.key(out.root.fields, out.root.count, 32, field))
                return hue::ErrorCode::kCorruptData;
            for (unsigned i = 0; i < out.list_count; ++i)
                if (std::strcmp(out.lists[i].key, field->key) == 0)
                    return hue::ErrorCode::kCorruptData;
            r.space();
            bool is_list = false;
            bool array_open = false;
            if (r.p != r.end && *r.p == '[') {
                ++r.p;
                array_open = true;
                r.space();
                is_list = r.p != r.end && (*r.p == '{' || *r.p == ']');
            }
            if (!is_list) {
                if (!r.value(*field, array_open))
                    return hue::ErrorCode::kCorruptData;
                ++out.root.count;
                continue;
            }
            if (out.list_count == kDocumentMaxLists)
                return hue::ErrorCode::kCorruptData;
            auto& list = out.lists[out.list_count];
            std::memcpy(list.key, field->key, sizeof(list.key));
            list.count = 0;
            if (!r.take(']')) {
                do {
                    if (list.count == kDocumentMaxRecords || !r.take('{'))
                        return hue::ErrorCode::kCorruptData;
                    auto& record = list.items[list.count];
                    record.count = 0;
                    if (!r.take('}')) {
                        do {
                            ConfigField* member = nullptr;
                            if (!r.key(record.fields, record.count, kRecordMaxFields, member) ||
                                !r.value(*member))
                                return hue::ErrorCode::kCorruptData;
                            ++record.count;
                        } while (r.take(','));
                        if (!r.take('}'))
                            return hue::ErrorCode::kCorruptData;
                    }
                    ++list.count;
                } while (r.take(','));
                if (!r.take(']'))
                    return hue::ErrorCode::kCorruptData;
            }
            ++out.list_count;
        } while (r.take(','));
        if (!r.take('}'))
            return hue::ErrorCode::kCorruptData;
    }
    r.space();
    if (r.p != r.end)
        return hue::ErrorCode::kCorruptData;
    return {};
}
