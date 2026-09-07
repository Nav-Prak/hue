#include <doctest/doctest.h>

#include "hue/combat/combat.h"

using hue::combat::Action;
using hue::combat::State;
using hue::combat::StateMachine;
using hue::combat::TickFlags;

namespace {

constexpr float kTick = 1.0f / 60.0f;

// Advances n ticks with no flags set.
void run_ticks(StateMachine& machine, int n) {
    for (int i = 0; i < n; ++i) {
        (void)machine.update(kTick, {});
    }
}

TickFlags finished() {
    TickFlags flags;
    flags.clip_finished = true;
    return flags;
}

TickFlags hit(bool fatal = false) {
    TickFlags flags;
    flags.hit_taken = true;
    flags.fatal = fatal;
    return flags;
}

} // namespace

TEST_CASE("combat: locomotion starts buffered actions immediately") {
    StateMachine machine;
    CHECK(machine.state() == State::kLocomotion);

    machine.buffer(Action::kAttackLight);
    CHECK(machine.update(kTick, {}) == State::kAttackLight);
    CHECK(machine.buffered_action() == Action::kNone); // consumed

    // Finished attack with nothing buffered returns to locomotion.
    CHECK(machine.update(kTick, finished()) == State::kLocomotion);

    machine.buffer(Action::kAttackHeavy);
    CHECK(machine.update(kTick, {}) == State::kAttackHeavy);
}

TEST_CASE("combat: buffered input expires after the tuned window") {
    hue::combat::Tuning tuning;
    tuning.input_buffer_seconds = 0.1f;
    StateMachine machine(tuning);

    // Press during an attack whose cancel window never opens: the buffer
    // must expire before the clip ends and not fire.
    machine.buffer(Action::kAttackLight);
    (void)machine.update(kTick, {});
    CHECK(machine.state() == State::kAttackLight);
    machine.handle_event("cancel_close");

    machine.buffer(Action::kDodge);
    run_ticks(machine, 12); // 0.2s > 0.1s window
    CHECK(machine.state() == State::kAttackLight);
    CHECK(machine.buffered_action() == Action::kNone); // expired
    CHECK(machine.update(kTick, finished()) == State::kLocomotion);
}

TEST_CASE("combat: dodge cancels an attack while the cancel window is open") {
    StateMachine machine;
    machine.buffer(Action::kAttackHeavy);
    (void)machine.update(kTick, {});
    REQUIRE(machine.state() == State::kAttackHeavy);

    // Windup is cancelable until the clip's cancel_close event.
    CHECK(machine.cancel_open());
    machine.buffer(Action::kDodge);
    CHECK(machine.update(kTick, {}) == State::kDodge);
}

TEST_CASE("combat: attacks chain only in recovery, dodge cancels any window") {
    StateMachine machine;
    machine.buffer(Action::kAttackLight);
    (void)machine.update(kTick, {});
    REQUIRE(machine.state() == State::kAttackLight);

    // Committed part of the swing: cancel window closed, hit not landed.
    machine.handle_event("cancel_close");
    machine.buffer(Action::kAttackLight);
    run_ticks(machine, 3);
    CHECK(machine.state() == State::kAttackLight); // no chain yet

    // Active frames pass; recovery opens the cancel window again.
    machine.handle_event("hit_begin");
    CHECK(machine.hitbox_active());
    machine.handle_event("hit_end");
    CHECK_FALSE(machine.hitbox_active());
    machine.handle_event("cancel_open");

    // The still-buffered light attack now chains into a second swing.
    CHECK(machine.update(kTick, {}) == State::kAttackLight);
    CHECK(machine.state_seconds() == doctest::Approx(0.0f)); // fresh state
}

TEST_CASE("combat: dodge commits fully and grants i-frames between events") {
    StateMachine machine;
    machine.buffer(Action::kDodge);
    (void)machine.update(kTick, {});
    REQUIRE(machine.state() == State::kDodge);
    CHECK_FALSE(machine.invulnerable());

    machine.handle_event("iframe_begin");
    CHECK(machine.invulnerable());

    // A hit during i-frames is ignored entirely.
    CHECK(machine.update(kTick, hit()) == State::kDodge);

    machine.handle_event("iframe_end");
    CHECK_FALSE(machine.invulnerable());

    // Mid-dodge presses do not cancel the roll; they fire when it ends.
    machine.buffer(Action::kAttackHeavy);
    run_ticks(machine, 2);
    CHECK(machine.state() == State::kDodge);
    CHECK(machine.update(kTick, finished()) == State::kAttackHeavy);
}

TEST_CASE("combat: hits stagger, clear buffered intent, and can kill") {
    StateMachine machine;
    machine.buffer(Action::kAttackLight);
    (void)machine.update(kTick, {});
    REQUIRE(machine.state() == State::kAttackLight);

    machine.buffer(Action::kAttackHeavy); // stale intent
    CHECK(machine.update(kTick, hit()) == State::kHitReact);
    CHECK(machine.buffered_action() == Action::kNone); // cleared by stagger

    // Reacting ends into locomotion when nothing new was pressed.
    CHECK(machine.update(kTick, finished()) == State::kLocomotion);

    // Fatal hit is terminal and absorbs every input and event afterwards.
    CHECK(machine.update(kTick, hit(true)) == State::kDeath);
    CHECK_FALSE(machine.alive());
    machine.buffer(Action::kDodge);
    CHECK(machine.buffered_action() == Action::kNone);
    CHECK(machine.update(kTick, finished()) == State::kDeath);
    CHECK(machine.update(kTick, hit()) == State::kDeath);
}

TEST_CASE("combat: event flags reset on every state change") {
    StateMachine machine;
    machine.buffer(Action::kAttackLight);
    (void)machine.update(kTick, {});
    machine.handle_event("hit_begin");
    CHECK(machine.hitbox_active());

    // The swing ends mid-active-frames (defensive: malformed event track);
    // leaving the state must drop the hitbox.
    (void)machine.update(kTick, finished());
    CHECK(machine.state() == State::kLocomotion);
    CHECK_FALSE(machine.hitbox_active());

    // hit_begin outside an attack state never raises the hitbox flag.
    machine.handle_event("hit_begin");
    CHECK_FALSE(machine.hitbox_active());

    // iframe_begin outside a dodge never grants invulnerability.
    machine.handle_event("iframe_begin");
    CHECK_FALSE(machine.invulnerable());
}
