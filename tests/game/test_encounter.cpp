// tests/game/test_encounter.cpp
#include "combat_hud.h"
#include "encounter.h"
#include "fight_report.h"
#include "hue/asset/gltf_loader.h"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
    std::optional<RigAsset> rig_storage[kRigCount];
    const RigAsset* rigs[kRigCount]{};
    std::optional<ActorRoster> roster;
    // Refreshed after every reset: a scene change can replace a slot's character.
    CombatCharacter* actors[kMaxActors]{};
    hue::MeshDraw draws[kMaxActors];
    FollowCamera camera;
    std::optional<Encounter> session;
    Fixture() {
        REQUIRE(physics);
        REQUIRE(arena);
        REQUIRE(physics.value().add_static_box({0, -0.5f, 0}, {}, {30, 0.5f, 30}));
        const char* meshes[kRigCount] = {"player.glb", "enemy.glb"};
        const char* events[kRigCount] = {"player.events.json", "enemy.events.json"};
        for (unsigned i = 0; i < kRigCount; ++i) {
            const auto bytes = model_bytes(meshes[i]);
            auto mesh = hue::asset::load_gltf_skinned(bytes.data(), bytes.size());
            const auto event_bytes = model_bytes(events[i]);
            auto track = hue::anim::parse_animation_events(event_bytes.data(), event_bytes.size());
            REQUIRE(mesh);
            REQUIRE(track);
            rig_storage[i].emplace(
                RigAsset{std::move(mesh.value()), std::move(track.value()), hue::MeshHandle{}});
            rigs[i] = &*rig_storage[i];
        }
        roster.emplace(physics.value(), rigs);
        REQUIRE(roster->initialize());
        session.emplace(physics.value(), *roster, camera);
        reset();
    }
    void reset() {
        REQUIRE(session->reset(arena.value(), draws));
        for (std::uint32_t i = 0; i < kMaxActors; ++i)
            actors[i] = i < session->actor_count() ? &session->actor(i) : nullptr;
    }
    // Player plus up to seven barbarians on clear ground east of the default spawn.
    [[nodiscard]] static SceneConfig crowd(unsigned enemies) {
        static constexpr float kSpots[7][2] = {{0.8f, 3.0f}, {0.8f, 4.3f}, {2.1f, 3.0f},
                                               {2.1f, 4.3f}, {3.4f, 3.0f}, {0.8f, 7.6f},
                                               {2.1f, 7.6f}};
        SceneConfig scene;
        scene.actor_count = 1 + enemies;
        for (unsigned e = 0; e < enemies && e < 7; ++e)
            scene.actors[1 + e] = {RigId::kBarbarian,
                                   {kSpots[e][0], kSceneGroundY, kSpots[e][1]},
                                   hue::kPi * 0.5f};
        REQUIRE(valid_scene(scene));
        return scene;
    }
    void tick(hue::Vec3 move = {}) {
        arena.value().reset();
        REQUIRE(session->tick(move, arena.value(), draws));
    }
};
} // namespace

