#include <gtest/gtest.h>

#include "transfer_control.h"
#include "level/spawn_config.h"
#include "units/unit_types.h"
#include "units/heal.h"

#include <algorithm>

//------------------------------------------------------------------------------
// transfer_progress — pure fly-over timing helper.
//------------------------------------------------------------------------------

TEST(TransferProgressTest, ClampsAndInterpolates) {
    EXPECT_FLOAT_EQ(transfer_progress(0.0f, 1.5f), 0.0f);
    EXPECT_FLOAT_EQ(transfer_progress(0.75f, 1.5f), 0.5f);
    EXPECT_FLOAT_EQ(transfer_progress(1.5f, 1.5f), 1.0f);
    EXPECT_FLOAT_EQ(transfer_progress(3.0f, 1.5f), 1.0f);   // clamped past the end
    EXPECT_FLOAT_EQ(transfer_progress(-1.0f, 1.5f), 0.0f);  // clamped before the start
    EXPECT_FLOAT_EQ(transfer_progress(1.0f, 0.0f), 1.0f);   // zero duration -> done
}

//------------------------------------------------------------------------------
// away_healed_health — regen for time spent on an inactive level.
//------------------------------------------------------------------------------

TEST(AwayHealTest, RegeneratesAndClamps) {
    // max=200, 2%/s => 4 hp/s.
    EXPECT_FLOAT_EQ(away_healed_health(100.0f, 200.0f, 0.0, 0.02f), 100.0f);   // no time
    EXPECT_FLOAT_EQ(away_healed_health(100.0f, 200.0f, 10.0, 0.02f), 140.0f);  // +40
    EXPECT_FLOAT_EQ(away_healed_health(180.0f, 200.0f, 100.0, 0.02f), 200.0f); // clamp to max
    EXPECT_FLOAT_EQ(away_healed_health(50.0f, 200.0f, -5.0, 0.02f), 50.0f);    // negative time ignored
    EXPECT_FLOAT_EQ(away_healed_health(50.0f, 0.0f, 10.0, 0.02f), 50.0f);      // zero max: unchanged
}

//------------------------------------------------------------------------------
// Spawn exclusion — classId 0 (the player's type-0 device) is never spawned as AI.
//------------------------------------------------------------------------------

namespace {
// classId 0 (the device) shares type group 1 with classId 1 — the group it would
// otherwise be spawnable from. After exclusion only classId 1 remains in that group.
const DroidProperties EXCL_DROIDS[] = {
    {.classId = 0, .typeCode = 101},  // player device (type 1 group) — must be excluded
    {.classId = 1, .typeCode = 100},  // type 1 group
    {.classId = 3, .typeCode = 200},  // type 2 group
};

const char* EXCL_SHIP_JSON = R"({
  "name": "Excl Ship",
  "levels": [
    { "level": 0, "profile": [5, 0, 0, 0, 0, 0, 0, 0, 0],
      "placedDroids": [{"classId": 0, "waypointIndex": 1, "angle": 0.0}] }
  ]
})";
}  // namespace

TEST(SpawnExclusionTest, NeverSpawnsClassZero) {
    clearSpawnConfig();
    buildTypeClassMap(EXCL_DROIDS, static_cast<int>(std::size(EXCL_DROIDS)));
    ASSERT_TRUE(loadShipSpawnsFromJson(EXCL_SHIP_JSON));

    const LevelSpawnDef* def = getSpawnDef(0, 0);
    ASSERT_NE(def, nullptr);

    auto results = resolveSpawns(*def, 20, -1);

    // The placed classId-0 droid is dropped; the 5 type-1 spawns all resolve to classId 1
    // (the only class left in that group). classId 0 must never appear.
    EXPECT_EQ(results.size(), 5u);
    for (const auto& s : results) {
        EXPECT_NE(s.classId, 0);
    }
    clearSpawnConfig();
}
