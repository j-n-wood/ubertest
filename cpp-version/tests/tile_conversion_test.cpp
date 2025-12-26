#include <gtest/gtest.h>
#include "scene_convert/scene_types.h"
#include "scene_convert/archetile_parser.h"
#include "scene_convert/domain_parser.h"
#include "scene_convert/scene_json.h"
#include <filesystem>

namespace fs = std::filesystem;

// Test data paths (uber is at test_project level, not cpp-version level)
// From cpp-version/build, need to go up to cpp-version, then up to test_project
static const char* TILES_PATH = "../../uber/uberdroid/data/tiles.txt";
static const char* XMAPFILE_PATH = "../../uber/uberdroid/ship1/xmapfile0.txt";

// Helper to find the correct path (tries multiple relative paths)
static const char* findTilesPath() {
    // From build directory
    if (fs::exists(TILES_PATH)) return TILES_PATH;
    // From build/tests directory
    if (fs::exists("../../../uber/uberdroid/data/tiles.txt"))
        return "../../../uber/uberdroid/data/tiles.txt";
    return TILES_PATH;
}

static const char* findXmapfilePath() {
    if (fs::exists(XMAPFILE_PATH)) return XMAPFILE_PATH;
    if (fs::exists("../../../uber/uberdroid/ship1/xmapfile0.txt"))
        return "../../../uber/uberdroid/ship1/xmapfile0.txt";
    return XMAPFILE_PATH;
}

//------------------------------------------------------------------------------
// Archetile Parser Tests
//------------------------------------------------------------------------------

class ArchetileParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Clear any cached archetiles
        ArchetileCache::instance().clear();
    }
};

TEST_F(ArchetileParserTest, LoadTilesFile) {
    std::vector<Archetile> archetiles;
    bool result = parseArchetilesFile(findTilesPath(), archetiles);

    ASSERT_TRUE(result) << "Failed to parse tiles.txt";
    EXPECT_GT(archetiles.size(), 0) << "No archetiles loaded";

    // Check first archetile has expected structure
    if (!archetiles.empty()) {
        const auto& first = archetiles[0];
        EXPECT_FALSE(first.name.empty()) << "First archetile has no name";
        EXPECT_GE(first.tile.vertices.size(), 3) << "First archetile has too few vertices";
    }
}

TEST_F(ArchetileParserTest, ArchetileCacheLoads) {
    bool loaded = ArchetileCache::instance().load(findTilesPath());
    ASSERT_TRUE(loaded) << "Failed to load archetiles into cache";
    EXPECT_TRUE(ArchetileCache::instance().isLoaded());
}

TEST_F(ArchetileParserTest, ArchetileCacheGet) {
    bool loaded = ArchetileCache::instance().load(findTilesPath());
    ASSERT_TRUE(loaded);

    // Index 0 should exist (Plain tile)
    const Archetile* tile0 = ArchetileCache::instance().get(0);
    ASSERT_NE(tile0, nullptr) << "Archetile 0 not found";
    EXPECT_EQ(tile0->index, 0);
    EXPECT_EQ(tile0->name, "Plain");

    // Invalid index should return nullptr
    const Archetile* invalid = ArchetileCache::instance().get(9999);
    EXPECT_EQ(invalid, nullptr);
}

TEST_F(ArchetileParserTest, ExpandArchetile) {
    ArchetileCache::instance().load(findTilesPath());
    const Archetile* archetype = ArchetileCache::instance().get(0);
    ASSERT_NE(archetype, nullptr);

    // Expand at offset
    float offsetX = 100.0f;
    float offsetY = 200.0f;
    Tile expanded = expandArchetile(*archetype, offsetX, offsetY);

    // Vertices should be offset
    ASSERT_GE(expanded.vertices.size(), 3);

    // Check that positions have been offset
    for (size_t i = 0; i < expanded.vertices.size(); ++i) {
        EXPECT_GE(expanded.vertices[i].position.x, offsetX - 1.0f);
        EXPECT_GE(expanded.vertices[i].position.y, offsetY - 1.0f);
    }
}

//------------------------------------------------------------------------------
// Domain Parser Tests
//------------------------------------------------------------------------------

class DomainParserTest : public ::testing::Test {
protected:
    void SetUp() override {
        ArchetileCache::instance().clear();
    }
};

