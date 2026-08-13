#include <gtest/gtest.h>
#include "combat/disruptor.h"
#include "units/unit_instance.h"
#include "units/combat_state.h"
#include "units/unit_types.h"
#include "units/weapon.h"
#include "physics/body_user_data.h"
#include "box2d/box2d.h"
#include <vector>

// disruptorBlast: an omnidirectional area zap. It damages every candidate that is not the firer,
// alive, NOT disruptorShielded, within maxRange, and has a clear wall/closed-door line-of-sight from
// the fire position (other units never block). Damage bypasses armour. Mirrors beam_test's bare-world
// harness — units are dynamic circle bodies, walls are static boxes.
class DisruptorTest : public ::testing::Test {
protected:
    b2WorldId world;
    UnitDefinition def;         // collision radius 0.5, energy 100 -> 100 health, armour 20
    UnitDefinition shieldedDef; // same but disruptorShielded = true
    WeaponDefinition disruptor; // damage 40, maxRange 10, area

    void SetUp() override {
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = {0.0f, 0.0f};
        world = b2CreateWorld(&wd);

        def.collisionRadius = 0.5f;
        def.properties.energy = 100;
        def.properties.armour = 20.0f;              // to prove the disruptor bypasses armour

        shieldedDef = def;
        shieldedDef.properties.disruptorShielded = true;

        disruptor.id = 6;
        disruptor.damage = 40.0f;
        disruptor.maxRange = 10.0f;
        disruptor.type = WeaponType::Area;
    }
    void TearDown() override { b2DestroyWorld(world); }

    void makeUnit(UnitInstance& u, Vector2 pos, const UnitDefinition& d) {
        u.definition = &d;
        u.combatState = initCombatState(d.properties);   // health 100
        u.active = true;
        u.bodyUserData.tag = BodyTag::Unit;
        u.bodyUserData.owner = &u;
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = {pos.x, pos.y};
        bd.userData = &u.bodyUserData;
        u.bodyId = b2CreateBody(world, &bd);
        b2Circle circle = {{0, 0}, d.collisionRadius};
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

TEST_F(DisruptorTest, DamagesAllUnitsInRangeAndLos) {
    UnitInstance firer, a, b, c;
    makeUnit(firer, {0.0f, 0.0f}, def);
    makeUnit(a, {3.0f, 0.0f}, def);
    makeUnit(b, {0.0f, -4.0f}, def);
    makeUnit(c, {-2.0f, 2.0f}, def);
    std::vector<UnitInstance*> units = {&firer, &a, &b, &c};

    int hits = disruptorBlast(world, {0.0f, 0.0f}, &firer, disruptor, units);
    EXPECT_EQ(hits, 3);
    // 40 damage, armour bypassed (would be 40-20=20 otherwise) -> 100 -> 60 exactly.
    EXPECT_FLOAT_EQ(a.combatState.currentHealth, 60.0f);
    EXPECT_FLOAT_EQ(b.combatState.currentHealth, 60.0f);
    EXPECT_FLOAT_EQ(c.combatState.currentHealth, 60.0f);
    EXPECT_TRUE(a.damageAlert);
}

TEST_F(DisruptorTest, NeverDamagesTheFirer) {
    UnitInstance firer, a;
    makeUnit(firer, {0.0f, 0.0f}, def);
    makeUnit(a, {2.0f, 0.0f}, def);
    std::vector<UnitInstance*> units = {&firer, &a};

    disruptorBlast(world, {0.0f, 0.0f}, &firer, disruptor, units);
    EXPECT_FLOAT_EQ(firer.combatState.currentHealth, 100.0f);
}

TEST_F(DisruptorTest, SkipsUnitsBeyondMaxRange) {
    UnitInstance firer, near, far;
    makeUnit(firer, {0.0f, 0.0f}, def);
    makeUnit(near, {9.0f, 0.0f}, def);   // within 10
    makeUnit(far, {12.0f, 0.0f}, def);   // beyond 10
    std::vector<UnitInstance*> units = {&firer, &near, &far};

    int hits = disruptorBlast(world, {0.0f, 0.0f}, &firer, disruptor, units);
    EXPECT_EQ(hits, 1);
    EXPECT_LT(near.combatState.currentHealth, 100.0f);
    EXPECT_FLOAT_EQ(far.combatState.currentHealth, 100.0f);
}

TEST_F(DisruptorTest, WallBlocksLineOfSight) {
    UnitInstance firer, exposed, hidden;
    makeUnit(firer, {0.0f, 0.0f}, def);
    makeUnit(exposed, {4.0f, 0.0f}, def);   // clear line along +X
    makeUnit(hidden, {0.0f, 5.0f}, def);    // behind a wall along +Y
    makeWall({0.0f, 2.5f}, 3.0f, 0.3f);     // spans the +Y sightline
    std::vector<UnitInstance*> units = {&firer, &exposed, &hidden};

    int hits = disruptorBlast(world, {0.0f, 0.0f}, &firer, disruptor, units);
    EXPECT_EQ(hits, 1);
    EXPECT_LT(exposed.combatState.currentHealth, 100.0f);
    EXPECT_FLOAT_EQ(hidden.combatState.currentHealth, 100.0f) << "wall blocks LOS";
}

TEST_F(DisruptorTest, ShieldedUnitsAreImmune) {
    UnitInstance firer, normal, shielded;
    makeUnit(firer, {0.0f, 0.0f}, def);
    makeUnit(normal, {2.0f, 0.0f}, def);
    makeUnit(shielded, {-2.0f, 0.0f}, shieldedDef);
    std::vector<UnitInstance*> units = {&firer, &normal, &shielded};

    int hits = disruptorBlast(world, {0.0f, 0.0f}, &firer, disruptor, units);
    EXPECT_EQ(hits, 1);
    EXPECT_LT(normal.combatState.currentHealth, 100.0f);
    EXPECT_FLOAT_EQ(shielded.combatState.currentHealth, 100.0f) << "disruptorShielded is immune";
}

TEST_F(DisruptorTest, AnotherUnitDoesNotBlockLineOfSight) {
    // Units must NOT block the sightline (only geometry does) — a droid behind another droid is
    // still hit, unlike a beam.
    UnitInstance firer, mid, behind;
    makeUnit(firer, {0.0f, 0.0f}, def);
    makeUnit(mid, {3.0f, 0.0f}, def);
    makeUnit(behind, {6.0f, 0.0f}, def);
    std::vector<UnitInstance*> units = {&firer, &mid, &behind};

    int hits = disruptorBlast(world, {0.0f, 0.0f}, &firer, disruptor, units);
    EXPECT_EQ(hits, 2) << "both the near and far unit are hit; units don't shadow each other";
}