TEST_CASE("encounter: ten automated fights finish through real contact damage") {
    Fixture f;
    for (unsigned fight = 0; fight < 10; ++fight) {
        f.reset();
        f.session->start();
        f.session->toggle_lock();
        const auto started = std::chrono::steady_clock::now();
        for (unsigned tick = 0; tick < 3600 && f.session->state() == EncounterState::kFighting;
             ++tick) {
            // Deliberately simple policy, not a simulated human or balance verdict.
            if (fight % 2 == 0)
                f.session->buffer(hue::combat::Action::kAttackHeavy);
            f.tick();
        }
        REQUIRE(f.session->state() != EncounterState::kFighting);
        CHECK(f.session->stats().hits[0] + f.session->stats().hits[1] > 0);
        const auto wall =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        FightReport report{f.session->state(), f.session->stats(), wall,
                           f.session->tuning().revision, FightSource::kAutomated};
        char record[1024];
        REQUIRE(format_fight_report(record, sizeof(record), report));
        // Optional explicit test-output destination, never the source tree by default.
        if (const auto* output = std::getenv("HUE_TEST_TELEMETRY"))
            REQUIRE(append_fight_report(output, report));
    }
}

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
        CHECK(f.session->hitstop() == 0.0f);
        CHECK(f.session->ai_state() == hue::combat::AiState::kIdle);
        CHECK_FALSE(f.session->locked());
        for (unsigned i = 0; i < 2; ++i) {
            CHECK(f.actors[i]->health() == 100.0f);
            CHECK(f.actors[i]->poise() == 40.0f);
            CHECK(f.actors[i]->activation() == 0);
            CHECK_FALSE(f.actors[i]->invulnerable());
            CHECK(f.actors[i]->buffered_action() == hue::combat::Action::kNone);
            CHECK(f.physics.value().character_velocity(i).y == 0.0f);
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
    CombatCharacter actor(mesh.value(), track.value(), {}, 0);
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

TEST_CASE("encounter HUD: bitmap glyphs align to framebuffer pixels at multiple sizes") {
    Fixture f;
    std::vector<hue::HudQuad> quads(hue::kMaxHudQuads);
    for (const auto size : {std::array<unsigned, 2>{1280, 720}, {854, 480}, {2560, 1440},
                            {800, 600}, {1920, 1080}, {3440, 1440}, {3840, 2160}}) {
        for (unsigned page : {0u, 1u, 2u}) {
            std::uint32_t count = 0;
            append_combat_hud(quads.data(), count, {}, *f.session, page, 16.6, 16384, 2, 2,
                              nullptr, size[0], size[1]);
            REQUIRE(count < hue::kMaxHudQuads);
            unsigned glyph_quads = 0;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (quads[i].color.x != 0.94f)
                    continue;
                ++glyph_quads;
                for (const auto corner : {quads[i].ndc_min, quads[i].ndc_max}) {
                    const float x = (corner.x + 1) * static_cast<float>(size[0]) * 0.5f;
                    const float y = (corner.y + 1) * static_cast<float>(size[1]) * 0.5f;
                    CHECK(std::abs(x - std::round(x)) < 0.002f);
                    CHECK(std::abs(y - std::round(y)) < 0.002f);
                    CHECK(corner.x >= -1.0f);
                    CHECK(corner.x <= 1.0f);
                    CHECK(corner.y >= -1.0f);
                    CHECK(corner.y <= 1.0f);
                }
            }
            CHECK(glyph_quads > 100);
        }
    }
}

TEST_CASE("encounter HUD: bounded finite quads with diagnostics and capacity exhaustion") {
    Fixture f;
    std::vector<hue::HudQuad> quads(hue::kMaxHudQuads + 1);
    quads.back().ndc_min.x = 1234.0f;
    std::uint32_t count = 0;
    append_combat_hud(quads.data(), count, f.camera.camera(16.0f / 9.0f), *f.session, true, 16.6,
                      16384, 2, 2);
    CHECK(count > 0);
    CHECK(count < hue::kMaxHudQuads);
    for (std::uint32_t i = 0; i < count; ++i) {
        CHECK(std::isfinite(quads[i].ndc_min.x));
        CHECK(std::isfinite(quads[i].skew.y));
    }
    count = hue::kMaxHudQuads;
    append_combat_hud(quads.data(), count, {}, *f.session, true, 0, 0, 2, 2);
    CHECK(count == hue::kMaxHudQuads);
    CHECK(quads.back().ndc_min.x == 1234.0f);
    count = 0;
    append_combat_hud(quads.data(), count, {}, *f.session, 2, 16.6, 16384, 2, 2);
    CHECK(count < hue::kMaxHudQuads);
    // Diagnostics for a full eight-actor scene must still fit the quad budget.
    REQUIRE(f.session->stage_scene(Fixture::crowd(7)));
    f.reset();
    count = 0;
    append_combat_hud(quads.data(), count, f.camera.camera(16.0f / 9.0f), *f.session, 1, 16.6,
                      16384, 8, 8);
    CHECK(count < hue::kMaxHudQuads);
}

