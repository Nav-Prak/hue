// game/src/tuning.h
// One bounded configuration shared by movement, pose blending, combat and AI.
// Schema version is separate from the author's revision used in fight reports.
#pragma once
#include "hue/combat/ai.h"
#include "hue/core/result.h"
#include <cstddef>
struct GameTuning {
    unsigned revision = 1;
    float walk_speed = 1.6f, run_speed = 4.0f, idle_threshold = 0.2f;
    float dodge_speed = 6.0f, dodge_seconds = 0.45f;
    float health = 100.0f, poise = 40.0f, regen_delay = 1.0f, regen_rate = 18.0f;
    float input_buffer = 0.35f;
    hue::combat::Strike light = hue::combat::kStrikeLight;
    hue::combat::Strike heavy = hue::combat::kStrikeHeavy;
    hue::combat::AiTuning ai{};
    float cancel_close_scale = 1.0f, cancel_open_scale = 1.0f;
    float lock_acquire = 15.0f, lock_release = 20.0f;
};
[[nodiscard]] bool valid_tuning(const GameTuning& tuning) noexcept;
[[nodiscard]] hue::Result<GameTuning> parse_tuning(const void* data, std::size_t size);
