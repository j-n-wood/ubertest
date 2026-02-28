#include <gtest/gtest.h>
#include "units/weapon.h"
#include "combat/projectile_manager.h"
#include "units/unit_instance.h"

//------------------------------------------------------------------------------
// Test fixture — loads weapon data from inline JSON
//------------------------------------------------------------------------------

static const char* TEST_WEAPONS_JSON = R"([
  {"id": 0, "name": "Plasma Bolt", "damage": 11.0, "speed": 17.5, "fireRate": 0.8,
   "maxRange": 20.0, "optimumRange": 10.0, "type": "projectile", "damageType": "plasma", "twin": false},
  {"id": 1, "name": "Gas Axe", "damage": 3.5, "speed": 12.5, "fireRate": 1.2,
   "maxRange": 7.5, "optimumRange": 5.0, "type": "beam", "damageType": "cutter", "twin": false},
  {"id": 2, "name": "Laser Rifle", "damage": 20.0, "speed": 20.0, "fireRate": 0.6,
   "maxRange": 20.0, "optimumRange": 15.0, "type": "projectile", "damageType": "laser", "twin": false},
  {"id": 3, "name": "Plasma Cannon", "damage": 33.0, "speed": 12.5, "fireRate": 1.1,
   "maxRange": 20.0, "optimumRange": 10.0, "type": "projectile", "damageType": "plasma", "twin": false},
  {"id": 4, "name": "Rapid Laser", "damage": 16.0, "speed": 20.0, "fireRate": 0.45,
   "maxRange": 25.0, "optimumRange": 15.0, "type": "projectile", "damageType": "laser", "twin": false},
  {"id": 5, "name": "Plasma Torch", "damage": 6.0, "speed": 27.5, "fireRate": 0.15,
   "maxRange": 9.0, "optimumRange": 7.5, "type": "projectile", "damageType": "plasma", "twin": false},
  {"id": 6, "name": "Disruptor", "damage": 40.0, "speed": 0.0, "fireRate": 1.7,
   "maxRange": 25.0, "optimumRange": 25.0, "type": "area", "damageType": "disruptor", "twin": false},
  {"id": 7, "name": "Twin Particle Cannon", "damage": 22.0, "speed": 16.0, "fireRate": 1.1,
   "maxRange": 20.0, "optimumRange": 10.0, "type": "projectile", "damageType": "plasma", "twin": true},
  {"id": 8, "name": "Exterminator", "damage": 6.0, "speed": 12.5, "fireRate": 1.2,
   "maxRange": 8.5, "optimumRange": 8.5, "type": "beam", "damageType": "cutter", "twin": false}
])";

class WeaponTestFixture : public ::testing::Test {
protected:
    void SetUp() override {
        ASSERT_TRUE(loadWeaponsFromJson(TEST_WEAPONS_JSON));
    }
};

//------------------------------------------------------------------------------
// Weapon tests
//------------------------------------------------------------------------------

TEST_F(WeaponTestFixture, CooldownPreventsRapidFire) {
    // Plasma Torch (id=5) has fireRate=0.15s
    DroidProperties props;
    props.weapon = 5;

    WeaponState state = initWeaponState(props);
    ASSERT_TRUE(hasWeapon(state));
    EXPECT_FLOAT_EQ(state.definition.fireRate, 0.15f);

    // First shot fires
    EXPECT_TRUE(tryFire(state));
    // Immediate second shot blocked
    EXPECT_FALSE(tryFire(state));

    // Partial cooldown — still blocked
    updateWeaponCooldown(state, 0.10f);
    EXPECT_FALSE(tryFire(state));

    // Full cooldown elapsed — can fire again
    updateWeaponCooldown(state, 0.10f);
    EXPECT_TRUE(tryFire(state));
}

TEST_F(WeaponTestFixture, DamageScalesWithWeaponStat) {
    // Original weapon data: Gas Axe(1)=3.5, Laser Rifle(2)=20, Plasma Cannon(3)=33
    WeaponDefinition w1 = getWeaponDefinition(1); // Gas Axe
    WeaponDefinition w2 = getWeaponDefinition(2); // Laser Rifle
    WeaponDefinition w3 = getWeaponDefinition(3); // Plasma Cannon

    EXPECT_FLOAT_EQ(w1.damage, 3.5f);
    EXPECT_FLOAT_EQ(w2.damage, 20.0f);
    EXPECT_FLOAT_EQ(w3.damage, 33.0f);

    EXPECT_GT(w2.damage, w1.damage);
    EXPECT_GT(w3.damage, w2.damage);
}

TEST_F(WeaponTestFixture, NoWeaponForNegativeId) {
    // Unarmed units have weapon=-1
    WeaponDefinition w = getWeaponDefinition(-1);
    EXPECT_EQ(w.id, -1);
    EXPECT_FLOAT_EQ(w.damage, 0.0f);

    // initWeaponState with -1 should produce no weapon
    DroidProperties props;
    props.weapon = -1;
    WeaponState state = initWeaponState(props);
    EXPECT_FALSE(hasWeapon(state));
    EXPECT_FALSE(tryFire(state));
}

