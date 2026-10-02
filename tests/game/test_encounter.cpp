// tests/game/test_encounter.cpp
#include "combat_hud.h"
#include "encounter.h"
#include "hue/asset/gltf_loader.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <optional>
#include <vector>

namespace {
std::vector<unsigned char> model_bytes(const char* name) {
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s", HUE_TEST_MODELS_DIR, name);
    auto* file = std::fopen(path, "rb");
    REQUIRE(file != nullptr);
    std::fseek(file, 0, SEEK_END);
    const long count = std::ftell(file);
    REQUIRE(count > 0);
    std::rewind(file);
    std::vector<unsigned char> data(static_cast<std::size_t>(count));
    REQUIRE(std::fread(data.data(), 1, data.size(), file) == data.size());
    std::fclose(file);
    return data;
}
struct Fixture {
    hue::Result<hue::physics::PhysicsWorld> physics = hue::physics::PhysicsWorld::create();
    hue::Result<hue::LinearArena> arena =
        hue::LinearArena::create(256 * 1024, hue::MemoryTag::kAnimation);
    std::optional<CombatCharacter> actors[2];
    hue::MeshDraw draws[2];
    FollowCamera camera;
    std::optional<Encounter> session;
    Fixture() {
        REQUIRE(physics);
        REQUIRE(arena);
        REQUIRE(physics.value().add_static_box({0, -0.5f, 0}, {}, {30, 0.5f, 30}));
        const char* meshes[2] = {"player.glb", "enemy.glb"};
        const char* events[2] = {"player.events.json", "enemy.events.json"};
        for (std::uint32_t i = 0; i < 2; ++i) {
            const auto bytes = model_bytes(meshes[i]);
            auto mesh = hue::asset::load_gltf_skinned(bytes.data(), bytes.size());
            const auto event_bytes = model_bytes(events[i]);
            auto track = hue::anim::parse_animation_events(event_bytes.data(), event_bytes.size());
            REQUIRE(mesh);
            REQUIRE(track);
            actors[i].emplace(std::move(mesh.value()), std::move(track.value()), hue::MeshHandle{},
                              i);
            REQUIRE(actors[i]->initialize());
            hue::physics::CharacterDesc desc;
            desc.position = kEncounterSpawns[i];
            REQUIRE(physics.value().create_character(desc));
        }
        session.emplace(physics.value(), *actors[0], *actors[1], 0, 1, camera);
        reset();
    }
    void reset() { REQUIRE(session->reset(arena.value(), draws)); }
    void tick(hue::Vec3 move = {}) {
        arena.value().reset();
        REQUIRE(session->tick(move, arena.value(), draws));
    }
};
} // namespace

TEST_CASE("encounter: ready gates input and terminal state rejects new actions") {
    Fixture f;
    f.session->buffer(hue::combat::Action::kAttackHeavy);
    f.session->toggle_lock();
    for (int i = 0; i < 20; ++i)
        f.tick({4, 0, 0});
    CHECK(f.session->state() == EncounterState::kReady);
    CHECK(f.session->stats().ticks == 0);
    CHECK_FALSE(f.session->locked());
    CHECK(f.session->position(0).x == doctest::Approx(kEncounterSpawns[0].x));
    f.session->start();
    f.session->toggle_lock();
    f.tick();
    CHECK(f.session->locked());
    f.actors[1]->apply_damage(1000);
    f.tick();
    CHECK(f.session->state() == EncounterState::kWon);
    CHECK_FALSE(f.session->locked());
    const auto ticks = f.session->stats().ticks;
    for (int i = 0; i < 200; ++i) {
        f.session->buffer(hue::combat::Action::kAttackHeavy);
        f.session->toggle_lock();
        f.session->start();
        f.tick({4, 0, 0});
    }
    CHECK(f.session->state() == EncounterState::kWon);
    CHECK(f.session->stats().ticks == ticks);
    CHECK(f.actors[1]->state() == hue::combat::State::kDeath);
    CHECK(f.actors[0]->buffered_action() == hue::combat::Action::kNone);
    CHECK_FALSE(f.actors[0]->hitbox_active());
}

