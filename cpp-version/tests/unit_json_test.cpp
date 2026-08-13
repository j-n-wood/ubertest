#include <gtest/gtest.h>
#include "units/unit_json.h"
#include "units/unit_types.h"
#include <string>

// A unit JSON exercising the animation/facing markers: a turret-role child, a head-role
// grandchild, an animMoving section, plus the unit-level fireWhileMoving / omnidirectional /
// turretTurnSpeed flags. See docs/unit_animation.md.
static const char* MARKED_UNIT_JSON = R"({
  "name": "Marked",
  "id": "marked",
  "collisionRadius": 0.4,
  "properties": {
    "classId": 99,
    "weapon": 3,
    "hasTurret": true,
    "omnidirectional": true,
    "fireWhileMoving": true,
    "turretTurnSpeed": 12.5,
    "headTurnSpeed": 4.0
  },
  "rootSection": {
    "name": "body",
    "model": "models/legs.glb",
    "animMoving": true,
    "children": [
      { "name": "turret", "model": "models/head.gltf", "role": "turret" },
      { "name": "sensor", "model": "models/eye.gltf", "role": "head" }
    ]
  }
})";

TEST(UnitAnimationMarkers, ParsesRoleAndFlags) {
    UnitDefinition def;
    ASSERT_TRUE(parseUnitDefinitionFromString(MARKED_UNIT_JSON, def));

    // Unit-level flags.
    EXPECT_TRUE(def.properties.omnidirectional);
    EXPECT_TRUE(def.properties.fireWhileMoving);
    EXPECT_FLOAT_EQ(def.properties.turretTurnSpeed, 12.5f);
    EXPECT_FLOAT_EQ(def.properties.headTurnSpeed, 4.0f);

    // Root section is animMoving and follows the unit.
    EXPECT_TRUE(def.rootSection.animMoving);
    EXPECT_EQ(def.rootSection.role, SectionRole::None);

    // A non-None role implies FollowFacing.
    ASSERT_EQ(def.rootSection.children.size(), 2u);
    const auto& turret = def.rootSection.children[0];
    const auto& sensor = def.rootSection.children[1];
    EXPECT_EQ(turret.role, SectionRole::Turret);
    EXPECT_EQ(turret.rotationMode, SectionRotationMode::FollowFacing);
    EXPECT_EQ(sensor.role, SectionRole::Head);
    EXPECT_EQ(sensor.rotationMode, SectionRotationMode::FollowFacing);
}

TEST(UnitAnimationMarkers, RoundTripsThroughSerialization) {
    UnitDefinition def;
    ASSERT_TRUE(parseUnitDefinitionFromString(MARKED_UNIT_JSON, def));

    std::string out = serializeUnitDefinitionToString(def, /*pretty=*/true);
    // Markers survive serialization (role written instead of the implied rotationMode).
    EXPECT_NE(out.find("\"role\": \"turret\""), std::string::npos);
    EXPECT_NE(out.find("\"role\": \"head\""), std::string::npos);
    EXPECT_NE(out.find("\"animMoving\": true"), std::string::npos);
    EXPECT_NE(out.find("\"fireWhileMoving\": true"), std::string::npos);
    EXPECT_NE(out.find("\"turretTurnSpeed\""), std::string::npos);
    EXPECT_NE(out.find("\"headTurnSpeed\""), std::string::npos);

    // Reparsing yields the same marker set.
    UnitDefinition def2;
    ASSERT_TRUE(parseUnitDefinitionFromString(out, def2));
    ASSERT_EQ(def2.rootSection.children.size(), 2u);
    EXPECT_EQ(def2.rootSection.children[0].role, SectionRole::Turret);
    EXPECT_EQ(def2.rootSection.children[1].role, SectionRole::Head);
    EXPECT_TRUE(def2.rootSection.animMoving);
    EXPECT_TRUE(def2.properties.fireWhileMoving);
    EXPECT_TRUE(def2.properties.omnidirectional);
    EXPECT_FLOAT_EQ(def2.properties.turretTurnSpeed, 12.5f);
    EXPECT_FLOAT_EQ(def2.properties.headTurnSpeed, 4.0f);
}

TEST(UnitAnimationMarkers, DefaultsWhenUnmarked) {
    static const char* PLAIN = R"({
      "name": "Plain", "id": "plain",
      "rootSection": { "name": "body", "model": "m.gltf" }
    })";
    UnitDefinition def;
    ASSERT_TRUE(parseUnitDefinitionFromString(PLAIN, def));
    EXPECT_EQ(def.rootSection.role, SectionRole::None);
    EXPECT_FALSE(def.rootSection.animMoving);
    EXPECT_FALSE(def.properties.fireWhileMoving);
    EXPECT_FALSE(def.properties.omnidirectional);
    EXPECT_FLOAT_EQ(def.properties.turretTurnSpeed, 0.0f);
    EXPECT_FLOAT_EQ(def.properties.headTurnSpeed, 0.0f);
    EXPECT_FLOAT_EQ(def.properties.dripThreshold, 0.0f);  // absent → never drips
}

// dripThreshold (health below which a moving droid leaks drip decals) parses + round-trips.
TEST(UnitDripThreshold, ParsesAndRoundTrips) {
    static const char* DRIPPY = R"({
      "name": "Drippy", "id": "drippy",
      "properties": { "classId": 7, "energy": 30, "dripThreshold": 21.0 },
      "rootSection": { "name": "body", "model": "m.gltf" }
    })";
    UnitDefinition def;
    ASSERT_TRUE(parseUnitDefinitionFromString(DRIPPY, def));
    EXPECT_FLOAT_EQ(def.properties.dripThreshold, 21.0f);

    std::string out = serializeUnitDefinitionToString(def, /*pretty=*/true);
    EXPECT_NE(out.find("dripThreshold"), std::string::npos);
    UnitDefinition rt;
    ASSERT_TRUE(parseUnitDefinitionFromString(out, rt));
    EXPECT_FLOAT_EQ(rt.properties.dripThreshold, 21.0f);
}

// disruptorShielded (immune to disruptor area damage) parses + round-trips; absent → false.
TEST(UnitDisruptorShielded, ParsesAndRoundTrips) {
    static const char* SHIELDED = R"({
      "name": "Shielded", "id": "shielded",
      "properties": { "classId": 8, "energy": 50, "disruptorShielded": true },
      "rootSection": { "name": "body", "model": "m.gltf" }
    })";
    UnitDefinition def;
    ASSERT_TRUE(parseUnitDefinitionFromString(SHIELDED, def));
    EXPECT_TRUE(def.properties.disruptorShielded);

    std::string out = serializeUnitDefinitionToString(def, /*pretty=*/true);
    EXPECT_NE(out.find("disruptorShielded"), std::string::npos);
    UnitDefinition rt;
    ASSERT_TRUE(parseUnitDefinitionFromString(out, rt));
    EXPECT_TRUE(rt.properties.disruptorShielded);

    // Absent → defaults false and is omitted from serialization.
    static const char* PLAIN = R"({
      "name": "Plain", "id": "plain",
      "properties": { "classId": 9, "energy": 30 },
      "rootSection": { "name": "body", "model": "m.gltf" }
    })";
    UnitDefinition pd;
    ASSERT_TRUE(parseUnitDefinitionFromString(PLAIN, pd));
    EXPECT_FALSE(pd.properties.disruptorShielded);
    EXPECT_EQ(serializeUnitDefinitionToString(pd, true).find("disruptorShielded"), std::string::npos);
}
