#include <gtest/gtest.h>
#include "combat/beam_manager.h"
#include "units/unit_instance.h"
#include "units/combat_state.h"
#include "units/unit_types.h"
#include "physics/body_user_data.h"
#include "box2d/box2d.h"

// Beam simulation: hitscan length (wall/door clipping), which units the line hits, and the
// continuous damage-accumulation path (shared with explosions). Rendering is not covered.
//
// Angle convention matches the game: forward = {-sin a, cos a}. All beams below fire north
// (+Y) with angle 0, forward {0, 1}, so units are placed along +Y.
class BeamTest : public ::testing::Test {
protected:
    b2WorldId world;
    UnitDefinition def;  // shared: collision radius 0.5, energy 100 → 100 health, armour 0

    void SetUp() override {
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = {0.0f, 0.0f};
        world = b2CreateWorld(&wd);
        def.collisionRadius = 0.5f;
        def.properties.energy = 100;
        def.properties.armour = 0.0f;
    }
    void TearDown() override { b2DestroyWorld(world); }

    void makeUnit(UnitInstance& u, Vector2 pos) {
        u.definition = &def;
        u.combatState = initCombatState(def.properties);  // health 100, armour 0
        u.active = true;
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = {pos.x, pos.y};
        u.bodyId = b2CreateBody(world, &bd);
        b2Circle circle = {{0, 0}, def.collisionRadius};
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.filter.categoryBits = CATEGORY_UNIT;
        sd.filter.maskBits = MASK_UNIT;
        b2CreateCircleShape(u.bodyId, &sd, &circle);
    }

    void makeWall(Vector2 c, float hx, float hy) {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_staticBody;
        bd.position = {c.x, c.y};
        b2BodyId w = b2CreateBody(world, &bd);
        b2Polygon box = b2MakeBox(hx, hy);
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.filter.categoryBits = CATEGORY_STATIC;
        sd.filter.maskBits = 0xFFFF;
        b2CreatePolygonShape(w, &sd, &box);
    }
};

TEST_F(BeamTest, CastLengthReachesMaxRangeWithNoWall) {
    EXPECT_NEAR(BeamManager::castLength(world, {0.0f, 0.0f}, 0.0f, 10.0f), 10.0f, 0.01f);
}

TEST_F(BeamTest, CastLengthTruncatesAtWall) {
    makeWall({0.0f, 5.0f}, 3.0f, 0.5f);  // spans y ∈ [4.5, 5.5]
    EXPECT_NEAR(BeamManager::castLength(world, {0.0f, 0.0f}, 0.0f, 10.0f), 4.5f, 0.05f);
}

TEST_F(BeamTest, HitsUnitOnLineOnly) {
    UnitInstance on, off, behind, beyond;
    makeUnit(on, {0.0f, 3.0f});      // on the line, within length
    makeUnit(off, {2.0f, 3.0f});     // 2 units to the side (> radius + half-width)
    makeUnit(behind, {0.0f, -3.0f}); // behind the muzzle
    makeUnit(beyond, {0.0f, 12.0f}); // past the length

    EXPECT_TRUE(BeamManager::hitsUnit({0, 0}, 0.0f, 10.0f, &on));
    EXPECT_FALSE(BeamManager::hitsUnit({0, 0}, 0.0f, 10.0f, &off));
    EXPECT_FALSE(BeamManager::hitsUnit({0, 0}, 0.0f, 10.0f, &behind));
    EXPECT_FALSE(BeamManager::hitsUnit({0, 0}, 0.0f, 10.0f, &beyond));
}

