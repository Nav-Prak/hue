// tests/game/test_config.cpp
#include "config_json.h"
#include "scene_config.h"
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <string>
namespace {
constexpr char kScene[] =
    R"({"version":1,"arena":"training_arena","player":"kaykit_knight","enemy":"kaykit_barbarian","player_spawn":[-0.8,0.05,3],"enemy_spawn":[0.8,0.05,3],"player_yaw":-90,"enemy_yaw":90})";
}
TEST_CASE("config: strict bounded JSON rejects ambiguous or unsupported syntax") {
    for (const char* text :
         {"", "null", "[]", "{\"a\":1,\"a\":2}", "{\"a\":01}", "{\"a\":+1}", "{\"a\":1.}",
          "{\"a\":1e}", "{\"a\":1e999}", "{\"a\":NaN}", "{\"a\":true}", "{\"a\":{}}", "{\"a\":[]}",
          "{\"a\":[1,2,3,4,5]}", "{\"a\":1,}", "{}garbage", "{\"a\":\"\\u0061\"}"}) {
        INFO(text);
        CHECK_FALSE(parse_config_json(text, std::strlen(text)));
    }
    CHECK_FALSE(parse_config_json(nullptr, 10));
    CHECK_FALSE(parse_config_json(kScene, kConfigMaxBytes + 1));
    std::string too_long = "{\"" + std::string(48, 'x') + "\":1}";
    CHECK_FALSE(parse_config_json(too_long.data(), too_long.size()));
    std::string many = "{";
    for (int i = 0; i < 33; ++i)
        many += (i ? "," : "") + std::string("\"f") + std::to_string(i) + "\":0";
    many += "}";
    CHECK_FALSE(parse_config_json(many.data(), many.size()));
}
TEST_CASE("scene: approved schema parses and every truncation is rejected") {
    auto parsed = parse_scene(kScene, sizeof(kScene) - 1);
    REQUIRE(parsed);
    CHECK(parsed.value().spawns[0].x == doctest::Approx(-0.8));
    CHECK(parsed.value().yaw[0] == doctest::Approx(-hue::kPi / 2));
    for (std::size_t i = 0; i < sizeof(kScene) - 1; ++i)
        CHECK_FALSE(parse_scene(kScene, i));
}
TEST_CASE("scene: rejects paths unknown keys versions collision and nonfinite positions") {
    const auto replace = [](const char* before, const char* after) {
        std::string text = kScene;
        text.replace(text.find(before), std::strlen(before), after);
        return text;
    };
    for (const auto& text :
         {replace("training_arena", "../../file"), replace("\"version\":1", "\"version\":2"),
          replace("\"version\":1", "\"version\":[1]"), replace("player_yaw", "unknown"),
          replace("[-0.8,0.05,3]", "[0.8,0.05,3]"), replace("[-0.8,0.05,3]", "[0,0.05,0]"),
          replace("[-0.8,0.05,3]", "[-4,0.05,3]"),
          replace("\"enemy_yaw\":90", "\"enemy_yaw\":181")}) {
        INFO(text);
        CHECK_FALSE(parse_scene(text.data(), text.size()));
    }
    SceneConfig scene;
    scene.spawns[0].x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(valid_scene(scene));
}
