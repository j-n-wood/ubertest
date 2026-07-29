#include <gtest/gtest.h>
#include "effects/effect_manager.h"
#include "units/unit_instance.h"
#include "physics/body_user_data.h"
#include "box2d/box2d.h"

// Explosions apply *realtime* damage: they accumulate raw damage onto a unit's
// combatState.pendingDamage (the unit sim flushes it on the 0.1s tick — see combat_state_test).
class EffectTestFixture : public ::testing::Test {
protected:
    b2WorldId worldId = b2_nullWorldId;
    EffectManager mgr;

    void SetUp() override {
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = {0.0f, 0.0f};
        worldId = b2CreateWorld(&wd);
        mgr.init(worldId);
    }
    void TearDown() override {
        mgr.destroy();
        b2DestroyWorld(worldId);
    }
    // A step puts freshly-created bodies into the broadphase so OverlapAABB sees them.
    void step() { b2World_Step(worldId, 1.0f / 60.0f, 4); }

    void createUnitBody(UnitInstance& u, Vector2 pos, int32_t group) {
        u.collisionGroupId = group;
        u.combatState.maxHealth = 100.0f;
        u.combatState.currentHealth = 100.0f;
        u.combatState.alive = true;
        u.bodyUserData.tag = BodyTag::Unit;
        u.bodyUserData.owner = &u;

        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = {pos.x, pos.y};
        bd.userData = &u.bodyUserData;
        u.bodyId = b2CreateBody(worldId, &bd);

        b2ShapeDef sd = b2DefaultShapeDef();
        sd.filter.categoryBits = CATEGORY_UNIT;
        sd.filter.maskBits = MASK_UNIT;
        sd.filter.groupIndex = group;
        b2Circle c = {{0, 0}, 0.2f};
        b2CreateCircleShape(u.bodyId, &sd, &c);
    }
};

TEST_F(EffectTestFixture, DamagesUnitInRange) {
    UnitInstance u;
    createUnitBody(u, {0.0f, 0.0f}, /*group*/ -2);
    step();

    mgr.spawnExplosion({0.0f, 0.0f}, /*ownerGroup*/ -1);
    mgr.update(0.1f);

    EXPECT_GT(u.combatState.pendingDamage, 0.0f);
    // At the centre (within the core) dps is EXPLOSION_DPS; 0.1s → ~EXPLOSION_DPS*0.1.
    EXPECT_NEAR(u.combatState.pendingDamage, EXPLOSION_DPS * 0.1f, 1e-4f);
}

TEST_F(EffectTestFixture, FalloffCloserTakesMore) {
    UnitInstance near, far;
    createUnitBody(near, {0.0f, 0.0f}, -2);
    createUnitBody(far, {0.6f, 0.0f}, -3);   // still within EXPLOSION_RADIUS (0.75)
    step();

    mgr.spawnExplosion({0.0f, 0.0f}, -1);
    mgr.update(0.1f);

    EXPECT_GT(near.combatState.pendingDamage, 0.0f);
    EXPECT_GT(far.combatState.pendingDamage, 0.0f);
    EXPECT_GT(near.combatState.pendingDamage, far.combatState.pendingDamage);  // 1/r falloff
}

TEST_F(EffectTestFixture, ExcludesSameGroup) {
    UnitInstance u;
    createUnitBody(u, {0.0f, 0.0f}, /*group*/ -1);  // same as ownerGroup below
    step();

    mgr.spawnExplosion({0.0f, 0.0f}, /*ownerGroup*/ -1);
    mgr.update(0.1f);

    EXPECT_FLOAT_EQ(u.combatState.pendingDamage, 0.0f);  // its own blast doesn't hurt it
}

TEST_F(EffectTestFixture, OutOfRangeUntouched) {
    UnitInstance u;
    createUnitBody(u, {3.0f, 0.0f}, -2);   // well beyond EXPLOSION_RADIUS
    step();

    mgr.spawnExplosion({0.0f, 0.0f}, -1);
    mgr.update(0.1f);

    EXPECT_FLOAT_EQ(u.combatState.pendingDamage, 0.0f);
}

TEST_F(EffectTestFixture, ExpiresAfterLifetime) {
    mgr.spawnExplosion({0.0f, 0.0f}, -1);
    ASSERT_EQ(mgr.getEffects().size(), 1u);

    mgr.update(EXPLOSION_LIFETIME + 0.01f);   // past its animation length
    EXPECT_TRUE(mgr.getEffects().empty());
}