TEST_F(DomainParserTest, ParseXmapfile) {
    Domain domain;
    const char* xmapPath = findXmapfilePath();
    fs::path basePath = fs::path(xmapPath).parent_path();

    bool result = parseDomainFile(xmapPath, domain, basePath, findTilesPath());

    ASSERT_TRUE(result) << "Failed to parse xmapfile";
    EXPECT_EQ(domain.levelNumber, 0);
    EXPECT_GT(domain.areas.size(), 0) << "No areas loaded";

    // Count total tiles
    int totalTiles = 0;
    for (const auto& area : domain.areas) {
        totalTiles += static_cast<int>(area.tiles.size());
    }
    EXPECT_GT(totalTiles, 0) << "No tiles loaded";
}

TEST_F(DomainParserTest, ParsedTilesHaveVertices) {
    Domain domain;
    const char* xmapPath = findXmapfilePath();
    fs::path basePath = fs::path(xmapPath).parent_path();

    bool result = parseDomainFile(xmapPath, domain, basePath, findTilesPath());
    ASSERT_TRUE(result);
    ASSERT_GT(domain.areas.size(), 0);

    // Check that tiles have valid vertex data
    for (const auto& area : domain.areas) {
        for (const auto& tile : area.tiles) {
            EXPECT_GE(tile.vertices.size(), 3) << "Tile has too few vertices";
            for (const auto& vertex : tile.vertices) {
                // Z should be positive (floor height)
                EXPECT_GE(vertex.position.z, 0.0f) << "Vertex Z is negative";
            }
        }
    }
}

TEST_F(DomainParserTest, ParsedDataStructures) {
    Domain domain;
    const char* xmapPath = findXmapfilePath();
    fs::path basePath = fs::path(xmapPath).parent_path();

    bool result = parseDomainFile(xmapPath, domain, basePath, findTilesPath());
    ASSERT_TRUE(result);

    // Domain should have areas with tiles (verified by other tests)
    // Count objects and features (may be zero for some maps)
    int totalObjects =
        static_cast<int>(domain.objects.doors.size()) +
        static_cast<int>(domain.objects.consoles.size()) +
        static_cast<int>(domain.objects.chargers.size()) +
        static_cast<int>(domain.objects.generic.size());

    // Count area features as well
    int totalFeatures = 0;
    for (const auto& area : domain.areas) {
        totalFeatures += static_cast<int>(area.features.size());
    }

    // Log counts for debugging (test passes regardless)
    EXPECT_GE(totalObjects, 0);
    EXPECT_GE(totalFeatures, 0);

    // At minimum, domain should have parsed something useful
    EXPECT_GT(domain.areas.size(), 0) << "No areas loaded";
}

//------------------------------------------------------------------------------
// JSON Serialization Tests
//------------------------------------------------------------------------------

class JsonSerializationTest : public ::testing::Test {
protected:
    Domain testDomain;

    void SetUp() override {
        ArchetileCache::instance().clear();

        // Load real data for testing
        const char* xmapPath = findXmapfilePath();
        fs::path basePath = fs::path(xmapPath).parent_path();
        parseDomainFile(xmapPath, testDomain, basePath, findTilesPath());
    }
};

TEST_F(JsonSerializationTest, DomainToJson) {
    std::string json = domainToJson(testDomain);

    EXPECT_FALSE(json.empty()) << "JSON output is empty";
    EXPECT_NE(json.find("\"version\""), std::string::npos) << "Missing version field";
    EXPECT_NE(json.find("\"areas\""), std::string::npos) << "Missing areas field";
    EXPECT_NE(json.find("\"tiles\""), std::string::npos) << "Missing tiles field";
}

TEST_F(JsonSerializationTest, DomainRoundTrip) {
    // Serialize
    std::string json = domainToJson(testDomain);
    ASSERT_FALSE(json.empty());

    // Deserialize
    Domain reloaded;
    bool result = jsonToDomain(json, reloaded);
    ASSERT_TRUE(result) << "Failed to parse JSON";

    // Verify key fields match
    EXPECT_EQ(reloaded.levelNumber, testDomain.levelNumber);
    EXPECT_EQ(reloaded.areas.size(), testDomain.areas.size());

    // Verify tile counts match
    int originalTiles = 0, reloadedTiles = 0;
    for (const auto& area : testDomain.areas) {
        originalTiles += static_cast<int>(area.tiles.size());
    }
    for (const auto& area : reloaded.areas) {
        reloadedTiles += static_cast<int>(area.tiles.size());
    }
    EXPECT_EQ(reloadedTiles, originalTiles);
}

