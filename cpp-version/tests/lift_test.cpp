#include <gtest/gtest.h>
#include <fstream>
#include <string>

#include "level/level_types.h"
#include "world_scale.h"
#include "level/lift_manager.h"
#include "level/ship_map.h"
#include "level/tmx_loader.h"

//------------------------------------------------------------------------------
// Helpers: build synthetic levels with lift markers.
//------------------------------------------------------------------------------
namespace {
TmxLevel makeLevel(int number, int w, int h, std::vector<TmxLift> lifts) {
    TmxLevel lvl;
    lvl.number = number;
    lvl.width = w;
    lvl.height = h;
    lvl.lifts = std::move(lifts);
    return lvl;
}
}  // namespace

//------------------------------------------------------------------------------
// LiftManager: grouping, ordering, proximity, and stepping.
//------------------------------------------------------------------------------

TEST(LiftManagerTest, BuildsOrderedElevatorChain) {
    // Elevator 0 spread over 3 levels, authored out of order by stopIndex.
    std::vector<TmxLevel> levels = {
        makeLevel(0, 10, 10, {{5, 5, 0, 0}}),   // level 0 -> stop 0
        makeLevel(1, 10, 10, {{2, 2, 0, 2}}),   // level 1 -> stop 2
        makeLevel(2, 10, 10, {{7, 1, 0, 1}}),   // level 2 -> stop 1
    };
    LiftManager lm;
    lm.build(levels);
    ASSERT_EQ(lm.stops().size(), 3u);

    // From the bottom (level 0, stop 0), stepping up visits stop 1 then stop 2.
    // Find the stop on level 0.
    const LiftStop* bottom = nullptr;
    for (const LiftStop& s : lm.stops()) if (s.level == 0) bottom = &s;
    ASSERT_NE(bottom, nullptr);
    EXPECT_EQ(bottom->stopIndex, 0);

    const LiftStop* up1 = lm.stepStop(bottom, +1);
    ASSERT_NE(up1, nullptr);
    EXPECT_EQ(up1->stopIndex, 1);
    EXPECT_EQ(up1->level, 2);              // stop 1 was authored on level 2

    const LiftStop* up2 = lm.stepStop(up1, +1);
    ASSERT_NE(up2, nullptr);
    EXPECT_EQ(up2->stopIndex, 2);
    EXPECT_EQ(up2->level, 1);

    EXPECT_EQ(lm.stepStop(up2, +1), nullptr);   // past the top
    EXPECT_EQ(lm.stepStop(bottom, -1), nullptr); // below the bottom
    // Stepping back down from the top returns to stop 1.
    EXPECT_EQ(lm.stepStop(up2, -1), up1);
}

TEST(LiftManagerTest, SeparateElevatorsDoNotChain) {
    std::vector<TmxLevel> levels = {
        makeLevel(0, 10, 10, {{1, 1, 0, 0}, {8, 8, 1, 0}}),  // two elevators on one level
        makeLevel(1, 10, 10, {{1, 1, 0, 1}}),
    };
    LiftManager lm;
    lm.build(levels);

    // The elevator-1 stop has no neighbours (single stop in its chain).
    const LiftStop* e1 = nullptr;
    for (const LiftStop& s : lm.stops()) if (s.elevator == 1) e1 = &s;
    ASSERT_NE(e1, nullptr);
    EXPECT_EQ(lm.stepStop(e1, +1), nullptr);
    EXPECT_EQ(lm.stepStop(e1, -1), nullptr);
}

