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
};
} // namespace
const ConfigField* ConfigObject::find(const char* key) const noexcept {
    for (unsigned i = 0; i < count; ++i)
        if (std::strcmp(fields[i].key, key) == 0)
            return &fields[i];
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
            if (result.count == 32)
                return hue::ErrorCode::kCorruptData;
            auto& field = result.fields[result.count];
            if (!r.string(field.key) || result.find(field.key) || !r.take(':'))
                return hue::ErrorCode::kCorruptData;
            r.space();
            if (r.p != r.end && *r.p == '"') {
                field.is_string = true;
                if (!r.string(field.string))
                    return hue::ErrorCode::kCorruptData;
            } else if (r.take('[')) {
                field.is_array = true;
                do {
                    if (field.count == 4 || !r.number(field.numbers[field.count++]))
                        return hue::ErrorCode::kCorruptData;
                } while (r.take(','));
                if (!r.take(']'))
                    return hue::ErrorCode::kCorruptData;
            } else {
                field.count = 1;
                if (!r.number(field.numbers[0]))
                    return hue::ErrorCode::kCorruptData;
            }
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