TEST_CASE("encounter: twenty restarts restore actors physics counters and owned poses") {
    Fixture f;
    hue::AllocationSnapshot warmed{};
    for (int cycle = 0; cycle < 20; ++cycle) {
        f.reset();
        CHECK(f.session->state() == EncounterState::kReady);
        CHECK(f.session->stats().ticks == 0);
        CHECK(f.session->stats().hits[0] == 0);
        CHECK(f.session->hitstop() == 0);
        CHECK(f.session->ai_state() == hue::combat::AiState::kIdle);
        CHECK_FALSE(f.session->locked());
        for (unsigned i = 0; i < 2; ++i) {
            CHECK(f.actors[i]->health() == 100);
            CHECK(f.actors[i]->poise() == 40);
            CHECK(f.actors[i]->activation() == 0);
            CHECK_FALSE(f.actors[i]->invulnerable());
            CHECK(f.actors[i]->buffered_action() == hue::combat::Action::kNone);
            CHECK(f.physics.value().character_velocity(i).y == 0);
            CHECK(f.session->position(i).x == doctest::Approx(kEncounterSpawns[i].x));
            CHECK(f.session->yaw(i) == doctest::Approx(kEncounterYaw[i]));
        }
        f.session->start();
        f.session->toggle_lock();
        f.session->buffer(hue::combat::Action::kAttackHeavy);
        for (int i = 0; i < 60; ++i)
            f.tick();
        f.actors[cycle % 2 == 0 ? 1 : 0]->apply_damage(1000);
        // If the last contact triggered hitstop, outcome resolves when it ends.
        for (int i = 0; i < 180; ++i)
            f.tick();
        CHECK(f.session->state() ==
              (cycle % 2 == 0 ? EncounterState::kWon : EncounterState::kLost));
        CHECK(f.physics.value().character_count() == 2);
        CHECK(f.physics.value().static_body_count() == 1);
        const auto current = hue::allocation_snapshot();
        if (cycle == 3)
            warmed = current;
        if (cycle > 3) {
            for (const auto tag : {hue::MemoryTag::kAnimation, hue::MemoryTag::kAssets,
                                   hue::MemoryTag::kPhysics, hue::MemoryTag::kGame}) {
                const auto index = static_cast<std::size_t>(tag);
                CHECK(current.tags[index].current_bytes == warmed.tags[index].current_bytes);
                CHECK(current.tags[index].live_allocations == warmed.tags[index].live_allocations);
            }
        }
    }
}

TEST_CASE("encounter: zero-tick and hitstop frames keep valid last-pose palettes") {
    Fixture f;
    const auto* retained = f.draws[0].joint_matrices;
    const auto first = retained[0];
    f.arena.value().reset();
    auto overwrite = f.arena.value().allocate_bytes(f.arena.value().capacity());
    REQUIRE(overwrite);
    std::memset(overwrite.value(), 0xDD, f.arena.value().capacity());
    CHECK(f.draws[0].joint_matrices == retained);
    CHECK(std::memcmp(&first, retained, sizeof(first)) == 0);
    f.session->start();
    f.session->buffer(hue::combat::Action::kAttackHeavy);
    bool stopped = false;
    for (int i = 0; i < 180 && !stopped; ++i) {
        f.tick();
        stopped = f.session->hitstop() > 0;
    }
    REQUIRE(stopped);
    const auto before = f.draws[0].joint_matrices[5];
    f.session->buffer(hue::combat::Action::kDodge);
    f.tick();
    CHECK(std::memcmp(&before, &f.draws[0].joint_matrices[5], sizeof(before)) == 0);
    CHECK(f.session->stats().hitstop_ticks > 0);
}

