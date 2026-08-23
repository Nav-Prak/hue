#include <doctest/doctest.h>

#include "hue/anim/animation.h"

#include <cstring>
#include <utility>

namespace {

hue::asset::SkinnedMeshData make_test_character() {
    hue::asset::SkinnedMeshData data;

    hue::asset::Joint root;
    std::strcpy(root.name, "root");
    CHECK(data.joints.push_back(root));

    hue::asset::Joint child;
    child.parent = 0;
    child.rest_translation = {0.0f, 1.0f, 0.0f};
    child.inverse_bind = hue::Mat4::translation({0.0f, -1.0f, 0.0f});
    std::strcpy(child.name, "child");
    CHECK(data.joints.push_back(child));

    hue::asset::AnimationClip idle;
    std::strcpy(idle.name, "idle");
    idle.duration = 1.0f;
    hue::asset::AnimationChannel idle_translation;
    idle_translation.joint = 0;
    idle_translation.path = hue::asset::AnimationPath::kTranslation;
    const float idle_times[] = {0.0f, 1.0f};
    const float idle_values[] = {0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f};
    CHECK(idle_translation.times.append(idle_times, 2));
    CHECK(idle_translation.values.append(idle_values, 6));
    CHECK(idle.channels.push_back(std::move(idle_translation)));
    CHECK(data.clips.push_back(std::move(idle)));

    hue::asset::AnimationClip attack;
    std::strcpy(attack.name, "attack");
    attack.duration = 1.0f;
    hue::asset::AnimationChannel attack_translation;
    attack_translation.joint = 0;
    attack_translation.path = hue::asset::AnimationPath::kTranslation;
    const float attack_times[] = {0.0f, 1.0f};
    const float attack_values[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f};
    CHECK(attack_translation.times.append(attack_times, 2));
    CHECK(attack_translation.values.append(attack_values, 6));
    CHECK(attack.channels.push_back(std::move(attack_translation)));
    CHECK(data.clips.push_back(std::move(attack)));
    return data;
}

} // namespace

TEST_CASE("animation: samples TRS and builds hierarchical skin matrices") {
    auto data = make_test_character();
    auto arena = hue::LinearArena::create(32 * 1024, hue::MemoryTag::kAnimation);
    REQUIRE(arena);
    auto pose = hue::anim::allocate_pose(arena.value(), 2);
    REQUIRE(pose);
    REQUIRE(hue::anim::sample_clip(data.joints.data(), 2, data.clips[0], 0.25f, false,
                                   pose.value().local));
    CHECK(pose.value().local[0].translation.x == doctest::Approx(0.5f));
    REQUIRE(hue::anim::build_skinning_matrices(
        data.joints.data(), 2, pose.value().local, pose.value().global, pose.value().skinning));
    CHECK(pose.value().global[1].at(0, 3) == doctest::Approx(0.5f));
    CHECK(pose.value().global[1].at(1, 3) == doctest::Approx(1.0f));
    CHECK(pose.value().skinning[1].at(1, 3) == doctest::Approx(0.0f));
}

TEST_CASE("animation: cross pose and 1D clip blends clamp correctly") {
    auto data = make_test_character();
    auto arena = hue::LinearArena::create(32 * 1024, hue::MemoryTag::kAnimation);
    REQUIRE(arena);
    auto out = arena.value().allocate<hue::anim::LocalTransform>(2);
    REQUIRE(out);
    REQUIRE(hue::anim::sample_blend_1d(data.joints.data(), 2, data.clips[0], 0.5f, 0.0f,
                                       data.clips[1], 0.5f, 4.0f, 2.0f, false,
                                       arena.value(), out.value()));
    CHECK(out.value()[0].translation.x == doctest::Approx(0.5f));
    CHECK(out.value()[0].translation.z == doctest::Approx(1.0f));
}

TEST_CASE("animation events: bounded JSON parses and validates against clips") {
    constexpr char json[] =
        R"({"events":[{"clip":"attack","time":0.2,"name":"hit_begin"},{"clip":"attack","time":0.6,"name":"hit_end"}]})";
    auto parsed = hue::anim::parse_animation_events(json, sizeof(json) - 1);
    REQUIRE(parsed);
    CHECK(parsed.value().events.size() == 2);
    CHECK(std::strcmp(parsed.value().events[0].name, "hit_begin") == 0);
    auto data = make_test_character();
    CHECK(hue::anim::validate_animation_events(data, parsed.value()));

    constexpr char bad[] = R"({"events":[{"clip":"missing","time":0.2,"name":"hit"}]})";
    auto bad_track = hue::anim::parse_animation_events(bad, sizeof(bad) - 1);
    REQUIRE(bad_track);
    CHECK_FALSE(hue::anim::validate_animation_events(data, bad_track.value()));
}

TEST_CASE("animator: crossfades, emits crossed events, and finishes one-shots") {
    auto data = make_test_character();
    constexpr char json[] =
        R"({"events":[{"clip":"attack","time":0.2,"name":"hit_begin"},{"clip":"attack","time":0.6,"name":"hit_end"}]})";
    auto events = hue::anim::parse_animation_events(json, sizeof(json) - 1);
    REQUIRE(events);
    hue::anim::Animator animator;
    REQUIRE(animator.bind(data));
    REQUIRE(animator.play(1, 0.2f, false));
    auto arena = hue::LinearArena::create(32 * 1024, hue::MemoryTag::kAnimation);
    REQUIRE(arena);
    const hue::anim::AnimationEvent* fired[4]{};
    auto frame = animator.update(0.25f, arena.value(), &events.value(), fired, 4);
    REQUIRE(frame);
    CHECK(frame.value().joint_count == 2);
    CHECK(frame.value().fired_event_count == 1);
    CHECK(std::strcmp(fired[0]->name, "hit_begin") == 0);
    arena.value().reset();
    REQUIRE(animator.update(0.75f, arena.value(), &events.value(), fired, 4));
    CHECK(animator.finished());
}

TEST_CASE("animation events: malformed and excessive inputs are rejected") {
    constexpr char malformed[] = R"({"events":[{"clip":"attack","time":-1,"name":"hit"}]})";
    CHECK_FALSE(hue::anim::parse_animation_events(malformed, sizeof(malformed) - 1));
    CHECK_FALSE(hue::anim::parse_animation_events(nullptr, 0));
    static char oversized[hue::anim::kMaxEventFileBytes + 1]{};
    CHECK_FALSE(hue::anim::parse_animation_events(oversized, sizeof(oversized)));
}
