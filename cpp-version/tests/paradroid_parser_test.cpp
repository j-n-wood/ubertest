#include <gtest/gtest.h>
#include "paradroid_parser.h"

// Expected level data from Paradroid.maps
struct ExpectedLevel {
    int number;
    int xlen;
    int ylen;
    const char* name;
    int waypointCount;
    int linkCount;
};

constexpr ExpectedLevel EXPECTED_LEVELS[] = {
    {0,  38, 16, "maintenance",  18, 48},
    {1,  48, 16, "engineering",  14, 27},
    {2,  18, 11, "robostores",    8, 14},
    {3,  31, 13, "quarterd",     16, 36},
    {4,  32, 13, "repairs",      14, 29},
    {5,  38, 15, "staterooms",   24, 53},
    {6,  38, 15, "stores",       20, 42},
    {7,  38, 15, "research",     19, 43},
    {8,  38, 12, "bridge",       13, 30},
    {9,  20,  8, "observation",   5, 10},
    {10, 22,  5, "airlock",       5, 12},
    {11, 34, 15, "reactor",      18, 44},
    {12, 34, 16, "upper cargo",  20, 56},
    {13, 34, 16, "mid carga",    17, 56},
    {14, 26, 16, "vehicle hold", 14, 36},
    {15, 10, 15, "shuttle bay",   8, 17},
};
constexpr int EXPECTED_LEVEL_COUNT = 16;
constexpr int EXPECTED_TOTAL_WAYPOINTS = 233;
constexpr int EXPECTED_TOTAL_LINKS = 553;

// Default input path (relative to build/tests/ where run_tests executes)
// From cpp-version/build/tests/ -> ../../../tiled/Paradroid.maps
const char* TEST_INPUT_PATH = "../../../tiled/Paradroid.maps";

TEST(ParadroidParserTest, ParsesCorrectNumberOfLevels) {
    auto result = parseParadroidMaps(TEST_INPUT_PATH);
    ASSERT_TRUE(result.success) << result.errorMsg;
    EXPECT_EQ(result.mapFile.levels.size(), EXPECTED_LEVEL_COUNT);
}

TEST(ParadroidParserTest, AllLevelDimensions) {
    auto result = parseParadroidMaps(TEST_INPUT_PATH);
    ASSERT_TRUE(result.success) << result.errorMsg;
    ASSERT_EQ(result.mapFile.levels.size(), EXPECTED_LEVEL_COUNT);

    for (int i = 0; i < EXPECTED_LEVEL_COUNT; i++) {
        const auto& level = result.mapFile.levels[i];
        const auto& expected = EXPECTED_LEVELS[i];
        EXPECT_EQ(level.levelNumber, expected.number) << "Level " << i;
        EXPECT_EQ(level.xlen, expected.xlen) << "Level " << i << " xlen";
        EXPECT_EQ(level.ylen, expected.ylen) << "Level " << i << " ylen";
        EXPECT_EQ(level.name, expected.name) << "Level " << i << " name";
    }
}

TEST(ParadroidParserTest, TileDataMatchesDimensions) {
    auto result = parseParadroidMaps(TEST_INPUT_PATH);
    ASSERT_TRUE(result.success) << result.errorMsg;

    for (const auto& level : result.mapFile.levels) {
        EXPECT_EQ(level.tiles.size(), level.ylen)
            << "Level " << level.levelNumber << " row count";
        for (size_t row = 0; row < level.tiles.size(); row++) {
            EXPECT_EQ(level.tiles[row].size(), level.xlen)
                << "Level " << level.levelNumber << " row " << row;
        }
    }
}

TEST(ParadroidParserTest, AreaName) {
    auto result = parseParadroidMaps(TEST_INPUT_PATH);
    ASSERT_TRUE(result.success) << result.errorMsg;
    EXPECT_EQ(result.mapFile.areaName, "U.S.S. Paradroid");
}

TEST(ParadroidParserTest, WaypointCountsPerLevel) {
    auto result = parseParadroidMaps(TEST_INPUT_PATH);
    ASSERT_TRUE(result.success) << result.errorMsg;
    ASSERT_EQ(result.mapFile.levels.size(), EXPECTED_LEVEL_COUNT);

    for (int i = 0; i < EXPECTED_LEVEL_COUNT; i++) {
        const auto& level = result.mapFile.levels[i];
        const auto& expected = EXPECTED_LEVELS[i];
        EXPECT_EQ(level.waypoints.size(), expected.waypointCount)
            << "Level " << i << " (" << expected.name << ") waypoint count";
    }
}

TEST(ParadroidParserTest, LinkCountsPerLevel) {
    auto result = parseParadroidMaps(TEST_INPUT_PATH);
    ASSERT_TRUE(result.success) << result.errorMsg;
    ASSERT_EQ(result.mapFile.levels.size(), EXPECTED_LEVEL_COUNT);

    for (int i = 0; i < EXPECTED_LEVEL_COUNT; i++) {
        const auto& level = result.mapFile.levels[i];
        const auto& expected = EXPECTED_LEVELS[i];

        int linkCount = 0;
        for (const auto& wp : level.waypoints) {
            linkCount += wp.connections.size();
        }
        EXPECT_EQ(linkCount, expected.linkCount)
            << "Level " << i << " (" << expected.name << ") link count";
    }
}

TEST(ParadroidParserTest, TotalWaypointsAndLinks) {
    auto result = parseParadroidMaps(TEST_INPUT_PATH);
    ASSERT_TRUE(result.success) << result.errorMsg;

    int totalWaypoints = 0;
    int totalLinks = 0;
    for (const auto& level : result.mapFile.levels) {
        totalWaypoints += level.waypoints.size();
        for (const auto& wp : level.waypoints) {
            totalLinks += wp.connections.size();
        }
    }

    EXPECT_EQ(totalWaypoints, EXPECTED_TOTAL_WAYPOINTS) << "Total waypoints";
    EXPECT_EQ(totalLinks, EXPECTED_TOTAL_LINKS) << "Total links";
}
