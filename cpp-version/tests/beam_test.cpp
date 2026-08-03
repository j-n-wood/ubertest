#include <gtest/gtest.h>
#include "combat/beam_manager.h"
#include "units/unit_instance.h"
#include "units/combat_state.h"
#include "units/unit_types.h"
#include "physics/body_user_data.h"
#include "box2d/box2d.h"

// Beam simulation: the hitscan ray stops at the first wall, closed door, OR unit (other than
// the shooter). The unit it stops on takes continuous damage; anything behind it is shielded.
// Rendering is not covered.
//
// Angle convention matches the game: forward = {-sin a, cos a}. All beams below fire north
// (+Y) with angle 0, forward {0, 1}, so units/walls are placed along +Y. Units have radius
// 0.5, so a ray from the origin hits a unit centred at (0, d) at its front face y = d - 0.5.
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

    // A unit with a dynamic body tagged so the beam ray can identify it (and skip the shooter).
    void makeUnit(UnitInstance& u, Vector2 pos) {
        u.definition = &def;
        u.combatState = initCombatState(def.properties);  // health 100, armour 0
        u.active = true;
        u.bodyUserData.tag = BodyTag::Unit;
        u.bodyUserData.owner = &u;
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = {pos.x, pos.y};
        bd.userData = &u.bodyUserData;
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

TEST_F(BeamTest, CastLengthReachesMaxRangeWithNoObstacle) {
    EXPECT_NEAR(BeamManager::castLength(world, {0.0f, 0.0f}, 0.0f, 10.0f), 10.0f, 0.01f);
}

TEST_F(BeamTest, CastLengthTruncatesAtWall) {
    makeWall({0.0f, 5.0f}, 3.0f, 0.5f);  // spans y ∈ [4.5, 5.5]
    EXPECT_NEAR(BeamManager::castLength(world, {0.0f, 0.0f}, 0.0f, 10.0f), 4.5f, 0.05f);
}

TEST_F(BeamTest, CastRayStopsAtUnit) {
    UnitInstance u;
    makeUnit(u, {0.0f, 3.0f});  // front face at y = 2.5
    BeamHit hit = BeamManager::castRay(world, {0.0f, 0.0f}, 0.0f, 10.0f);
    EXPECT_FALSE(hit.hitWall);
    EXPECT_EQ(hit.unit, &u);
    EXPECT_NEAR(hit.length, 2.5f, 0.1f);
}

TEST_F(BeamTest, CastRayReportsWallImpact) {
    makeWall({0.0f, 5.0f}, 3.0f, 0.5f);  // near face y = 4.5, facing -Y toward the muzzle
    BeamHit hit = BeamManager::castRay(world, {0.0f, 0.0f}, 0.0f, 10.0f);
    EXPECT_TRUE(hit.hitWall);
    EXPECT_EQ(hit.unit, nullptr);
    EXPECT_NEAR(hit.point.y, 4.5f, 0.05f);
    EXPECT_LT(hit.normal.y, -0.5f) << "surface normal points back toward the muzzle (-Y)";
}

TEST_F(BeamTest, CastRayNoObstacleNoImpact) {
    BeamHit hit = BeamManager::castRay(world, {0.0f, 0.0f}, 0.0f, 10.0f);
    EXPECT_FALSE(hit.hitWall);
    EXPECT_EQ(hit.unit, nullptr);
    EXPECT_NEAR(hit.length, 10.0f, 0.01f);
    EXPECT_NEAR(hit.point.y, 10.0f, 0.01f);  // point sits at the range end
}

TEST_F(BeamTest, FireDamagesFirstUnitAndRecordsGeometry) {
    UnitInstance shooter, target;
    makeUnit(shooter, {0.0f, 0.0f});
    makeUnit(target, {0.0f, 3.0f});

    BeamManager bm;
    bm.beginFrame();
    float len = bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, /*dps=*/100.0f, /*dt=*/0.1f,
                        &shooter, /*weaponId=*/1);

    EXPECT_NEAR(len, 2.5f, 0.1f);  // stops at the target's front face
    ASSERT_EQ(bm.beams().size(), 1u);
    EXPECT_EQ(bm.beams()[0].weaponId, 1);
    EXPECT_TRUE(bm.beams()[0].hit) << "a unit impact clips the beam and spawns sparks";

    // 100 dps * 0.1 s = 10 raw, accumulated; flush the realtime tick to realise it.
    updateRealtimeDamage(target.combatState, 0.1f);
    EXPECT_NEAR(target.combatState.currentHealth, 90.0f, 0.01f);
}

TEST_F(BeamTest, BeamStopsAtFirstUnitShieldingThoseBehind) {
    UnitInstance shooter, nearU, farU;
    makeUnit(shooter, {0.0f, 0.0f});
    makeUnit(nearU, {0.0f, 3.0f});
    makeUnit(farU, {0.0f, 6.0f});

    BeamManager bm;
    bm.beginFrame();
    bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, 100.0f, 0.1f, &shooter, 1);

    updateRealtimeDamage(nearU.combatState, 0.1f);
    updateRealtimeDamage(farU.combatState, 0.1f);
    EXPECT_LT(nearU.combatState.currentHealth, 100.0f) << "near unit is hit";
    EXPECT_NEAR(farU.combatState.currentHealth, 100.0f, 0.01f) << "far unit is shielded by the near one";
}

TEST_F(BeamTest, FireNeverDamagesTheShooter) {
    UnitInstance shooter, target;
    makeUnit(shooter, {0.0f, 0.0f});  // the muzzle sits on the shooter
    makeUnit(target, {0.0f, 3.0f});

    BeamManager bm;
    bm.beginFrame();
    bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, 100.0f, 0.1f, &shooter, 1);

    updateRealtimeDamage(shooter.combatState, 0.1f);
    EXPECT_NEAR(shooter.combatState.currentHealth, 100.0f, 0.01f);
}

TEST_F(BeamTest, WallBeforeUnitBlocksDamageAndSparks) {
    UnitInstance shooter, target;
    makeUnit(shooter, {0.0f, 0.0f});
    makeWall({0.0f, 2.0f}, 3.0f, 0.5f);  // wall front at y = 1.5
    makeUnit(target, {0.0f, 4.0f});      // behind the wall

    BeamManager bm;
    bm.beginFrame();
    float len = bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, 100.0f, 0.1f, &shooter, 1);

    EXPECT_NEAR(len, 1.5f, 0.05f);
    ASSERT_EQ(bm.beams().size(), 1u);
    EXPECT_TRUE(bm.beams()[0].hit) << "stopped on the wall → spawns sparks";
    updateRealtimeDamage(target.combatState, 0.1f);
    EXPECT_NEAR(target.combatState.currentHealth, 100.0f, 0.01f) << "target is behind the wall";
}

TEST_F(BeamTest, FireWithNoObstacleDoesNotSpark) {
    UnitInstance shooter;
    makeUnit(shooter, {0.0f, 0.0f});
    BeamManager bm;
    bm.beginFrame();
    bm.fire(world, {0.0f, 0.0f}, 0.0f, 10.0f, 100.0f, 0.1f, &shooter, 1);
    ASSERT_EQ(bm.beams().size(), 1u);
    EXPECT_FALSE(bm.beams()[0].hit) << "reached maxRange with nothing in the way";
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