TEST_F(BeamTest, FireDamagesUnitOnLineAndRecordsGeometry) {
    UnitInstance shooter, target;
    makeUnit(shooter, {0.0f, 0.0f});
    makeUnit(target, {0.0f, 3.0f});

    BeamManager bm;
    bm.beginFrame();
    UnitInstance* targets[] = {&target};
    float len = bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, /*dps=*/100.0f, /*dt=*/0.1f,
                        &shooter, targets, 1, /*weaponId=*/1);

    EXPECT_NEAR(len, 10.0f, 0.01f);
    ASSERT_EQ(bm.beams().size(), 1u);
    EXPECT_EQ(bm.beams()[0].weaponId, 1);

    // Damage is accumulated (100 dps * 0.1 s = 10 raw); flush the realtime tick to realise it.
    updateRealtimeDamage(target.combatState, 0.1f);
    EXPECT_NEAR(target.combatState.currentHealth, 90.0f, 0.01f);
}

TEST_F(BeamTest, FireExcludesShooterAndUnitsBehindWall) {
    UnitInstance shooter, target;
    makeUnit(shooter, {0.0f, 0.0f});
    makeUnit(target, {0.0f, 4.0f});
    makeWall({0.0f, 2.0f}, 3.0f, 0.5f);  // wall at y ∈ [1.5, 2.5] between shooter and target

    BeamManager bm;
    bm.beginFrame();
    UnitInstance* targets[] = {&shooter, &target};  // shooter included in the list on purpose
    float len = bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, 100.0f, 0.1f, &shooter, targets, 2, 1);

    EXPECT_NEAR(len, 1.5f, 0.05f);  // clipped at the wall
    updateRealtimeDamage(target.combatState, 0.1f);
    updateRealtimeDamage(shooter.combatState, 0.1f);
    EXPECT_NEAR(target.combatState.currentHealth, 100.0f, 0.01f) << "target is behind the wall";
    EXPECT_NEAR(shooter.combatState.currentHealth, 100.0f, 0.01f) << "the shooter never damages itself";
}

TEST_F(BeamTest, CastRayReportsWallImpact) {
    makeWall({0.0f, 5.0f}, 3.0f, 0.5f);  // near face at y = 4.5, facing -Y toward the muzzle
    BeamHit hit = BeamManager::castRay(world, {0.0f, 0.0f}, 0.0f, 10.0f);
    EXPECT_TRUE(hit.hitWall);
    EXPECT_NEAR(hit.point.y, 4.5f, 0.05f);
    EXPECT_NEAR(hit.point.x, 0.0f, 0.05f);
    EXPECT_LT(hit.normal.y, -0.5f) << "surface normal points back toward the muzzle (-Y)";
}

TEST_F(BeamTest, CastRayNoWallNoImpact) {
    BeamHit hit = BeamManager::castRay(world, {0.0f, 0.0f}, 0.0f, 10.0f);
    EXPECT_FALSE(hit.hitWall);
    EXPECT_NEAR(hit.length, 10.0f, 0.01f);
    EXPECT_NEAR(hit.point.y, 10.0f, 0.01f);  // point sits at the range end
}

TEST_F(BeamTest, FireRecordsWallImpactForSparks) {
    UnitInstance shooter;
    makeUnit(shooter, {0.0f, 0.0f});
    makeWall({0.0f, 5.0f}, 3.0f, 0.5f);

    BeamManager bm;
    bm.beginFrame();
    bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, 0.0f, 0.0f, &shooter, nullptr, 0, 1);
    ASSERT_EQ(bm.beams().size(), 1u);
    EXPECT_TRUE(bm.beams()[0].hitWall);
    EXPECT_NEAR(bm.beams()[0].hitPoint.y, 4.5f, 0.05f);
}

TEST_F(BeamTest, AnimationFrameCyclesAtBeamFps) {
    BeamManager bm;
    EXPECT_EQ(bm.animFrame(), 0);
    const float frameTime = 1.0f / BEAM_ANIM_FPS;
    bm.update(frameTime);
    EXPECT_EQ(bm.animFrame(), 1);
    bm.update(frameTime);
    EXPECT_EQ(bm.animFrame(), 2);
    bm.update(frameTime);
    EXPECT_EQ(bm.animFrame(), 0) << "wraps after BEAM_FRAME_COUNT frames";
}
