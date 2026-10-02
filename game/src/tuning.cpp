// game/src/tuning.cpp
#include "tuning.h"
#include "config_json.h"
#include <cmath>
namespace {
bool range(float x, float lo, float hi) {
    return std::isfinite(x) && x >= lo && x <= hi;
}
bool strike(const hue::combat::Strike& s) {
    return range(s.health, 1, 100) && range(s.poise, 1, 100) && range(s.hitstop_seconds, 0, 0.2f) &&
           range(s.shake, 0, 0.5f);
}
} // namespace
bool valid_tuning(const GameTuning& t) noexcept {
    const auto& a = t.ai;
    return t.revision > 0 && t.revision <= 1000000 && range(t.walk_speed, 0.5f, 4) &&
           range(t.run_speed, t.walk_speed + 0.1f, 8) &&
           range(t.idle_threshold, 0.01f, t.walk_speed * 0.5f) && range(t.dodge_speed, 1, 10) &&
           range(t.dodge_seconds, 0.1f, 1) && range(t.health, 1, 1000) && range(t.poise, 1, 200) &&
           range(t.regen_delay, 0, 10) && range(t.regen_rate, 0, 100) &&
           range(t.input_buffer, 0.05f, 1) && strike(t.light) && strike(t.heavy) &&
           range(t.cancel_close_scale, 0.1f, 2) && range(t.cancel_open_scale, 0.1f, 2) &&
           range(t.lock_acquire, 2, 20) && range(t.lock_release, t.lock_acquire, 30) &&
           range(a.attack_range, 0.5f, 2.5f) && range(a.aggro_range, a.attack_range, 20) &&
           range(a.deaggro_range, a.aggro_range, 30) && range(a.walk_speed, 0.1f, 4) &&
           range(a.run_speed, a.walk_speed, 8) && range(a.strafe_speed, 0, 4) &&
           range(a.notice_seconds, 0, 5) && range(a.recover_seconds, 0.1f, 5) &&
           range(a.los_memory_seconds, 0, 5) && range(a.light_weight, 0, 1) &&
           range(a.light_cooldown, 0, 10) && range(a.heavy_cooldown, 0, 10);
}
hue::Result<GameTuning> parse_tuning(const void* data, std::size_t size) {
    auto obj = parse_config_json(data, size);
    if (!obj || obj.value().count != 14)
        return hue::ErrorCode::kCorruptData;
    const auto values = [&](const char* name, unsigned count, bool array) -> const float* {
        const auto* f = obj.value().find(name);
        return f && !f->is_string && f->is_array == array && f->count == count ? f->numbers
                                                                               : nullptr;
    };
    const float* version = values("version", 1, false);
    const float* revision = values("revision", 1, false);
    const float* movement = values("movement", 3, true);
    const float* dodge = values("dodge", 2, true);
    const float* vitals = values("vitals", 4, true);
    const float* buffer = values("input_buffer", 1, false);
    const float* light = values("light_strike", 4, true);
    const float* heavy = values("heavy_strike", 4, true);
    const float* ranges = values("ai_ranges", 3, true);
    const float* speeds = values("ai_speeds", 3, true);
    const float* times = values("ai_times", 3, true);
    const float* attacks = values("ai_attacks", 3, true);
    const float* cancel = values("cancel_scale", 2, true);
    const float* lock = values("lock_ranges", 2, true);
    if (!version || *version != 1 || !revision || !range(*revision, 1, 1000000) ||
        std::floor(*revision) != *revision || !movement || !dodge || !vitals || !buffer || !light ||
        !heavy || !ranges || !speeds || !times || !attacks || !cancel || !lock)
        return hue::ErrorCode::kCorruptData;
    GameTuning t;
    t.revision = static_cast<unsigned>(*revision);
    t.walk_speed = movement[0];
    t.run_speed = movement[1];
    t.idle_threshold = movement[2];
    t.dodge_speed = dodge[0];
    t.dodge_seconds = dodge[1];
    t.health = vitals[0];
    t.poise = vitals[1];
    t.regen_delay = vitals[2];
    t.regen_rate = vitals[3];
    t.input_buffer = *buffer;
    t.light = {light[0], light[1], light[2], light[3]};
    t.heavy = {heavy[0], heavy[1], heavy[2], heavy[3]};
    t.ai.aggro_range = ranges[0];
    t.ai.deaggro_range = ranges[1];
    t.ai.attack_range = ranges[2];
    t.ai.walk_speed = speeds[0];
    t.ai.run_speed = speeds[1];
    t.ai.strafe_speed = speeds[2];
    t.ai.notice_seconds = times[0];
    t.ai.recover_seconds = times[1];
    t.ai.los_memory_seconds = times[2];
    t.ai.light_weight = attacks[0];
    t.ai.light_cooldown = attacks[1];
    t.ai.heavy_cooldown = attacks[2];
    t.cancel_close_scale = cancel[0];
    t.cancel_open_scale = cancel[1];
    t.lock_acquire = lock[0];
    t.lock_release = lock[1];
    if (!valid_tuning(t))
        return hue::ErrorCode::kCorruptData;
    return t;
}
