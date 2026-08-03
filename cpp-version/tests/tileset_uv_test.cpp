#include <gtest/gtest.h>
#include "level/tileset_loader.h"
#include "level/level_types.h"

// getTileUV colour-row offset: the map_blocks atlas stacks 7 colour-variant rows of the same
// tiles (44 cols x 7 rows, 64px tiles + 2px spacing, image 2952x500). rowOffset shifts sampling
// down whole rows for the base-palette / lights-out feature. Diffuse only; clamped to range.
static TmxTileset makeAtlas() {
    TmxTileset ts;
    ts.imageWidth = 2952;
    ts.imageHeight = 500;
    ts.tileWidth = 64;
    ts.tileHeight = 64;
    ts.spacing = 2;
    ts.tileCount = 308;  // 44 * 7
    ts.columns = 44;
    ts.firstGid = 1;
    return ts;
}

TEST(TileUV, RowOffsetShiftsVByWholeRows) {
    TmxTileset ts = makeAtlas();
    const float rowStep = (ts.tileHeight + ts.spacing) / static_cast<float>(ts.imageHeight);  // 66/500

    float u0, v0, u1, v1;          // tile GID 1, base row 0
    getTileUV(ts, 1, &u0, &v0, &u1, &v1, 0);
    float u0b, v0b, u1b, v1b;      // same tile shifted down 6 rows (lights-out)
    getTileUV(ts, 1, &u0b, &v0b, &u1b, &v1b, 6);

    EXPECT_FLOAT_EQ(u0b, u0) << "same column";
    EXPECT_FLOAT_EQ(u1b, u1);
    EXPECT_NEAR(v0b - v0, 6 * rowStep, 1e-5f);
    EXPECT_NEAR(v1b - v1, 6 * rowStep, 1e-5f);
}

TEST(TileUV, RowOffsetClampsToLastRow) {
    TmxTileset ts = makeAtlas();  // 7 rows → last index 6
    float u0, v0, u1, v1;
    getTileUV(ts, 1, &u0, &v0, &u1, &v1, 100);  // absurd offset
    float u0b, v0b, u1b, v1b;
    getTileUV(ts, 1, &u0b, &v0b, &u1b, &v1b, 6);  // last row
    EXPECT_FLOAT_EQ(v0, v0b) << "clamped to the last row";
    EXPECT_FLOAT_EQ(v1, v1b);
}

TEST(TileUV, ZeroOffsetUnchanged) {
    TmxTileset ts = makeAtlas();
    float a0, b0, a1, b1, c0, d0, c1, d1;
    getTileUV(ts, 5, &a0, &b0, &a1, &b1);           // default rowOffset = 0
    getTileUV(ts, 5, &c0, &d0, &c1, &d1, 0);
    EXPECT_FLOAT_EQ(a0, c0);
    EXPECT_FLOAT_EQ(b0, d0);
    EXPECT_FLOAT_EQ(a1, c1);
    EXPECT_FLOAT_EQ(b1, d1);
}
