#include <doctest/doctest.h>

#include "hue/combat/ai.h"
#include "hue/core/math.h"

#include <string>

using hue::combat::Action;
using hue::combat::AiState;
using hue::combat::Brain;
using hue::combat::Perception;
using hue::combat::State;

namespace {

constexpr float kTick = 1.0f / 60.0f;

Perception melee_sight() {
    Perception sense;
    sense.target_alive = true;
    sense.self_alive = true;
    sense.line_of_sight = true;
    sense.distance = 1.2f;
    sense.to_target = {1.2f, 0.0f, 0.0f};
    sense.self_state = State::kLocomotion;
    return sense;
}

Perception far_sight(float distance) {
    Perception sense = melee_sight();
    sense.distance = distance;
    sense.to_target = {distance, 0.0f, 0.0f};
    return sense;
}

} // namespace

TEST_CASE("ai: names cover every state") {
    CHECK(std::string(hue::combat::ai_state_name(AiState::kIdle)) == "idle");
    CHECK(std::string(hue::combat::ai_state_name(AiState::kAggro)) == "aggro");
    CHECK(std::string(hue::combat::ai_state_name(AiState::kApproach)) == "approach");
    CHECK(std::string(hue::combat::ai_state_name(AiState::kAttack)) == "attack");
    CHECK(std::string(hue::combat::ai_state_name(AiState::kRecover)) == "recover");
}

TEST_CASE("ai: out of range or without LOS stays idle") {
    Brain brain;
    Perception sense = far_sight(20.0f);
    const auto far = brain.update(kTick, sense);
    CHECK(brain.state() == AiState::kIdle);
    CHECK(far.action == Action::kNone);
    CHECK(far.face_target == false);
    CHECK(length_squared(far.planar_velocity) == 0.0f);

    sense = melee_sight();
    sense.line_of_sight = false;
    (void)brain.update(kTick, sense);
    CHECK(brain.state() == AiState::kIdle);
}

TEST_CASE("ai: notice then approach then swing in melee") {
    Brain brain({}, 7);
    Perception sense = melee_sight();

    const auto noticed = brain.update(kTick, sense);
    CHECK(brain.state() == AiState::kAggro);
    CHECK(brain.entered());
    CHECK(noticed.face_target);
    CHECK(noticed.action == Action::kNone);

    bool approached = false;
    hue::combat::AiCommand command{};
    for (int i = 0; i < 30; ++i) {
        command = brain.update(kTick, sense);
        if (brain.state() == AiState::kApproach) {
            approached = true;
            break;
        }
    }
    REQUIRE(approached);

    bool swung = command.action != Action::kNone;
    for (int i = 0; i < 5 && !swung; ++i) {
        command = brain.update(kTick, sense);
        swung = command.action != Action::kNone;
    }
    REQUIRE(swung);
    CHECK((command.action == Action::kAttackLight || command.action == Action::kAttackHeavy));
    CHECK(brain.state() == AiState::kAttack);
    CHECK(brain.last_attack() == command.action);
    CHECK(length_squared(command.planar_velocity) == 0.0f);
}

TEST_CASE("ai: recover strafes then returns to approach") {
    Brain brain({}, 3);
    Perception sense = melee_sight();

    hue::combat::AiCommand command{};
    for (int i = 0; i < 40; ++i) {
        command = brain.update(kTick, sense);
        if (command.action != Action::kNone) {
            break;
        }
    }
    REQUIRE(brain.state() == AiState::kAttack);

    sense.self_state = State::kAttackLight;
    for (int i = 0; i < 8; ++i) {
        command = brain.update(kTick, sense);
        CHECK(command.action == Action::kNone);
        CHECK(length_squared(command.planar_velocity) == 0.0f);
    }
    CHECK(brain.state() == AiState::kAttack);

    sense.self_state = State::kLocomotion;
    command = brain.update(kTick, sense);
    CHECK(brain.state() == AiState::kRecover);
    CHECK(length_squared(command.planar_velocity) > 1.0f);

    bool back_to_approach = false;
    for (int i = 0; i < 50; ++i) {
        command = brain.update(kTick, sense);
        if (brain.state() == AiState::kApproach || brain.state() == AiState::kAttack) {
            back_to_approach = true;
            break;
        }
        CHECK(brain.state() == AiState::kRecover);
    }
    CHECK(back_to_approach);
}