TEST_CASE("encounter: scene staging preserves live state and rejects invalid candidates") {
    Fixture f;
    f.session->start();
    f.tick();
    const auto before = f.session->position(0);
    SceneConfig candidate;
    candidate.actors[0].position = {-2, 0.05f, 3};
    REQUIRE(f.session->stage_scene(candidate));
    CHECK(f.session->scene_pending());
    CHECK(f.session->position(0).x == before.x);
    candidate.actors[0].position = {0, 0, 0};
    CHECK_FALSE(f.session->stage_scene(candidate));
    CHECK(f.session->scene().actors[0].position.x == doctest::Approx(-0.8));
    for (int i = 0; i < 20; ++i) {
        f.reset();
        CHECK(f.session->position(0).x == -2.0f);
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
    CHECK(f.session->perception().distance > 0.0f);
    CHECK(f.session->brain().light_cooldown() >= 0.0f);
    CHECK(f.session->brain().heavy_cooldown() >= 0.0f);
    f.reset();
    CHECK(f.session->brain().light_cooldown() == 0.0f);
    CHECK(f.session->brain().heavy_cooldown() == 0.0f);
    CHECK(f.session->perception().distance == 0.0f);
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
    CHECK(f.actors[0]->health() == 100.0f);
    CHECK(f.session->tuning().revision == 1);
    CHECK(f.actors[0]->cancel_open());
    auto bad = t;
    bad.cancel_open_scale = 0.1f;
    CHECK_FALSE(f.session->stage_tuning(bad));
    CHECK(f.session->tuning_pending());
    for (int cycle = 0; cycle < 20; ++cycle) {
        f.reset();
        CHECK(f.actors[0]->health() == 150.0f);
        CHECK(f.session->tuning().revision == 2);
        CHECK_FALSE(f.session->tuning_pending());
        f.session->start();
        f.session->buffer(hue::combat::Action::kAttackLight);
        for (int i = 0; i < 8; ++i)
            f.tick();
        CHECK_FALSE(f.actors[0]->cancel_open());
    }
}

TEST_CASE("encounter: several enemies must all fall, and the lock follows the living") {
    Fixture f;
    REQUIRE(f.session->stage_scene(Fixture::crowd(3)));
    f.reset();
    REQUIRE(f.session->actor_count() == 4);
    CHECK(f.physics.value().enabled_character_count() == 4);
    CHECK(f.session->enemies_alive() == 3);
    f.session->start();
    f.session->toggle_lock();
    f.tick();
    CHECK(f.session->lock_target() == 1); // nearest
    f.session->cycle_lock();
    f.tick();
    CHECK(f.session->lock_target() == 2);
    CHECK(f.session->focus_enemy() == 2);
    f.actors[2]->apply_damage(1000);
    f.tick();
    f.tick();
    CHECK(f.session->state() == EncounterState::kFighting);
    CHECK(f.session->enemies_alive() == 2);
    CHECK(f.session->locked());
    CHECK(f.session->lock_target() != 2); // moved to a living enemy
    f.actors[1]->apply_damage(1000);
    f.tick();
    CHECK(f.session->state() == EncounterState::kFighting);
    f.actors[3]->apply_damage(1000);
    f.tick();
    CHECK(f.session->state() == EncounterState::kWon);
    CHECK_FALSE(f.session->locked());
    // Cycling with nothing alive or after the result must not acquire.
    f.session->cycle_lock();
    f.tick();
    CHECK_FALSE(f.session->locked());
}

TEST_CASE("encounter: a crowd fight ends through real contact and never double-counts a swing") {
    Fixture f;
    REQUIRE(f.session->stage_scene(Fixture::crowd(3)));
    for (unsigned fight = 0; fight < 3; ++fight) {
        f.reset();
        f.session->start();
        f.session->toggle_lock();
        std::uint64_t last_activation = 0;
        std::uint32_t hits_this_swing = 0;
        for (unsigned tick = 0; tick < 5400 && f.session->state() == EncounterState::kFighting;
             ++tick) {
            if (fight % 2 == 0)
                f.session->buffer(hue::combat::Action::kAttackHeavy);
            const auto before = f.session->stats().hits[0];
            f.tick();
            if (f.actors[0]->activation() != last_activation) {
                last_activation = f.actors[0]->activation();
                hits_this_swing = 0;
            }
            hits_this_swing += f.session->stats().hits[0] - before;
            // One swing can hit each enemy at most once.
            CHECK(hits_this_swing <= 3);
        }
        REQUIRE(f.session->state() != EncounterState::kFighting);
        CHECK(f.session->stats().hits[0] + f.session->stats().hits[1] > 0);
        // Crowding enemies steer apart rather than stacking on one point.
        for (std::uint32_t a = 1; a < 4; ++a)
            for (std::uint32_t b = a + 1; b < 4; ++b)
                if (f.actors[a]->alive() && f.actors[b]->alive())
                    CHECK(hue::length(f.session->position(a) - f.session->position(b)) > 0.3f);
    }
}

TEST_CASE("encounter: scene changes reuse actors, controllers and props without growth") {
    Fixture f;
    SceneConfig big = Fixture::crowd(7);
    big.actors[3].rig = RigId::kKnight; // an enemy may use any approved rig
    big.prop_count = 2;
    big.props[0] = {{6.0f, 0.5f, 9.0f}, {0.5f, 0.5f, 0.5f}, 0.3f, {0.8f, 0.2f, 0.2f}};
    big.props[1] = {{-6.0f, 1.0f, 9.0f}, {1.0f, 1.0f, 0.25f}, 0.0f, {0.2f, 0.2f, 0.8f}};
    const SceneConfig small{};
    hue::AllocationSnapshot warmed{};
    for (int cycle = 0; cycle < 12; ++cycle) {
        const bool use_big = cycle % 2 == 0;
        REQUIRE(f.session->stage_scene(use_big ? big : small));
        f.reset();
        const std::uint32_t expected = use_big ? 8u : 2u;
        CHECK(f.session->actor_count() == expected);
        CHECK(f.physics.value().enabled_character_count() == expected);
        CHECK(f.physics.value().character_count() == 8); // parked, never destroyed
        CHECK(f.physics.value().prop_count() == (use_big ? 2u : 0u));
        CHECK(f.physics.value().static_body_count() == (use_big ? 3u : 1u));
        CHECK(f.session->scene_revision() == static_cast<std::uint32_t>(cycle + 2)); // fixture applied 1
        if (use_big) {
            // The prop is real collision: a ray straight down lands on its top.
            const auto hit = f.physics.value().cast_ray({6.0f, 5.0f, 9.0f}, {0, -1, 0}, 10.0f);
            REQUIRE(hit.hit);
            CHECK(hit.position.y == doctest::Approx(1.0f).epsilon(0.02));
        }
        f.session->start();
        for (int i = 0; i < 30; ++i)
            f.tick();
        const auto current = hue::allocation_snapshot();
        if (cycle == 3)
            warmed = current;
        if (cycle > 3 && cycle % 2 == 1) {
            for (const auto tag : {hue::MemoryTag::kAnimation, hue::MemoryTag::kAssets,
                                   hue::MemoryTag::kPhysics, hue::MemoryTag::kGame}) {
                const auto index = static_cast<std::size_t>(tag);
                CHECK(current.tags[index].current_bytes == warmed.tags[index].current_bytes);
                CHECK(current.tags[index].live_allocations == warmed.tags[index].live_allocations);
            }
        }
    }
}

TEST_CASE("encounter: presentation events are bounded and describe the fight") {
    Fixture f;
    EncounterEvent events[kMaxEncounterEvents];
    CHECK(f.session->drain_events(events, kMaxEncounterEvents) == 0);
    f.session->start();
    REQUIRE(f.session->drain_events(events, kMaxEncounterEvents) == 1);
    CHECK(events[0].type == EncounterEventType::kFightStart);
    f.session->buffer(hue::combat::Action::kAttackHeavy);
    bool swing = false, hit = false;
    for (int i = 0; i < 120; ++i) {
        f.tick();
        const auto count = f.session->drain_events(events, kMaxEncounterEvents);
        for (std::uint32_t e = 0; e < count; ++e) {
            swing = swing || (events[e].type == EncounterEventType::kSwingHeavy && events[e].actor == 0);
            hit = hit || (events[e].type == EncounterEventType::kHitHeavy && events[e].actor == 1);
            CHECK(std::isfinite(events[e].position.x));
        }
    }
    CHECK(swing);
    CHECK(hit);
    // Never drained: the queue saturates instead of growing or overwriting memory.
    f.reset();
    f.session->start();
    for (int i = 0; i < 600; ++i) {
        f.session->toggle_lock();
        f.tick();
    }
    CHECK(f.session->drain_events(events, kMaxEncounterEvents) == kMaxEncounterEvents);
    CHECK(f.session->drain_events(events, kMaxEncounterEvents) == 0);
    CHECK(f.session->drain_events(nullptr, 4) == 0);
    f.actors[1]->apply_damage(1000);
    f.tick();
    const auto count = f.session->drain_events(events, kMaxEncounterEvents);
    REQUIRE(count > 0);
    CHECK(events[count - 1].type == EncounterEventType::kVictory);
}
