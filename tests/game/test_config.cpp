// tests/game/test_config.cpp
#include "config_json.h"
#include "runtime_files.h"
#include "scene_config.h"
#include <cstdio>
#include <cstring>
#include <doctest/doctest.h>
#include <limits>
#include <memory>
#include <string>
#include <vector>
namespace {
constexpr char kScene[] =
    R"({"version":2,"arena":"training_arena","sun_direction":[0.4,-1,0.3],"sun_color":[1,0.96,0.9,3],"ambient":[0.05,0.055,0.07],"shadows":1,"actors":[{"rig":"kaykit_knight","role":"player","position":[-0.8,0.05,3],"yaw":-90},{"rig":"kaykit_barbarian","role":"enemy","position":[0.8,0.05,3],"yaw":90}],"props":[{"shape":"box","position":[6,0.5,9],"size":[1,1,1],"yaw":30,"color":[0.7,0.4,0.2]}],"lights":[{"position":[0,4.2,0],"radius":10,"color":[1,0.55,0.25],"intensity":14}]})";
std::string replaced(const char* before, const char* after) {
    std::string text = kScene;
    const auto at = text.find(before);
    REQUIRE(at != std::string::npos);
    text.replace(at, std::strlen(before), after);
    return text;
}
std::vector<char> shipped(const char* name) {
    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%s.json", HUE_TEST_CONFIG_DIR, name);
    std::vector<char> bytes(kDocumentMaxBytes);
    const auto size = read_bounded_file(path, bytes.data(), bytes.size());
    REQUIRE(size);
    bytes.resize(size.value());
    return bytes;
}
} // namespace
TEST_CASE("config: strict bounded JSON rejects ambiguous or unsupported syntax") {
    for (const char* text :
         {"", "null", "[]", "{\"a\":1,\"a\":2}", "{\"a\":01}", "{\"a\":+1}", "{\"a\":1.}",
          "{\"a\":1e}", "{\"a\":1e999}", "{\"a\":NaN}", "{\"a\":true}", "{\"a\":{}}", "{\"a\":[]}",
          "{\"a\":[1,2,3,4,5]}", "{\"a\":1,}", "{}garbage", "{\"a\":\"\\u0061\"}",
          "{\"a\":[{\"b\":1}]}"}) {
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
TEST_CASE("config document: record lists parse within every cap and reject nesting") {
    auto document = std::make_unique<ConfigDocument>();
    const char* good = R"({"a":1,"items":[{"x":[1,2],"n":"v"},{}],"empty":[],"b":[3,4]})";
    REQUIRE(parse_config_document(good, std::strlen(good), *document));
    CHECK(document->root.count == 2);
    REQUIRE(document->list_count == 2);
    const auto* items = document->list("items");
    REQUIRE(items);
    CHECK(items->count == 2);
    CHECK(items->items[0].count == 2);
    REQUIRE(items->items[0].find("x"));
    CHECK(items->items[0].find("x")->numbers[1] == 2.0f);
    CHECK(std::strcmp(items->items[0].find("n")->string, "v") == 0);
    CHECK(items->items[1].count == 0);
    REQUIRE(document->list("empty"));
    CHECK(document->list("empty")->count == 0);
    CHECK(document->list("b") == nullptr); // a number array stays a root field
    CHECK(document->root.find("b")->count == 2);
    for (const char* text :
         {"", "[]", "{\"a\":[{\"b\":{}}]}", "{\"a\":[{\"b\":[{}]}]}", "{\"a\":[{\"b\":1},2]}",
          "{\"a\":[1,{\"b\":1}]}", "{\"a\":[{\"b\":1,\"b\":2}]}", "{\"a\":[],\"a\":[]}",
          "{\"a\":1,\"a\":[]}", "{\"a\":[],\"a\":1}", "{\"a\":[{\"b\":1},]}", "{\"a\":[{\"b\":1}",
          "{\"a\":[{\"b\":1}]}x", "{\"a\":[{\"b\":true}]}"}) {
        INFO(text);
        CHECK_FALSE(parse_config_document(text, std::strlen(text), *document));
    }
    CHECK_FALSE(parse_config_document(nullptr, 4, *document));
    CHECK_FALSE(parse_config_document(good, kDocumentMaxBytes + 1, *document));
    // Caps: lists per document, records per list, fields per record.
    std::string lists = "{";
    for (unsigned i = 0; i <= kDocumentMaxLists; ++i)
        lists += (i ? "," : "") + std::string("\"l") + std::to_string(i) + "\":[]";
    lists += "}";
    CHECK_FALSE(parse_config_document(lists.data(), lists.size(), *document));
    std::string records = "{\"l\":[";
    for (unsigned i = 0; i <= kDocumentMaxRecords; ++i)
        records += i ? ",{}" : "{}";
    records += "]}";
    CHECK_FALSE(parse_config_document(records.data(), records.size(), *document));
    std::string fields = "{\"l\":[{";
    for (unsigned i = 0; i <= kRecordMaxFields; ++i)
        fields += (i ? "," : "") + std::string("\"f") + std::to_string(i) + "\":0";
    fields += "}]}";
    CHECK_FALSE(parse_config_document(fields.data(), fields.size(), *document));
}
TEST_CASE("scene: approved schema parses and every truncation is rejected") {
    auto parsed = parse_scene(kScene, sizeof(kScene) - 1);
    REQUIRE(parsed);
    const SceneConfig& scene = parsed.value();
    CHECK(scene.actor_count == 2);
    CHECK(scene.actors[0].rig == RigId::kKnight);
    CHECK(scene.actors[1].rig == RigId::kBarbarian);
    CHECK(scene.actors[0].position.x == doctest::Approx(-0.8));
    CHECK(scene.actors[0].yaw == doctest::Approx(-hue::kPi / 2));
    REQUIRE(scene.prop_count == 1);
    CHECK(scene.props[0].half_extents.x == doctest::Approx(0.5));
    CHECK(scene.props[0].yaw == doctest::Approx(hue::radians(30.0f)));
    REQUIRE(scene.light_count == 1);
    CHECK(scene.lights[0].intensity == doctest::Approx(14.0));
    CHECK(scene.sun_intensity == doctest::Approx(3.0));
    CHECK(scene.shadows);
    for (std::size_t i = 0; i < sizeof(kScene) - 1; ++i)
        CHECK_FALSE(parse_scene(kScene, i));
}
TEST_CASE("scene: the shipped scene files are valid") {
    for (const char* name : {"arena", "gauntlet"}) {
        INFO(name);
        const auto bytes = shipped(name);
        auto parsed = parse_scene(bytes.data(), bytes.size());
        REQUIRE(parsed);
        CHECK(valid_scene(parsed.value()));
    }
    const auto gauntlet = shipped("gauntlet");
    auto parsed = parse_scene(gauntlet.data(), gauntlet.size());
    REQUIRE(parsed);
    CHECK(parsed.value().actor_count == 4);
    CHECK(parsed.value().prop_count == 4);
}
TEST_CASE("scene: rejects paths unknown keys versions collisions and out-of-range values") {
    for (const auto& text :
         {replaced("training_arena", "../../file"), replaced("\"version\":2", "\"version\":1"),
          replaced("\"version\":2", "\"version\":3"), replaced("\"version\":2", "\"version\":[2]"),
          replaced("\"shadows\":1", "\"shadows\":2"), replaced("\"shadows\":1", "\"unknown\":1"),
          replaced("\"shadows\":1", "\"shadows\":1,\"extra\":1"),
          replaced("kaykit_barbarian", "../enemy"), replaced("kaykit_knight", "kaykit_wizard"),
          replaced("\"role\":\"player\"", "\"role\":\"enemy\""),
          replaced("\"role\":\"enemy\"", "\"role\":\"player\""),
          replaced("[-0.8,0.05,3]", "[0.8,0.05,3]"),  // on top of the enemy
          replaced("[-0.8,0.05,3]", "[0,0.05,0]"),    // inside the center pillar
          replaced("[-0.8,0.05,3]", "[6,0.05,0]"),    // inside a ring box
          replaced("[-0.8,0.05,3]", "[-4,0.05,0]"),   // on the ramp
          replaced("[-0.8,0.05,3]", "[6,0.05,9]"),    // inside the prop
          replaced("[-0.8,0.05,3]", "[-19,0.05,3]"),  // outside the arena
          replaced("[-0.8,0.05,3]", "[-0.8,1,3]"),    // not on the ground
          replaced("[-0.8,0.05,3]", "[-0.8,0.05]"), replaced("\"yaw\":90", "\"yaw\":181"),
          replaced("\"shape\":\"box\"", "\"shape\":\"mesh\""),
          replaced("\"size\":[1,1,1]", "\"size\":[1,0,1]"),
          replaced("\"size\":[1,1,1]", "\"size\":[1,40,1]"),
          replaced("[6,0.5,9]", "[6,-3,9]"), replaced("[0.7,0.4,0.2]", "[2,0.4,0.2]"),
          replaced("\"radius\":10", "\"radius\":0"), replaced("\"intensity\":14", "\"intensity\":-1"),
          replaced("[0.4,-1,0.3]", "[0.4,1,0.3]"),  // sun pointing up
          replaced("[0.4,-1,0.3]", "[0,0,0]"), replaced("[1,0.96,0.9,3]", "[1,0.96,0.9]"),
          replaced("[1,0.96,0.9,3]", "[1,0.96,0.9,900]"),
          // a lone player has nobody to fight
          replaced(",{\"rig\":\"kaykit_barbarian\",\"role\":\"enemy\",\"position\":[0.8,0.05,3],\"yaw\":90}", ""),
          replaced("\"props\":[{", "\"props\":[{},{"), replaced("\"props\":", "\"prop\":")}) {
        INFO(text);
        CHECK_FALSE(parse_scene(text.data(), text.size()));
    }
    // More actors, props or lights than the fixed capacities.
    std::string crowd = kScene;
    const std::string enemy =
        ",{\"rig\":\"kaykit_barbarian\",\"role\":\"enemy\",\"position\":[%d,0.05,12],\"yaw\":0}";
    std::string extra;
    for (int i = 0; i < 7; ++i) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), enemy.c_str(), -8 + i * 2);
        extra += buffer;
    }
    crowd.insert(crowd.find("],\"props\""), extra);
    CHECK_FALSE(parse_scene(crowd.data(), crowd.size())); // 9 actors
    crowd.erase(crowd.find(extra), std::strlen(",{\"rig\":\"kaykit_barbarian\",\"role\":\"enemy\",\"position\":[-8,0.05,12],\"yaw\":0}"));
    CHECK(parse_scene(crowd.data(), crowd.size())); // 8 actors is the cap
    SceneConfig scene;
    CHECK(valid_scene(scene));
    scene.actors[0].position.x = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(valid_scene(scene));
    scene = {};
    scene.actor_count = kMaxSceneActors + 1;
    CHECK_FALSE(valid_scene(scene));
    scene = {};
    scene.actors[1].rig = RigId::kCount;
    CHECK_FALSE(valid_scene(scene));
    scene = {};
    scene.prop_count = kMaxSceneProps + 1;
    CHECK_FALSE(valid_scene(scene));
}
TEST_CASE("scene: names are whitelisted identifiers, never paths") {
    for (const char* name : {"arena", "gauntlet", "a", "scene_2", "0123456789abcdef0123456789abcdef"})
        CHECK(valid_scene_name(name));
    for (const char* name :
         {"", "../arena", "arena.json", "Arena", "a/b", "a\\b", "a b", "c:arena", "arena\n",
          "0123456789abcdef0123456789abcdef0"})
        CHECK_FALSE(valid_scene_name(name));
    CHECK_FALSE(valid_scene_name(nullptr));
    CHECK_FALSE(read_scene_file("../arena"));
    CHECK_FALSE(read_scene_file("no_such_scene_here"));
}
