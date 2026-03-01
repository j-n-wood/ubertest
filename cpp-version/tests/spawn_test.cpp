#include <gtest/gtest.h>
#include "level/spawn_config.h"
#include "units/unit_types.h"
#include <set>
#include <algorithm>

//------------------------------------------------------------------------------
// Test fixture — builds type-class map from DroidProperties, loads ship spawns
//------------------------------------------------------------------------------

// Simulates loaded droid classes with known typeCode -> type mapping
static const DroidProperties TEST_DROIDS[] = {
    {.classId = 1,  .typeCode = 100},
    {.classId = 2,  .typeCode = 100},   // type 1: classes [1, 2]
    {.classId = 3,  .typeCode = 200},
    {.classId = 4,  .typeCode = 200},
    {.classId = 5,  .typeCode = 200},   // type 2: classes [3, 4, 5]
    {.classId = 6,  .typeCode = 300},
    {.classId = 7,  .typeCode = 300},   // type 3: classes [6, 7]
    {.classId = 8,  .typeCode = 400},
    {.classId = 9,  .typeCode = 400},
    {.classId = 10, .typeCode = 400},   // type 4: classes [8, 9, 10]
    {.classId = 11, .typeCode = 500},
    {.classId = 12, .typeCode = 500},
    {.classId = 13, .typeCode = 500},   // type 5: classes [11, 12, 13]
    {.classId = 14, .typeCode = 600},
    {.classId = 15, .typeCode = 600},
    {.classId = 16, .typeCode = 600},   // type 6: classes [14, 15, 16]
    {.classId = 17, .typeCode = 700},
    {.classId = 18, .typeCode = 700},
    {.classId = 19, .typeCode = 700},   // type 7: classes [17, 18, 19]
    {.classId = 20, .typeCode = 800},
    {.classId = 21, .typeCode = 800},
    {.classId = 22, .typeCode = 800},   // type 8: classes [20, 21, 22]
    {.classId = 23, .typeCode = 900},   // type 9: classes [23]
};

static const char* TEST_SHIP_JSON = R"({
  "name": "Test Ship",
  "levels": [
    {
      "level": 0,
      "profile": [2, 1, 1, 3, 0, 0, 0, 0, 0],
      "placedDroids": []
    },
    {
      "level": 1,
      "profile": [1, 0, 0, 0, 0, 0, 0, 0, 0],
      "placedDroids": [{"classId": 23, "waypointIndex": 5, "angle": 1.57}]
    }
  ]
})";

class SpawnFixture : public ::testing::Test {
protected:
    void SetUp() override {
        clearSpawnConfig();
        buildTypeClassMap(TEST_DROIDS, static_cast<int>(std::size(TEST_DROIDS)));
        ASSERT_TRUE(loadShipSpawnsFromJson(TEST_SHIP_JSON));
    }
};

//------------------------------------------------------------------------------
// Tests
//------------------------------------------------------------------------------

TEST_F(SpawnFixture, RespectsUnitCounts) {
    // Level 0 profile: [2, 1, 1, 3, 0, 0, 0, 0, 0] = 7 droids total
    const LevelSpawnDef* def = getSpawnDef(0, 0);
    ASSERT_NE(def, nullptr);

    // Total from profile
    int expectedTotal = 2 + 1 + 1 + 3;  // = 7
    auto results = resolveSpawns(*def, 20, -1);
    EXPECT_EQ(static_cast<int>(results.size()), expectedTotal);

    // Every spawned class must belong to the correct type group
    // Type 1 classes: [1,2], Type 2: [3,4,5], Type 3: [6,7], Type 4: [8,9,10]
    int type1Count = 0, type2Count = 0, type3Count = 0, type4Count = 0;
    for (const auto& spawn : results) {
        if (spawn.classId >= 1 && spawn.classId <= 2) ++type1Count;
        else if (spawn.classId >= 3 && spawn.classId <= 5) ++type2Count;
        else if (spawn.classId >= 6 && spawn.classId <= 7) ++type3Count;
        else if (spawn.classId >= 8 && spawn.classId <= 10) ++type4Count;
    }
    EXPECT_EQ(type1Count, 2);
    EXPECT_EQ(type2Count, 1);
    EXPECT_EQ(type3Count, 1);
    EXPECT_EQ(type4Count, 3);

    // Level 1: profile [1,0,...] + 1 placed droid = 2 total
    const LevelSpawnDef* def1 = getSpawnDef(0, 1);
    ASSERT_NE(def1, nullptr);
    auto results1 = resolveSpawns(*def1, 20, -1);
    EXPECT_EQ(static_cast<int>(results1.size()), 2);

    // The placed droid should be class 23
    bool foundClass23 = false;
    for (const auto& spawn : results1) {
        if (spawn.classId == 23) foundClass23 = true;
    }
    EXPECT_TRUE(foundClass23);
}

TEST_F(SpawnFixture, UsesDistinctWaypoints) {
    // With enough waypoints, each unit should get a unique waypoint
    const LevelSpawnDef* def = getSpawnDef(0, 0);
    ASSERT_NE(def, nullptr);

    auto results = resolveSpawns(*def, 20, -1);
    ASSERT_EQ(static_cast<int>(results.size()), 7);

    std::set<int> usedWaypoints;
    for (const auto& spawn : results) {
        usedWaypoints.insert(spawn.waypointIndex);
    }
    // All waypoints should be distinct
    EXPECT_EQ(usedWaypoints.size(), results.size());
}

TEST_F(SpawnFixture, AvoidsPlayerWaypoint) {
    const LevelSpawnDef* def = getSpawnDef(0, 0);
    ASSERT_NE(def, nullptr);

    int playerWaypoint = 3;
    auto results = resolveSpawns(*def, 20, playerWaypoint);

    // No droid should be assigned to the player's waypoint
    for (const auto& spawn : results) {
        EXPECT_NE(spawn.waypointIndex, playerWaypoint);
    }
}

TEST_F(SpawnFixture, HandlesInsufficientWaypoints) {
    // Level 0 wants 7 droids, but only provide 4 waypoints (+ 1 player = 3 available)
    const LevelSpawnDef* def = getSpawnDef(0, 0);
    ASSERT_NE(def, nullptr);

    auto results = resolveSpawns(*def, 4, 0);

    // All 7 droids should still be spawned (some waypoints reused)
    EXPECT_EQ(static_cast<int>(results.size()), 7);

    // No droid should be on the player waypoint
    for (const auto& spawn : results) {
        EXPECT_NE(spawn.waypointIndex, 0);
    }

    // All waypoint indices should be valid (within range)
    for (const auto& spawn : results) {
        EXPECT_GE(spawn.waypointIndex, 0);
        EXPECT_LT(spawn.waypointIndex, 4);
    }
}
