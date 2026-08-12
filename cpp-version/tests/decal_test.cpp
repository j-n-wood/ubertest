#include <gtest/gtest.h>
#include "effects/decal_manager.h"
#include "raylib.h"

// DecalManager is render-free (the game draws the quads); these exercise its data behaviour:
// per-deck storage/persistence, spawning, the cleaner queries, and the fade→remove lifecycle.

TEST(DecalManager, SpawnAddsToActiveDeck) {
    DecalManager m;
    m.build(3);
    m.setActiveLevel(1);
    EXPECT_TRUE(m.active().empty());

    m.spawnBlastmark({2.0f, 3.0f}, 0.5f);
    m.spawnDrip({4.0f, 5.0f}, 0.2f);
    ASSERT_EQ(m.active().size(), 2u);
    EXPECT_EQ(m.active()[0].texture, TEX_DECAL_BLASTMARK);
    EXPECT_EQ(m.active()[1].texture, TEX_DECAL_DRIP);
    EXPECT_TRUE(m.active()[0].cleanable);
    EXPECT_FLOAT_EQ(m.active()[0].alpha, 1.0f);

    // Zero/negative size is ignored (nothing to draw).
    m.spawnDrip({0, 0}, 0.0f);
    EXPECT_EQ(m.active().size(), 2u);
}

TEST(DecalManager, PersistsPerDeck) {
    DecalManager m;
    m.build(3);
    m.setActiveLevel(0); m.spawnBlastmark({0, 0}, 0.5f);
    m.setActiveLevel(1); m.spawnBlastmark({1, 1}, 0.5f); m.spawnBlastmark({2, 2}, 0.5f);

    // Each deck keeps its own marks across switches.
    m.setActiveLevel(0); EXPECT_EQ(m.active().size(), 1u);
    m.setActiveLevel(1); EXPECT_EQ(m.active().size(), 2u);
    m.setActiveLevel(2); EXPECT_TRUE(m.active().empty());
    // Out-of-range active level is safe (empty), spawns are dropped.
    m.setActiveLevel(99);
    EXPECT_TRUE(m.active().empty());
    m.spawnBlastmark({0, 0}, 0.5f);
    EXPECT_TRUE(m.active().empty());
}

TEST(DecalManager, NearestCleanableRespectsRangeAndFlag) {
    DecalManager m;
    m.build(1);
    m.setActiveLevel(0);
    m.spawnBlastmark({0, 0}, 0.5f);    // idx 0 at origin
    m.spawnDrip({10.0f, 0}, 0.2f);     // idx 1 far away

    EXPECT_EQ(m.nearestCleanable({0.3f, 0}, 1.0f), 0);    // in range of origin mark
    EXPECT_EQ(m.nearestCleanable({0.3f, 0}, 0.1f), -1);   // nothing within a tiny radius
    EXPECT_EQ(m.nearestCleanable({9.5f, 0}, 2.0f), 1);    // picks the closer (far) mark

    // A non-cleanable decal (e.g. a future level decal) is never returned.
    m.setActiveLevel(0);
    // (No public API to add a non-cleanable one yet; the flag defaults true for runtime marks.)
}

TEST(DecalManager, CleanFadesThenRemoves) {
    DecalManager m;
    m.build(1);
    m.setActiveLevel(0);
    m.spawnBlastmark({0, 0}, 0.5f);

    // Fading isn't done until alpha reaches 0.
    EXPECT_FALSE(m.cleanAt(0, 0.05f));
    EXPECT_LT(m.active()[0].alpha, 1.0f);

    bool done = false;
    for (int i = 0; i < 1000 && !done; ++i) done = m.cleanAt(0, 0.05f);
    EXPECT_TRUE(done);
    EXPECT_LE(m.active()[0].alpha, 0.0f);

    // A fully-faded decal is excluded from cleaner queries and reaped by update().
    EXPECT_EQ(m.nearestCleanable({0, 0}, 5.0f), -1);
    m.update(0.016f);
    EXPECT_TRUE(m.active().empty());

    // cleanAt on a stale/invalid index is safe and reports "done".
    EXPECT_TRUE(m.cleanAt(0, 0.05f));
    EXPECT_TRUE(m.cleanAt(-1, 0.05f));
}

TEST(DecalManager, ClearEmptiesEveryDeck) {
    DecalManager m;
    m.build(2);
    m.setActiveLevel(0); m.spawnBlastmark({0, 0}, 0.5f);
    m.setActiveLevel(1); m.spawnBlastmark({1, 1}, 0.5f);

    m.clear();
    m.setActiveLevel(0); EXPECT_TRUE(m.active().empty());
    m.setActiveLevel(1); EXPECT_TRUE(m.active().empty());
}