TEST_CASE("ai: chase uses run speed outside melee") {
    Brain brain;
    Perception sense = far_sight(8.0f);
    (void)brain.update(kTick, sense);
    REQUIRE(brain.state() == AiState::kAggro);

    hue::combat::AiCommand command{};
    for (int i = 0; i < 30; ++i) {
        command = brain.update(kTick, sense);
        if (brain.state() == AiState::kApproach) {
            break;
        }
    }
    REQUIRE(brain.state() == AiState::kApproach);
    command = brain.update(kTick, sense);
    CHECK(command.action == Action::kNone);
    CHECK(doctest::Approx(length(command.planar_velocity)).epsilon(0.01f) == 3.2f);
    CHECK(command.planar_velocity.x > 0.0f);
}

TEST_CASE("ai: dead target or self returns to idle") {
    Brain brain;
    Perception sense = melee_sight();
    (void)brain.update(kTick, sense);
    CHECK(brain.state() == AiState::kAggro);

    sense.target_alive = false;
    const auto command = brain.update(kTick, sense);
    CHECK(brain.state() == AiState::kIdle);
    CHECK(command.face_target == false);
    CHECK(length_squared(command.planar_velocity) == 0.0f);
}

TEST_CASE("ai: hit-react roots without dropping aggro") {
    Brain brain;
    Perception sense = melee_sight();
    (void)brain.update(kTick, sense);
    REQUIRE(brain.state() == AiState::kAggro);

    sense.self_state = State::kHitReact;
    const auto command = brain.update(kTick, sense);
    CHECK(brain.state() == AiState::kAggro);
    CHECK(command.face_target);
    CHECK(command.action == Action::kNone);
    CHECK(length_squared(command.planar_velocity) == 0.0f);
}

TEST_CASE("ai: lost LOS past memory deaggros") {
    hue::combat::AiTuning tuning;
    tuning.los_memory_seconds = 0.2f;
    Brain brain(tuning, 1);
    Perception sense = melee_sight();
    (void)brain.update(kTick, sense);
    REQUIRE(brain.state() == AiState::kAggro);

    sense.line_of_sight = false;
    for (int i = 0; i < 20; ++i) {
        (void)brain.update(kTick, sense);
    }
    CHECK(brain.state() == AiState::kIdle);
}

TEST_CASE("ai: two attacks remain available across prolonged repeated combat") {
    Brain brain({}, 2026);
    auto sense = melee_sight();
    unsigned light = 0, heavy = 0;
    int swing_ticks = 0;
    for (int tick = 0; tick < 7200; ++tick) {
        if (swing_ticks > 0)
            --swing_ticks;
        else
            sense.self_state = State::kLocomotion;
        const auto command = brain.update(kTick, sense);
        if (command.action == Action::kAttackLight) {
            ++light;
            swing_ticks = 64;
            sense.self_state = State::kAttackLight;
        } else if (command.action == Action::kAttackHeavy) {
            ++heavy;
            swing_ticks = 98;
            sense.self_state = State::kAttackHeavy;
        }
    }
    CHECK(light > 10);
    CHECK(heavy > 10);
}

TEST_CASE("ai: heavy whiff has a tunable recovery opportunity after animation ends") {
    unsigned recover_ticks[2]{};
    for (unsigned trial = 0; trial < 2; ++trial) {
        hue::combat::AiTuning tuning;
        tuning.light_weight = 0; // deterministic heavy opener
        tuning.recover_seconds = trial == 0 ? 0.65f : 0.85f;
        Brain brain(tuning, 2026);
        auto sense = melee_sight();
        hue::combat::AiCommand command;
        for (int tick = 0; tick < 120 && command.action == Action::kNone; ++tick)
            command = brain.update(kTick, sense);
        REQUIRE(command.action == Action::kAttackHeavy);
        sense.self_state = State::kAttackHeavy;
        sense.distance = 3; // target evades; no contact injected into brain
        for (int tick = 0; tick < 98; ++tick)
            CHECK(brain.update(kTick, sense).action == Action::kNone);
        sense.self_state = State::kLocomotion;
        (void)brain.update(kTick, sense);
        REQUIRE(brain.state() == AiState::kRecover);
        for (unsigned tick = 0; tick < 120 && brain.state() == AiState::kRecover; ++tick) {
            CHECK(brain.update(kTick, sense).action == Action::kNone);
            ++recover_ticks[trial];
        }
    }
    CHECK(recover_ticks[0] >= 39);
    CHECK(recover_ticks[1] >= recover_ticks[0] + 11);
    CHECK(recover_ticks[1] <= recover_ticks[0] + 13);
}

TEST_CASE("ai: ignores non-finite dt") {
    Brain brain;
    Perception sense = melee_sight();
    const auto command = brain.update(0.0f, sense);
    CHECK(brain.state() == AiState::kIdle);
    CHECK(command.action == Action::kNone);
}
