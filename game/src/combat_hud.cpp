// game/src/combat_hud.cpp
#include "combat_hud.h"
#include "hue/core/trace.h"
#include <cmath>
#include <cstdio>

namespace {
// Original 5x7 uppercase bitmap alphabet; rows are five-bit masks.
constexpr unsigned char kLetters[26][7] = {
    {14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30}, {14, 17, 16, 16, 16, 17, 14},
    {30, 17, 17, 17, 17, 17, 30}, {31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
    {14, 17, 16, 23, 17, 17, 15}, {17, 17, 17, 31, 17, 17, 17}, {14, 4, 4, 4, 4, 4, 14},
    {7, 2, 2, 2, 2, 18, 12},      {17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
    {17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17}, {14, 17, 17, 17, 17, 17, 14},
    {30, 17, 17, 30, 16, 16, 16}, {14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
    {15, 16, 16, 14, 1, 1, 30},   {31, 4, 4, 4, 4, 4, 4},       {17, 17, 17, 17, 17, 17, 14},
    {17, 17, 17, 17, 17, 10, 4},  {17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
    {17, 17, 10, 4, 4, 4, 4},     {31, 1, 2, 4, 8, 16, 31}};
constexpr unsigned char kDigits[10][7] = {{14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
                                          {14, 17, 1, 2, 4, 8, 31},     {30, 1, 1, 14, 1, 1, 30},
                                          {2, 6, 10, 18, 31, 2, 2},     {31, 16, 16, 30, 1, 1, 30},
                                          {14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
                                          {14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14}};
unsigned char glyph_row(char c, int row) {
    if (c >= 'a' && c <= 'z')
        c = static_cast<char>(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z')
        return kLetters[c - 'A'][row];
    if (c >= '0' && c <= '9')
        return kDigits[c - '0'][row];
    if (c == ':' && (row == 2 || row == 5))
        return 4;
    if (c == '.' && row == 6)
        return 4;
    if ((c == '-' || c == '_') && row == (c == '-' ? 3 : 6))
        return 14;
    if (c == '/')
        return static_cast<unsigned char>(1u << static_cast<unsigned>(row * 4 / 6));
    return 0;
}
void quad(hue::HudQuad* q, std::uint32_t& n, hue::Vec2 a, hue::Vec2 b, hue::Vec4 color,
          hue::Vec2 skew = {}) {
    if (n < hue::kMaxHudQuads)
        q[n++] = {a, b, color, skew};
}
void text(hue::HudQuad* q, std::uint32_t& n, float x, float y, const char* message,
          float scale = 1.0f) {
    const float px = 0.0027f * scale, py = 0.0048f * scale;
    for (unsigned i = 0; message[i] != '\0' && i < 160; ++i) {
        for (int row = 0; row < 7;) {
            const unsigned bits = glyph_row(message[i], row);
            int end_row = row + 1;
            while (end_row < 7 && glyph_row(message[i], end_row) == bits)
                ++end_row;
            for (int col = 0; col < 5;) {
                if ((bits & (1u << static_cast<unsigned>(4 - col))) == 0) {
                    ++col;
                    continue;
                }
                const int begin = col++;
                while (col < 5 && (bits & (1u << static_cast<unsigned>(4 - col))) != 0)
                    ++col;
                quad(q, n,
                     {x + static_cast<float>(begin) * px, y - static_cast<float>(end_row) * py},
                     {x + static_cast<float>(col) * px, y - static_cast<float>(row) * py},
                     {0.94f, 0.96f, 1.0f, 1.0f});
            }
            row = end_row;
        }
        x += px * 6.0f;
    }
}
bool project(const hue::Camera& camera, hue::Vec3 p, hue::Vec2& out) {
    const auto clip = camera.view_projection() * hue::Vec4{p.x, p.y, p.z, 1.0f};
    if (!(clip.w > 0.01f) || clip.z < 0.0f)
        return false;
    out = {clip.x / clip.w, clip.y / clip.w};
    return std::isfinite(out.x) && std::isfinite(out.y);
}
void line(hue::HudQuad* q, std::uint32_t& n, const hue::Camera& camera, hue::Vec3 a, hue::Vec3 b,
          hue::Vec4 color) {
    hue::Vec2 p{}, end{};
    if (!project(camera, a, p) || !project(camera, b, end))
        return;
    const float dx = end.x - p.x, dy = end.y - p.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.00001f)
        return;
    const hue::Vec2 normal{-dy * 0.0018f / len, dx * 0.0018f / len};
    quad(q, n, p, {end.x, p.y + normal.y}, color, {normal.x, dy});
}
void volumes(hue::HudQuad* q, std::uint32_t& n, const hue::Camera& camera,
             const CombatCharacter& actor, const hue::Mat4& model,
             const hue::combat::BoundVolumeSet& set, hue::Vec4 color) {
    for (std::uint32_t v = 0; v < set.count; ++v) {
        const auto& volume = set.items[v];
        const auto world = actor.volume_transform(model, volume);
        const float r = volume.radius;
        const float h =
            volume.kind == hue::combat::VolumeKind::kCapsule ? volume.half_height : 0.0f;
        // Equators and two axial silhouettes show the actual spherical caps.
        for (int ring = 0; ring < 4; ++ring) {
            hue::Vec3 previous{};
            for (int s = 0; s <= 32; ++s) {
                const float angle = static_cast<float>(s) * hue::kTwoPi / 32.0f;
                const float c = std::cos(angle), sn = std::sin(angle);
                hue::Vec3 local{};
                if (ring < 2)
                    local = {r * c, ring == 0 ? h : -h, r * sn};
                else {
                    const float y = r * sn + (sn >= 0.0f ? h : -h);
                    local = ring == 2 ? hue::Vec3{r * c, y, 0.0f} : hue::Vec3{0.0f, y, r * c};
                }
                const auto point = world.transform_point(local);
                if (s > 0)
                    line(q, n, camera, previous, point, color);
                previous = point;
            }
        }
    }
}
} // namespace

void append_combat_hud(hue::HudQuad* q, std::uint32_t& n, const hue::Camera& camera,
                       const Encounter& encounter, const CombatCharacter& player,
                       const CombatCharacter& enemy, unsigned diagnostics, double frame_ms,
                       std::size_t arena_peak, std::size_t entities, std::uint32_t bodies,
                       const char* reload_status) {
    HUE_PROFILE_ZONE("game::combat_hud");
    quad(q, n, {-0.98f, -0.98f}, {0.98f, -0.76f}, {0.02f, 0.025f, 0.04f, 0.88f});
    text(q, n, -0.94f, -0.80f, "WASD / LEFT STICK MOVE   SHIFT RUN   RMB / RIGHT STICK CAMERA");
    text(q, n, -0.94f, -0.85f, "LMB / X LIGHT   E / Y HEAVY   SPACE / B DODGE   TAB / R3 LOCK");
    text(q, n, -0.94f, -0.90f,
         "ENTER / START BEGIN   R RESET   F2 / BACK DEBUG   F5 SCENE RELOAD   ESC QUIT");
    char buffer[160];
    std::snprintf(
        buffer, sizeof(buffer), "%s   TIME %.1F   LOCK %s", encounter_state_name(encounter.state()),
        static_cast<double>(encounter.stats().ticks) / 60.0, encounter.locked() ? "ON" : "OFF");
    text(q, n, -0.42f, 0.78f, buffer);
    if (encounter.state() != EncounterState::kFighting) {
        quad(q, n, {-0.70f, 0.02f}, {0.70f, 0.27f}, {0.025f, 0.035f, 0.06f, 0.92f});
        text(q, n, -0.25f, 0.23f, encounter_state_name(encounter.state()), 2.0f);
        text(q, n, -0.54f, 0.10f,
             encounter.state() == EncounterState::kReady ? "ENTER / START TO FIGHT"
                                                         : "ENTER / START OR R TO RESTART");
    }
    if (!diagnostics)
        return;
    if (reload_status)
        text(q, n, -0.94f, -0.70f, reload_status);
    if (diagnostics == 2) {
        quad(q, n, {-0.98f, -0.05f}, {0.60f, 0.71f}, {0.02f, 0.025f, 0.04f, 0.94f});
        text(q, n, -0.94f, 0.67f, "CPU TAG       LIVE KB    PEAK KB    ALLOCS    FRAME B", 0.9f);
        const auto snapshot = hue::allocation_snapshot();
        for (std::size_t i = 0; i < hue::kMemoryTagCount; ++i) {
            const auto& tag = snapshot.tags[i];
            std::snprintf(buffer, sizeof(buffer), "%-12s %8zu %10zu %9zu %10zu",
                          hue::memory_tag_name(static_cast<hue::MemoryTag>(i)),
                          tag.current_bytes / 1024, tag.peak_bytes / 1024, tag.live_allocations,
                          tag.frame_requested_bytes);
            text(q, n, -0.94f, 0.61f - static_cast<float>(i) * 0.05f, buffer, 0.9f);
        }
        std::snprintf(buffer, sizeof(buffer),
                      "FRAME %.2F MS  ARENA PEAK %zu B  ENTITIES %zu  CONTROLLERS %u", frame_ms,
                      arena_peak, entities, bodies);
        text(q, n, -0.94f, 0.10f, buffer, 0.9f);
        text(q, n, -0.94f, 0.04f, "CPU ALLOCATIONS ONLY - NOT VRAM. F2 RETURNS TO GAME.", 0.9f);
        return;
    }
    volumes(q, n, camera, player, encounter.model(0), player.hurtboxes(),
            {0.1f, 0.9f, 0.4f, 0.85f});
    volumes(q, n, camera, enemy, encounter.model(1), enemy.hurtboxes(), {0.1f, 0.7f, 1.0f, 0.85f});
    const CombatCharacter* actors[2] = {&player, &enemy};
    for (std::uint32_t i = 0; i < 2; ++i) {
        const auto& actor = *actors[i];
        volumes(q, n, camera, actor, encounter.model(i), actor.hitboxes(),
                actor.hitbox_active() ? hue::Vec4{1.0f, 0.15f, 0.1f, 1.0f}
                                      : hue::Vec4{0.6f, 0.6f, 0.6f, 0.6f});
    }
    quad(q, n, {-0.98f, 0.12f}, {0.95f, 0.71f}, {0.02f, 0.025f, 0.04f, 0.9f});
    for (std::uint32_t i = 0; i < 2; ++i) {
        const auto& a = *actors[i];
        std::snprintf(buffer, sizeof(buffer),
                      "%s %s HP %.0F POISE %.0F HIT %d CANCEL %d IFRAME %d BUFFER %u %.2FS",
                      i == 0 ? "PLAYER" : "ENEMY", hue::combat::state_name(a.state()),
                      static_cast<double>(a.health()), static_cast<double>(a.poise()),
                      a.hitbox_active() ? 1 : 0, a.cancel_open() ? 1 : 0, a.invulnerable() ? 1 : 0,
                      static_cast<unsigned>(a.buffered_action()),
                      static_cast<double>(a.buffer_remaining()));
        text(q, n, -0.94f, 0.67f - static_cast<float>(i) * 0.05f, buffer);
    }
    std::snprintf(buffer, sizeof(buffer), "AI %s   HITS %u / %u   DODGES %u   HITSTOP %.3F",
                  hue::combat::ai_state_name(encounter.ai_state()), encounter.stats().hits[0],
                  encounter.stats().hits[1], encounter.stats().dodges,
                  static_cast<double>(encounter.hitstop()));
    text(q, n, -0.94f, 0.57f, buffer);
    std::size_t live = 0;
    const auto memory = hue::allocation_snapshot();
    for (const auto& tag : memory.tags)
        live += tag.current_bytes;
    std::snprintf(buffer, sizeof(buffer),
                  "FRAME %.2F MS   HEAP %zu KB   POSE PEAK %zu B   ENTITIES %zu BODIES %u",
                  frame_ms, live / 1024, arena_peak, entities, bodies);
    text(q, n, -0.94f, 0.52f, buffer);
    text(q, n, -0.94f, 0.47f, "XRAY VOLUMES: RED ACTIVE HIT   GRAY INACTIVE   GREEN/BLUE HURT");
    const auto& sense = encounter.perception();
    const auto& brain = encounter.brain();
    std::snprintf(buffer, sizeof(buffer), "AI LAST TICK DIST %.2F LOS %d LOST %.2FS STATE %.2FS",
                  static_cast<double>(sense.distance), sense.line_of_sight ? 1 : 0,
                  static_cast<double>(brain.los_lost_seconds()),
                  static_cast<double>(brain.state_seconds()));
    text(q, n, -0.94f, 0.42f, buffer);
    std::snprintf(buffer, sizeof(buffer), "COOLDOWN LIGHT %.2FS HEAVY %.2FS  LAST ACTION %u",
                  static_cast<double>(brain.light_cooldown()),
                  static_cast<double>(brain.heavy_cooldown()),
                  static_cast<unsigned>(brain.last_attack()));
    text(q, n, -0.94f, 0.37f, buffer);
    std::snprintf(buffer, sizeof(buffer), "HIT ACTIVATION %llu / %llu   SAMPLED WINDOW %d / %d",
                  static_cast<unsigned long long>(player.activation()),
                  static_cast<unsigned long long>(enemy.activation()),
                  player.strike_window() ? 1 : 0, enemy.strike_window() ? 1 : 0);
    text(q, n, -0.94f, 0.32f, buffer);
    text(q, n, -0.94f, 0.27f, "BUFFER: 0 NONE  1 LIGHT  2 HEAVY  3 DODGE. EXPIRY IN SIM SECONDS.");
    std::snprintf(buffer, sizeof(buffer), "TUNING REV %u  PENDING %d  SCENE PENDING %d. F2 MEMORY.",
                  encounter.tuning().revision, encounter.tuning_pending() ? 1 : 0,
                  encounter.scene_pending() ? 1 : 0);
    text(q, n, -0.94f, 0.22f, buffer);
}
