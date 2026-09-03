#include <doctest/doctest.h>

#include "hue/ecs/ecs.h"

namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float x = 0.0f;
    float y = 0.0f;
};

struct Health {
    int value = 100;
};

} // namespace

TEST_CASE("ecs: entity handles recycle slots with fresh generations") {
    hue::ecs::EntityRegistry registry;
    auto first = registry.create();
    REQUIRE(first);
    auto second = registry.create();
    REQUIRE(second);
    CHECK(registry.alive_count() == 2);
    CHECK(hue::ecs::entity_index(first.value()) != hue::ecs::entity_index(second.value()));

    REQUIRE(registry.destroy(first.value()));
    CHECK_FALSE(registry.alive(first.value()));
    CHECK(registry.alive(second.value()));

    // The freed slot is reused, but the stale handle stays dead.
    auto third = registry.create();
    REQUIRE(third);
    CHECK(hue::ecs::entity_index(third.value()) == hue::ecs::entity_index(first.value()));
    CHECK(hue::ecs::entity_generation(third.value()) !=
          hue::ecs::entity_generation(first.value()));
    CHECK_FALSE(registry.alive(first.value()));
    CHECK(registry.alive(third.value()));

    CHECK_FALSE(registry.destroy(first.value())); // double destroy rejected
    CHECK_FALSE(registry.alive(hue::ecs::kInvalidEntity));
}

TEST_CASE("ecs: sparse-set pool add/get/remove keeps dense arrays packed") {
    hue::ecs::EntityRegistry registry;
    hue::ecs::ComponentPool<Position> positions;

    hue::ecs::Entity entities[4];
    for (auto& entity : entities) {
        auto created = registry.create();
        REQUIRE(created);
        entity = created.value();
        REQUIRE(positions.add(entity, {static_cast<float>(hue::ecs::entity_index(entity)), 0.0f}));
    }
    CHECK(positions.size() == 4);

    // Swap-remove from the middle: dense stays packed, lookups stay correct.
    CHECK(positions.remove(entities[1]));
    CHECK(positions.size() == 3);
    CHECK_FALSE(positions.has(entities[1]));
    for (const auto entity : {entities[0], entities[2], entities[3]}) {
        REQUIRE(positions.has(entity));
        CHECK(positions.get(entity)->x ==
              doctest::Approx(static_cast<float>(hue::ecs::entity_index(entity))));
    }
    CHECK_FALSE(positions.remove(entities[1])); // second remove is a no-op

    // Re-adding overwrites in place rather than duplicating.
    REQUIRE(positions.add(entities[0], {42.0f, 0.0f}));
    CHECK(positions.size() == 3);
    CHECK(positions.get(entities[0])->x == doctest::Approx(42.0f));
}

TEST_CASE("ecs: stale handles do not reach the recycled slot's component") {
    hue::ecs::EntityRegistry registry;
    hue::ecs::ComponentPool<Health> health;

    auto original = registry.create();
    REQUIRE(original);
    REQUIRE(health.add(original.value(), {50}));
    REQUIRE(registry.destroy(original.value()));
    CHECK(health.remove(original.value())); // caller cleans up on destroy

    auto recycled = registry.create(); // same index, new generation
    REQUIRE(recycled);
    REQUIRE(health.add(recycled.value(), {100}));

    CHECK(health.get(original.value()) == nullptr);
    CHECK_FALSE(health.has(original.value()));
    CHECK(health.get(recycled.value())->value == 100);
}

TEST_CASE("ecs: multi-pool query visits exactly the entities with all components") {
    hue::ecs::EntityRegistry registry;
    hue::ecs::ComponentPool<Position> positions;
    hue::ecs::ComponentPool<Velocity> velocities;

    hue::ecs::Entity moving[3];
    for (auto& entity : moving) {
        auto created = registry.create();
        REQUIRE(created);
        entity = created.value();
        REQUIRE(positions.add(entity, {0.0f, 0.0f}));
        REQUIRE(velocities.add(entity, {1.0f, 2.0f}));
    }
    // Two entities with position only: the query must skip them.
    for (int i = 0; i < 2; ++i) {
        auto created = registry.create();
        REQUIRE(created);
        REQUIRE(positions.add(created.value(), {9.0f, 9.0f}));
    }

    int visited = 0;
    hue::ecs::for_each(velocities, positions,
                       [&](hue::ecs::Entity, Velocity& velocity, Position& position) {
                           position.x += velocity.x;
                           position.y += velocity.y;
                           ++visited;
                       });
    CHECK(visited == 3);
    for (const auto entity : moving) {
        CHECK(positions.get(entity)->x == doctest::Approx(1.0f));
        CHECK(positions.get(entity)->y == doctest::Approx(2.0f));
    }
}

TEST_CASE("ecs: deferred adds and removes apply on flush, not during iteration") {
    hue::ecs::World world;
    auto pool = world.pool<Health>();
    REQUIRE(pool);

    hue::ecs::Entity entities[3];
    for (auto& entity : entities) {
        auto created = world.create();
        REQUIRE(created);
        entity = created.value();
        REQUIRE(world.add(entity, Health{10}));
    }

    // During iteration: queue a remove for every visited entity and an add
    // for a brand-new one. Pool contents must not change mid-walk.
    auto newcomer = world.create();
    REQUIRE(newcomer);
    std::size_t visited = 0;
    hue::ecs::for_each(*pool.value(), [&](hue::ecs::Entity entity, Health&) {
        ++visited;
        REQUIRE(pool.value()->deferred_remove(entity));
        if (visited == 1) {
            REQUIRE(pool.value()->deferred_add(newcomer.value(), Health{77}));
        }
    });
    CHECK(visited == 3);
    CHECK(pool.value()->size() == 3); // still unchanged

    REQUIRE(world.flush());
    CHECK(pool.value()->size() == 1);
    CHECK(pool.value()->get(newcomer.value())->value == 77);
}

TEST_CASE("ecs: deferred entity destroy removes components from every pool") {
    hue::ecs::World world;
    auto positions = world.pool<Position>();
    auto health = world.pool<Health>();
    REQUIRE(positions);
    REQUIRE(health);

    auto doomed = world.create();
    auto survivor = world.create();
    REQUIRE(doomed);
    REQUIRE(survivor);
    REQUIRE(world.add(doomed.value(), Position{1.0f, 1.0f}));
    REQUIRE(world.add(doomed.value(), Health{5}));
    REQUIRE(world.add(survivor.value(), Position{2.0f, 2.0f}));

    REQUIRE(world.destroy_deferred(doomed.value()));
    CHECK(world.alive(doomed.value())); // still alive until flush
    REQUIRE(world.flush());

    CHECK_FALSE(world.alive(doomed.value()));
    CHECK(world.alive_count() == 1);
    CHECK(positions.value()->get(doomed.value()) == nullptr);
    CHECK(health.value()->get(doomed.value()) == nullptr);
    CHECK(positions.value()->get(survivor.value())->x == doctest::Approx(2.0f));

    // A deferred add racing a deferred destroy: the destroy wins.
    auto racer = world.create();
    REQUIRE(racer);
    REQUIRE(health.value()->deferred_add(racer.value(), Health{1}));
    REQUIRE(world.destroy_deferred(racer.value()));
    REQUIRE(world.flush());
    CHECK_FALSE(world.alive(racer.value()));
    CHECK(health.value()->get(racer.value()) == nullptr);
}