TEST_CASE("encounter: 30 60 and 144 Hz schedules produce matching tick histories") {
    std::vector<std::array<unsigned, 7>> reference;
    for (const double hz : {30.0, 60.0, 144.0}) {
        Fixture f;
        f.session->start();
        hue::FixedTimestep time;
        std::vector<std::array<unsigned, 7>> history;
        unsigned tick = 0;
        while (tick < 600) {
            const int steps = time.advance(1.0 / hz);
            for (int s = 0; s < steps && tick < 600; ++s, ++tick) {
                if (tick % 65 == 0)
                    f.session->buffer(hue::combat::Action::kAttackHeavy);
                if (tick % 121 == 0)
                    f.session->buffer(hue::combat::Action::kDodge);
                f.tick();
                history.push_back({static_cast<unsigned>(f.session->state()),
                                   static_cast<unsigned>(f.actors[0]->state()),
                                   static_cast<unsigned>(f.actors[1]->state()),
                                   static_cast<unsigned>(f.actors[0]->health()),
                                   static_cast<unsigned>(f.actors[1]->health()),
                                   f.session->stats().hits[0], f.session->stats().hits[1]});
            }
        }
        if (reference.empty())
            reference = history;
        else
            CHECK(history == reference);
    }
}

TEST_CASE("encounter: simultaneous fatalities resolve to defeat and reset cleanly") {
    Fixture f;
    f.session->start();
    f.actors[0]->apply_damage(1000);
    f.actors[1]->apply_damage(1000);
    f.tick();
    CHECK(f.session->state() == EncounterState::kLost);
    f.tick();
    CHECK(f.actors[0]->state() == hue::combat::State::kDeath);
    CHECK(f.actors[1]->state() == hue::combat::State::kDeath);
    f.reset();
    CHECK(f.actors[0]->alive());
    CHECK(f.actors[1]->alive());
}

TEST_CASE("encounter: a held hit window damages once and a new press can attack again") {
    Fixture f;
    f.session->start();
    f.session->buffer(hue::combat::Action::kAttackHeavy);
    for (int i = 0; i < 100; ++i)
        f.tick();
    CHECK(f.session->stats().hits[0] == 1);
    CHECK(f.actors[0]->activation() == 1);
    // No second press: catch-up ticks must not synthesize another attack.
    for (int i = 0; i < 60; ++i)
        f.tick();
    CHECK(f.session->stats().hits[0] == 1);
    f.reset();
    f.session->start();
    f.session->buffer(hue::combat::Action::kAttackHeavy);
    for (int i = 0; i < 100; ++i)
        f.tick();
    CHECK(f.session->stats().hits[0] == 1);
}

TEST_CASE("combat character: sub-tick hit window remains observable with owned matrices") {
    auto bytes = model_bytes("player.glb");
    auto mesh = hue::asset::load_gltf_skinned(bytes.data(), bytes.size());
    REQUIRE(mesh);
    constexpr char events[] =
        R"({"events":[{"clip":"attack","time":0.001,"name":"hit_begin"},{"clip":"attack","time":0.002,"name":"hit_end"}]})";
    auto track = hue::anim::parse_animation_events(events, sizeof(events) - 1);
    REQUIRE(track);
    CombatCharacter actor(std::move(mesh.value()), std::move(track.value()), {}, 0);
    REQUIRE(actor.initialize());
    auto arena = hue::LinearArena::create(65536, hue::MemoryTag::kAnimation);
    REQUIRE(arena);
    hue::MeshDraw draws[1];
    REQUIRE(actor.reset(arena.value(), draws));
    actor.buffer_action(hue::combat::Action::kAttackLight);
    arena.value().reset();
    REQUIRE(actor.update(1.0f / 60.0f, arena.value(), draws));
    CHECK(actor.activation() == 1);
    CHECK(actor.strike_window());
    CHECK_FALSE(actor.hitbox_active());
    arena.value().reset();
    REQUIRE(actor.update(1.0f / 60.0f, arena.value(), draws));
    CHECK_FALSE(actor.strike_window());
}