TEST(LiftManagerTest, ProximityOnCurrentLevelOnly) {
    // Lift at tile (5,5) on a 10x10 level -> physics centre (0.5, 0.5) tiles, metric = *WORLD_SCALE.
    std::vector<TmxLevel> levels = { makeLevel(0, 10, 10, {{5, 5, 0, 0}}) };
    LiftManager lm;
    lm.build(levels);
    ASSERT_EQ(lm.stops().size(), 1u);
    Vector2 centre = lm.stops()[0].physicsCenter;
    EXPECT_NEAR(centre.x, 0.5f * WORLD_SCALE, 1e-4f);
    EXPECT_NEAR(centre.y, 0.5f * WORLD_SCALE, 1e-4f);

    lm.update(centre, 0);
    EXPECT_TRUE(lm.onLift());
    EXPECT_EQ(lm.currentStop(), &lm.stops()[0]);

    lm.update({centre.x + LIFT_USE_RADIUS + 0.2f, centre.y}, 0);  // too far
    EXPECT_FALSE(lm.onLift());

    lm.update(centre, 1);  // right spot, wrong level
    EXPECT_FALSE(lm.onLift());
}

//------------------------------------------------------------------------------
// ShipMap: fractional-rect parsing + deck-by-number lookup.
//------------------------------------------------------------------------------

TEST(ShipMapTest, ParsesElevatorsAndDecks) {
    const char* json = R"({
      "image": "ship_on.png", "refWidth": 578, "refHeight": 211,
      "elevators": [ {"x":0.1,"y":0.2,"w":0.03,"h":0.5} ],
      "decks": [ {"level": 7, "rects": [ {"x":0.0,"y":0.5,"w":0.2,"h":0.1},
                                          {"x":0.3,"y":0.5,"w":0.1,"h":0.1} ] } ]
    })";
    std::string path = std::string(TEST_PROJECT_ROOT) + "/cpp-version/build/_lift_test_shipmap.json";
    { std::ofstream f(path); f << json; }

    ShipMap sm;
    ASSERT_TRUE(sm.load(path));
    EXPECT_EQ(sm.imageName(), "ship_on.png");

    const Rectangle* e0 = sm.elevatorRect(0);
    ASSERT_NE(e0, nullptr);
    EXPECT_NEAR(e0->x, 0.1f, 1e-4f);
    EXPECT_NEAR(e0->height, 0.5f, 1e-4f);
    EXPECT_EQ(sm.elevatorRect(1), nullptr);      // out of range

    const auto* deck7 = sm.deckRects(7);
    ASSERT_NE(deck7, nullptr);
    EXPECT_EQ(deck7->size(), 2u);
    EXPECT_EQ(sm.deckRects(3), nullptr);         // no such deck number
}

//------------------------------------------------------------------------------
// TMX loader: level number from filename + lift objects from an object layer.
//------------------------------------------------------------------------------

TEST(LiftLoaderTest, ParsesLevelNumberAndLiftObjects) {
    // A minimal 2x2 map with a "lifts" object layer holding one lift point object.
    const char* tmx =
        "<?xml version=\"1.0\"?>\n"
        "<map width=\"2\" height=\"2\" tilewidth=\"64\" tileheight=\"64\">\n"
        " <layer><data encoding=\"csv\">0,0,0,0</data></layer>\n"
        " <objectgroup name=\"lifts\">\n"
        "  <object id=\"1\" x=\"96\" y=\"96\">\n"
        "   <properties>\n"
        "    <property name=\"elevator\" type=\"int\" value=\"3\"/>\n"
        "    <property name=\"stop_index\" type=\"int\" value=\"2\"/>\n"
        "   </properties>\n"
        "   <point/>\n"
        "  </object>\n"
        " </objectgroup>\n"
        "</map>\n";
    std::string path = std::string(TEST_PROJECT_ROOT) + "/cpp-version/build/level_5_liftfixture.tmx";
    { std::ofstream f(path); f << tmx; }

    TmxLoadResult r = loadTmxLevel(path);
    ASSERT_TRUE(r.success);
    EXPECT_EQ(r.level.number, 5);                 // from "level_5_..."
    ASSERT_EQ(r.level.lifts.size(), 1u);
    const TmxLift& lift = r.level.lifts[0];
    EXPECT_EQ(lift.col, 1);                        // 96 / 64
    EXPECT_EQ(lift.row, 1);
    EXPECT_EQ(lift.elevator, 3);
    EXPECT_EQ(lift.stopIndex, 2);
    EXPECT_TRUE(r.level.waypoints.empty());        // lift object not mis-read as waypoint
}
