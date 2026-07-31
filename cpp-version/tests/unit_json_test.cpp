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
    "turretTurnSpeed": 12.5
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
}