TEST_CASE("encounter HUD: bounded finite quads with diagnostics and capacity exhaustion") {
    Fixture f;
    std::vector<hue::HudQuad> quads(hue::kMaxHudQuads + 1);
    quads.back().ndc_min.x = 1234.0f;
    std::uint32_t count = 0;
    append_combat_hud(quads.data(), count, f.camera.camera(16.0f / 9.0f), *f.session, *f.actors[0],
                      *f.actors[1], true, 16.6, 16384, 2, 2);
    CHECK(count > 0);
    CHECK(count < hue::kMaxHudQuads);
    for (std::uint32_t i = 0; i < count; ++i) {
        CHECK(std::isfinite(quads[i].ndc_min.x));
        CHECK(std::isfinite(quads[i].skew.y));
    }
    count = hue::kMaxHudQuads;
    append_combat_hud(quads.data(), count, {}, *f.session, *f.actors[0], *f.actors[1], true, 0, 0,
                      2, 2);
    CHECK(count == hue::kMaxHudQuads);
    CHECK(quads.back().ndc_min.x == 1234.0f);
    count = 0;
    append_combat_hud(quads.data(), count, {}, *f.session, *f.actors[0], *f.actors[1], 2, 16.6,
                      16384, 2, 2);
    CHECK(count < hue::kMaxHudQuads);
}

TEST_CASE("encounter: scene staging preserves live state and rejects invalid candidates") {
    Fixture f;
    f.session->start();
    f.tick();
    const auto before = f.session->position(0);
    SceneConfig candidate;
    candidate.spawns[0] = {-2, 0.05f, 3};
    REQUIRE(f.session->stage_scene(candidate));
    CHECK(f.session->scene_pending());
    CHECK(f.session->position(0).x == before.x);
    candidate.spawns[0] = {0, 0, 0};
    CHECK_FALSE(f.session->stage_scene(candidate));
    CHECK(f.session->scene().spawns[0].x == doctest::Approx(-0.8));
    for (int i = 0; i < 20; ++i) {
        f.reset();
        CHECK(f.session->position(0).x == -2);
        CHECK_FALSE(f.session->scene_pending());
        CHECK(f.physics.value().character_count() == 2);
    }
}

TEST_CASE("encounter: diagnostics retain last AI sense and reset cooldowns") {
    Fixture f;
    f.session->start();
    for (int i = 0; i < 30; ++i)
        f.tick();
    CHECK(f.session->perception().line_of_sight);
    CHECK(f.session->perception().distance > 0);
    CHECK(f.session->brain().light_cooldown() >= 0);
    CHECK(f.session->brain().heavy_cooldown() >= 0);
    f.reset();
    CHECK(f.session->brain().light_cooldown() == 0);
    CHECK(f.session->brain().heavy_cooldown() == 0);
    CHECK(f.session->perception().distance == 0);
}

TEST_CASE("encounter: tuning commits on reset without altering active event tracks") {
    Fixture f;
    GameTuning t;
    t.revision = 2;
    t.health = 150;
    t.cancel_close_scale = 0.5f;
    f.session->start();
    f.session->buffer(hue::combat::Action::kAttackLight);
    for (int i = 0; i < 8; ++i)
        f.tick();
    CHECK(f.actors[0]->cancel_open());
    REQUIRE(f.session->stage_tuning(t));
    CHECK(f.actors[0]->health() == 100);
    CHECK(f.session->tuning().revision == 1);
    CHECK(f.actors[0]->cancel_open());
    auto bad = t;
    bad.cancel_open_scale = 0.1f;
    CHECK_FALSE(f.session->stage_tuning(bad));
    CHECK(f.session->tuning_pending());
    for (int cycle = 0; cycle < 20; ++cycle) {
        f.reset();
        CHECK(f.actors[0]->health() == 150);
        CHECK(f.session->tuning().revision == 2);
        CHECK_FALSE(f.session->tuning_pending());
        f.session->start();
        f.session->buffer(hue::combat::Action::kAttackLight);
        for (int i = 0; i < 8; ++i)
            f.tick();
        CHECK_FALSE(f.actors[0]->cancel_open());
    }
}