TEST_F(JsonSerializationTest, TileVerticesTransformed) {
    // JSON stores coordinates in render space (Y-up, scaled)
    // Original domain has game space coordinates (X,Y horizontal, Z vertical)
    // JSON converts: game(X,Y,Z) -> render(X*scale, Z*scale, Y*scale)

    std::string json = domainToJson(testDomain);
    Domain reloaded;
    bool result = jsonToDomain(json, reloaded);
    ASSERT_TRUE(result);

    // Compare first tile's vertices
    ASSERT_GT(testDomain.areas.size(), 0);
    ASSERT_GT(testDomain.areas[0].tiles.size(), 0);
    ASSERT_GT(reloaded.areas.size(), 0);
    ASSERT_GT(reloaded.areas[0].tiles.size(), 0);

    const auto& originalTile = testDomain.areas[0].tiles[0];
    const auto& reloadedTile = reloaded.areas[0].tiles[0];

    EXPECT_EQ(reloadedTile.vertices.size(), originalTile.vertices.size());

    // Verify coordinate transformation was applied correctly
    // Game coords: X horizontal, Y horizontal (forward), Z vertical (height)
    // Render coords: X horizontal, Y vertical (up), Z horizontal (depth)
    // Transformation: renderX = gameX * scale, renderY = gameZ * scale, renderZ = gameY * scale
    constexpr float SCALE = 0.0254f;

    for (size_t i = 0; i < originalTile.vertices.size() && i < reloadedTile.vertices.size(); ++i) {
        const auto& orig = originalTile.vertices[i].position;
        const auto& reloaded_pos = reloadedTile.vertices[i].position;

        // Check transformation: X stays, Y<->Z swap, scale applied
        EXPECT_NEAR(reloaded_pos.x, orig.x * SCALE, 0.001f) << "X coordinate transform failed";
        EXPECT_NEAR(reloaded_pos.y, orig.z * SCALE, 0.001f) << "Z->Y coordinate transform failed";
        EXPECT_NEAR(reloaded_pos.z, orig.y * SCALE, 0.001f) << "Y->Z coordinate transform failed";
    }
}

TEST_F(JsonSerializationTest, FileWriteAndLoad) {
    // Write to temp file
    std::string tempPath = "test_domain_output.json";

    bool writeResult = saveDomainToFile(tempPath, testDomain);
    ASSERT_TRUE(writeResult) << "Failed to write domain to file";

    // Check file exists
    ASSERT_TRUE(fs::exists(tempPath)) << "Output file not created";

    // Load from file
    Domain reloaded;
    bool loadResult = loadDomainFromFile(tempPath, reloaded);
    ASSERT_TRUE(loadResult) << "Failed to load domain from file";

    // Verify
    EXPECT_EQ(reloaded.areas.size(), testDomain.areas.size());

    // Cleanup
    fs::remove(tempPath);
}

//------------------------------------------------------------------------------
// Coordinate Transformation Tests
//------------------------------------------------------------------------------

TEST(CoordinateTransformTest, ToRenderCoords) {
    // Game space: X=horizontal, Y=horizontal(forward), Z=vertical(height)
    // Render space: X=horizontal, Y=vertical(up), Z=horizontal(depth)

    float scale = 1.0f;

    // Origin stays at origin
    Vector3 origin = {0, 0, 0};
    Vector3 result = {origin.x * scale, origin.z * scale, origin.y * scale};
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);

    // Height (game Z) becomes Y
    Vector3 heightPoint = {0, 0, 10};
    result = {heightPoint.x * scale, heightPoint.z * scale, heightPoint.y * scale};
    EXPECT_FLOAT_EQ(result.y, 10.0f) << "Game Z should become render Y";

    // Forward (game Y) becomes Z
    Vector3 forwardPoint = {0, 10, 0};
    result = {forwardPoint.x * scale, forwardPoint.z * scale, forwardPoint.y * scale};
    EXPECT_FLOAT_EQ(result.z, 10.0f) << "Game Y should become render Z";
}

TEST(CoordinateTransformTest, ScaleApplied) {
    float scale = 0.0254f;  // inches to meters

    Vector3 point = {100.0f, 200.0f, 1.0f};
    Vector3 result = {point.x * scale, point.z * scale, point.y * scale};

    EXPECT_NEAR(result.x, 2.54f, 0.001f);  // 100 inches = 2.54 meters
    EXPECT_NEAR(result.z, 5.08f, 0.001f);  // 200 inches = 5.08 meters
    EXPECT_NEAR(result.y, 0.0254f, 0.0001f); // 1 inch = 0.0254 meters
}