TEST_F(WeaponTestFixture, WeaponTypesParsed) {
    // Verify weapon types from original data
    EXPECT_EQ(getWeaponDefinition(0).type, WeaponType::Projectile); // Plasma Bolt
    EXPECT_EQ(getWeaponDefinition(1).type, WeaponType::Beam);       // Gas Axe
    EXPECT_EQ(getWeaponDefinition(6).type, WeaponType::Area);       // Disruptor

    // Twin flag
    EXPECT_FALSE(getWeaponDefinition(3).twin);
    EXPECT_TRUE(getWeaponDefinition(7).twin);

    // Damage types
    EXPECT_EQ(getWeaponDefinition(2).damageType, DamageType::Laser);
    EXPECT_EQ(getWeaponDefinition(6).damageType, DamageType::Disruptor);
    EXPECT_EQ(getWeaponDefinition(8).damageType, DamageType::Cutter);
}

TEST_F(WeaponTestFixture, AllWeaponsLoaded) {
    EXPECT_EQ(weaponCount(), 9);
}

//------------------------------------------------------------------------------
// Projectile tests — Box2D physics simulation
//------------------------------------------------------------------------------

class ProjectileTestFixture : public ::testing::Test {
protected:
    b2WorldId worldId = b2_nullWorldId;
    ProjectileManager mgr;

    void SetUp() override {
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {0.0f, 0.0f};
        worldId = b2CreateWorld(&worldDef);
    }

    void TearDown() override {
        b2DestroyWorld(worldId);
    }

    void step(float dt) {
        b2World_Step(worldId, dt, 4);
    }

    // Create a unit body in the world for hit detection tests.
    // Sets up BodyUserData on the UnitInstance so processContactEvents can find it.
    void createUnitBody(UnitInstance& unit, Vector2 pos, float radius, int32_t groupIndex) {
        unit.collisionGroupId = groupIndex;
        unit.bodyUserData.tag = BodyTag::Unit;
        unit.bodyUserData.owner = &unit;

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {pos.x, pos.y};
        bodyDef.userData = &unit.bodyUserData;
        unit.bodyId = b2CreateBody(worldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.filter.categoryBits = CATEGORY_UNIT;
        shapeDef.filter.maskBits = MASK_UNIT;
        shapeDef.filter.groupIndex = groupIndex;
        shapeDef.enableContactEvents = true;

        b2Circle circle = {{0, 0}, radius};
        b2CreateCircleShape(unit.bodyId, &shapeDef, &circle);
    }
};

TEST_F(ProjectileTestFixture, MovesAlongHeading) {
    // Fire projectile along +X axis at speed 10
    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 50.0f, 10.0f, -1);

    // After 1 second it should be at x=10
    step(1.0f);
    mgr.syncFromPhysics();

    const auto& projectiles = mgr.getProjectiles();
    ASSERT_EQ(projectiles.size(), 1);
    EXPECT_NEAR(projectiles[0].position.x, 10.0f, 0.1f);
    EXPECT_NEAR(projectiles[0].position.y, 0.0f, 0.1f);
    EXPECT_TRUE(projectiles[0].active);
}

TEST_F(ProjectileTestFixture, ExpiresAtLifetime) {
    // Fire with lifetime 0.5s
    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 50.0f, 0.5f, -1);

    // After 0.3s: still active
    mgr.update(0.3f);
    EXPECT_EQ(mgr.activeCount(), 1);

    // After another 0.3s: total 0.6s > 0.5s lifetime — expired
    mgr.update(0.3f);
    EXPECT_EQ(mgr.activeCount(), 0);
}

TEST_F(ProjectileTestFixture, HitsTarget) {
    // Create target unit at (5, 0) with radius 0.5
    UnitInstance target;
    target.combatState = {100.0f, 100.0f, 0.0f, true};
    createUnitBody(target, {5.0f, 0.0f}, 0.5f, -2);

    // Fire projectile along +X at speed 10, damage 40
    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 40.0f, 5.0f, -1);

    // Step in small increments, processing contact events after each step.
    // Contact events are only valid for the step in which they occur.
    for (int i = 0; i < 100; ++i) {
        step(0.01f);
        mgr.processContactEvents(worldId);
        if (mgr.activeCount() == 0) break;
    }

    EXPECT_FLOAT_EQ(target.combatState.currentHealth, 60.0f); // 100 - 40
    EXPECT_EQ(mgr.activeCount(), 0); // Projectile consumed
}

TEST_F(ProjectileTestFixture, MissesDistantTarget) {
    // Target at (5, 3) — projectile along +X will miss
    UnitInstance target;
    target.combatState = {100.0f, 100.0f, 0.0f, true};
    createUnitBody(target, {5.0f, 3.0f}, 0.5f, -2);

    // Fire along +X from origin
    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 40.0f, 5.0f, -1);

    // Step 0.5s — projectile at (5, 0), target at (5, 3) — no collision
    step(0.5f);
    mgr.processContactEvents(worldId);

    EXPECT_FLOAT_EQ(target.combatState.currentHealth, 100.0f); // No damage
    EXPECT_EQ(mgr.activeCount(), 1); // Projectile still active
}

TEST_F(ProjectileTestFixture, IgnoresOwner) {
    // Target and projectile share groupIndex -1 — Box2D won't collide them
    UnitInstance target;
    target.combatState = {100.0f, 100.0f, 0.0f, true};
    createUnitBody(target, {5.0f, 0.0f}, 0.5f, -1);

    // Fire with ownerId=-1 (same negative groupIndex as target)
    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 40.0f, 5.0f, -1);

    // Step 0.5s — projectile passes through target
    step(0.5f);
    mgr.processContactEvents(worldId);

    EXPECT_FLOAT_EQ(target.combatState.currentHealth, 100.0f); // No self-damage
    EXPECT_EQ(mgr.activeCount(), 1); // Projectile passes through
}
