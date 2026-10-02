// game/src/combat_hud.h
// Bounded bitmap text and projected wire volumes using the existing HUD pass.
// Debug volumes are deliberately X-ray (depth off), not collision geometry.
#pragma once
#include "encounter.h"

void append_combat_hud(hue::HudQuad* quads, std::uint32_t& count, const hue::Camera& camera,
                       const Encounter& encounter, const CombatCharacter& player,
                       const CombatCharacter& enemy, unsigned diagnostics, double frame_ms,
                       std::size_t arena_peak, std::size_t entities, std::uint32_t bodies);
