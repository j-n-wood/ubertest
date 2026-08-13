#include <gtest/gtest.h>
#include <fstream>
#include <iterator>
#include <string>
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
   "maxRange": 25.0, "optimumRange": 25.0, "windup": 0.4, "type": "area", "damageType": "disruptor", "twin": false},
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

    // Disruptor area windup (delay before the blast lands); other weapons default to 0.
    EXPECT_FLOAT_EQ(getWeaponDefinition(6).windup, 0.4f);
    EXPECT_FLOAT_EQ(getWeaponDefinition(0).windup, 0.0f);
}

TEST_F(WeaponTestFixture, AllWeaponsLoaded) {
    EXPECT_EQ(weaponCount(), 9);
}

// Per-weapon impact-spark count + colour (weapons.json). Unspecified weapons take the defaults;
// specified ones override (e.g. the plasma cannon test: 24 sparks in bright blue).
TEST(WeaponImpactSparks, ParsedAndDefaulted) {
    const char* js = R"([
      {"id": 0, "name": "Plain", "type": "projectile", "damageType": "plasma", "twin": false},
      {"id": 3, "name": "Cannon", "type": "projectile", "damageType": "plasma", "twin": false,
       "impactSparks": 24, "sparkColor": [80, 160, 255]}
    ])";
    ASSERT_TRUE(loadWeaponsFromJson(js));

    WeaponDefinition plain = getWeaponDefinition(0);
    EXPECT_EQ(plain.impactSparks, DEFAULT_IMPACT_SPARKS);
    EXPECT_EQ(plain.sparkColor.r, DEFAULT_SPARK_COLOR.r);
    EXPECT_EQ(plain.sparkColor.g, DEFAULT_SPARK_COLOR.g);
    EXPECT_EQ(plain.sparkColor.b, DEFAULT_SPARK_COLOR.b);

    WeaponDefinition cannon = getWeaponDefinition(3);
    EXPECT_EQ(cannon.impactSparks, 24);
    EXPECT_EQ(cannon.sparkColor.r, 80);
    EXPECT_EQ(cannon.sparkColor.g, 160);
    EXPECT_EQ(cannon.sparkColor.b, 255);
    EXPECT_EQ(cannon.sparkColor.a, 255);
}

TEST(WeaponImpactSparks, SurvivesSaveRoundTrip) {
    const char* js = R"([
      {"id": 3, "name": "Cannon", "type": "projectile", "damageType": "plasma", "twin": false,
       "impactSparks": 24, "sparkColor": [80, 160, 255]}
    ])";
    ASSERT_TRUE(loadWeaponsFromJson(js));

    std::string path = ::testing::TempDir() + "weapons_sparks_roundtrip.json";
    ASSERT_TRUE(saveWeaponsToFile(path));

    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"impactSparks\": 24"), std::string::npos);
    EXPECT_NE(text.find("\"sparkColor\""), std::string::npos);

    ASSERT_TRUE(loadWeaponsFromFile(path));
    WeaponDefinition c = getWeaponDefinition(3);
    EXPECT_EQ(c.impactSparks, 24);
    EXPECT_EQ(c.sparkColor.b, 255);
}

// Projectile sprite tint (spriteColor) + travel-spark rate: parsed, defaulted, and round-tripped.
TEST(WeaponSpriteAndTravelSparks, ParsedDefaultedAndRoundTrips) {
    const char* js = R"([
      {"id": 0, "name": "Plain", "type": "projectile", "damageType": "plasma", "twin": false},
      {"id": 1, "name": "Glowing", "type": "projectile", "damageType": "plasma", "twin": false,
       "spriteColor": [140, 255, 170], "travelSparkRate": 22,
       "travelSparkLife": 0.7, "travelSparkSize": 0.2, "travelSparkJitter": 0.15}
    ])";
    ASSERT_TRUE(loadWeaponsFromJson(js));

    // Defaults: white sprite tint (texture unchanged), no travel sparks, default spark life/size, no jitter.
    WeaponDefinition plain = getWeaponDefinition(0);
    EXPECT_EQ(plain.spriteColor.r, DEFAULT_SPRITE_COLOR.r);
    EXPECT_EQ(plain.spriteColor.g, DEFAULT_SPRITE_COLOR.g);
    EXPECT_EQ(plain.spriteColor.b, DEFAULT_SPRITE_COLOR.b);
    EXPECT_FLOAT_EQ(plain.travelSparkRate, 0.0f);
    EXPECT_FLOAT_EQ(plain.travelSparkLife, DEFAULT_TRAVEL_SPARK_LIFE);
    EXPECT_FLOAT_EQ(plain.travelSparkSize, DEFAULT_TRAVEL_SPARK_SIZE);
    EXPECT_FLOAT_EQ(plain.travelSparkJitter, 0.0f);

    // Parsed values, including the tunable lifetime + size + jitter.
    WeaponDefinition g = getWeaponDefinition(1);
    EXPECT_EQ(g.spriteColor.r, 140);
    EXPECT_EQ(g.spriteColor.g, 255);
    EXPECT_EQ(g.spriteColor.b, 170);
    EXPECT_FLOAT_EQ(g.travelSparkRate, 22.0f);
    EXPECT_FLOAT_EQ(g.travelSparkLife, 0.7f);
    EXPECT_FLOAT_EQ(g.travelSparkSize, 0.2f);
    EXPECT_FLOAT_EQ(g.travelSparkJitter, 0.15f);

    // Round-trip: non-default fields are written; defaults are omitted and re-default on reload.
    std::string path = ::testing::TempDir() + "weapons_sprite_roundtrip.json";
    ASSERT_TRUE(saveWeaponsToFile(path));
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"spriteColor\""), std::string::npos);
    EXPECT_NE(text.find("\"travelSparkRate\""), std::string::npos);
    EXPECT_NE(text.find("\"travelSparkLife\""), std::string::npos);
    EXPECT_NE(text.find("\"travelSparkSize\""), std::string::npos);
    EXPECT_NE(text.find("\"travelSparkJitter\""), std::string::npos);

    ASSERT_TRUE(loadWeaponsFromFile(path));
    EXPECT_EQ(getWeaponDefinition(1).spriteColor.g, 255);
    EXPECT_FLOAT_EQ(getWeaponDefinition(1).travelSparkRate, 22.0f);
    EXPECT_FLOAT_EQ(getWeaponDefinition(1).travelSparkLife, 0.7f);
    EXPECT_FLOAT_EQ(getWeaponDefinition(1).travelSparkSize, 0.2f);
    EXPECT_FLOAT_EQ(getWeaponDefinition(1).travelSparkJitter, 0.15f);
    // Defaults omitted on save → still default on reload.
    EXPECT_EQ(getWeaponDefinition(0).spriteColor.r, DEFAULT_SPRITE_COLOR.r);
    EXPECT_FLOAT_EQ(getWeaponDefinition(0).travelSparkRate, 0.0f);
    EXPECT_FLOAT_EQ(getWeaponDefinition(0).travelSparkLife, DEFAULT_TRAVEL_SPARK_LIFE);
}

// Projectile lifetime is decoupled from maxRange: an explicit `lifetime` controls travel, else it
// derives from maxRange/speed (back-compat). maxRange itself is the AI fire gate only.
TEST(WeaponLifetime, ExplicitOverridesElseDerivesFromRange) {
    const char* js = R"([
      {"id": 0, "name": "Derived",  "speed": 10.0, "maxRange": 20.0, "type": "projectile"},
      {"id": 1, "name": "Explicit", "speed": 10.0, "maxRange": 20.0, "lifetime": 0.3, "type": "projectile"}
    ])";
    ASSERT_TRUE(loadWeaponsFromJson(js));

    // Unspecified: raw field 0, resolver derives maxRange/speed = 20/10 = 2.0.
    WeaponDefinition d = getWeaponDefinition(0);
    EXPECT_FLOAT_EQ(d.lifetime, 0.0f);
    EXPECT_FLOAT_EQ(weaponProjectileLifetime(d), 2.0f);

    // Explicit: wins, independent of maxRange.
    WeaponDefinition e = getWeaponDefinition(1);
    EXPECT_FLOAT_EQ(e.lifetime, 0.3f);
    EXPECT_FLOAT_EQ(weaponProjectileLifetime(e), 0.3f);

    // Round-trip: explicit written, derived omitted.
    std::string path = ::testing::TempDir() + "weapons_lifetime.json";
    ASSERT_TRUE(saveWeaponsToFile(path));
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("\"lifetime\""), std::string::npos);
    ASSERT_TRUE(loadWeaponsFromFile(path));
    EXPECT_FLOAT_EQ(getWeaponDefinition(1).lifetime, 0.3f);
    EXPECT_FLOAT_EQ(getWeaponDefinition(0).lifetime, 0.0f);   // still derived on reload
}

// The runtime weapon editor tunes the table in place (getWeaponByIndex) and writes it
// back with saveWeaponsToFile. Edits must round-trip, and the emitted numbers must be
// clean (no float->double artefacts like 12.5 -> "12.500000476837158").
TEST_F(WeaponTestFixture, SaveRoundTripAndCleanFormatting) {
    WeaponDefinition* w = getWeaponByIndex(0);   // Plasma Bolt
    ASSERT_NE(w, nullptr);
    w->damage = 12.5f;
    w->fireRate = 0.45f;

    std::string path = ::testing::TempDir() + "weapons_roundtrip.json";
    ASSERT_TRUE(saveWeaponsToFile(path));

    // File contents: values are written in the clean shortest form, radius omitted at default.
    std::ifstream in(path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("12.5"), std::string::npos);
    EXPECT_NE(text.find("0.45"), std::string::npos);
    EXPECT_EQ(text.find("12.500000"), std::string::npos);  // no float artefact
    EXPECT_EQ(text.find("0.450000"), std::string::npos);

    // Reloading yields the edited values and preserves the non-numeric fields.
    ASSERT_TRUE(loadWeaponsFromFile(path));
    EXPECT_EQ(weaponCount(), 9);
    WeaponDefinition r = getWeaponDefinition(0);
    EXPECT_FLOAT_EQ(r.damage, 12.5f);
    EXPECT_FLOAT_EQ(r.fireRate, 0.45f);
    EXPECT_EQ(r.name, "Plasma Bolt");
    EXPECT_EQ(r.type, WeaponType::Projectile);
    EXPECT_FALSE(r.twin);
    // Radius stayed at the default (omitted on save, re-defaulted on load).
    EXPECT_FLOAT_EQ(r.radius, 0.1f);
}

// The shipped asset file (loaded at game_init via loadWeaponsFromFile) must define
// weapon 0 = Plasma Bolt with the phase-1 stats the game/AI/tests rely on.
TEST(WeaponFileTest, ShippedPlasmaBolt) {
    std::string path = std::string(TEST_PROJECT_ROOT) + "/cpp-version/assets/data/weapons.json";
    ASSERT_TRUE(loadWeaponsFromFile(path)) << "failed to load " << path;

    WeaponDefinition w = getWeaponDefinition(0);
    EXPECT_EQ(w.id, 0);
    EXPECT_EQ(w.name, "Plasma Bolt");
    EXPECT_FLOAT_EQ(w.damage, 20.599f);
    EXPECT_FLOAT_EQ(w.speed, 11.0f);
    EXPECT_FLOAT_EQ(w.fireRate, 0.8f);
    EXPECT_EQ(w.type, WeaponType::Projectile);
    EXPECT_EQ(w.damageType, DamageType::Plasma);
    EXPECT_FLOAT_EQ(w.radius, 0.1f);  // default physics radius
    // Plasma bolt sheds coloured travel sparks (added feature). Rate is a tunable balance value,
    // so just assert it's enabled; the sprite tint is asserted exactly.
    EXPECT_GT(w.travelSparkRate, 0.0f);
    EXPECT_EQ(w.spriteColor.r, 140);
    EXPECT_EQ(w.spriteColor.g, 255);
    EXPECT_EQ(w.spriteColor.b, 170);

    // Weapon 3 (Plasma Cannon) has a configured, larger physics radius.
    EXPECT_FLOAT_EQ(getWeaponDefinition(3).radius, 0.2f);
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

TEST_F(ProjectileTestFixture, RecordsImpactOnHit) {
    // A projectile that hits something records an impact (point/normal/incident/weaponId) for
    // the render layer to spawn impact sparks; a clean flight records none.
    UnitInstance target;
    target.combatState = {100.0f, 100.0f, 0.0f, true};
    createUnitBody(target, {5.0f, 0.0f}, 0.5f, -2);

    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 40.0f, 5.0f, /*owner*/ -1, /*weaponId*/ 7);

    bool sawImpact = false;
    ProjectileImpact rec{};
    for (int i = 0; i < 100; ++i) {
        step(0.01f);
        mgr.processContactEvents(worldId);
        if (!mgr.impacts().empty()) { rec = mgr.impacts().front(); sawImpact = true; }
        if (mgr.activeCount() == 0) break;
    }

    ASSERT_TRUE(sawImpact);
    EXPECT_EQ(rec.weaponId, 7);
    EXPECT_GT(rec.incident.x, 0.9f);  // travelling +X
    EXPECT_NEAR(rec.point.y, 0.0f, 0.2f);
    // Impact clears on a step with no new contact.
    step(0.01f);
    mgr.processContactEvents(worldId);
    EXPECT_TRUE(mgr.impacts().empty());
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

TEST_F(ProjectileTestFixture, CarriesWeaponId) {
    // weaponId rides on the projectile for per-weapon rendering (plasma vs laser sprite).
    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 5.0f, 5.0f, /*owner*/ -1, /*weaponId*/ 4);
    const auto& ps = mgr.getProjectiles();
    ASSERT_EQ(ps.size(), 1u);
    EXPECT_EQ(ps[0].weaponId, 4);
}

TEST_F(ProjectileTestFixture, CarriesRadiusAndAge) {
    // Per-weapon physics radius rides on the projectile; age advances with update() and
    // drives per-instance sprite animation.
    mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 5.0f, 5.0f, /*owner*/ -1, /*weaponId*/ 3, /*radius*/ 0.2f);
    const auto& ps = mgr.getProjectiles();
    ASSERT_EQ(ps.size(), 1u);
    EXPECT_FLOAT_EQ(ps[0].radius, 0.2f);
    EXPECT_FLOAT_EQ(ps[0].age, 0.0f);

    mgr.update(0.1f);
    EXPECT_NEAR(mgr.getProjectiles()[0].age, 0.1f, 1e-5f);
}

TEST_F(ProjectileTestFixture, ManyProjectilesStillDeactivateOnHit) {
    // Regression: each body stores &Projectile::userData. Growing/compacting the internal
    // vector used to leave those pointers dangling, so projectiles spawned after the first
    // reallocation were never identified on contact and never deactivated (they bounced /
    // lodged in corners). Spawn enough to force several reallocations, fire them all into a
    // target, and require every one to deal its damage once and then go inactive.
    UnitInstance target;
    target.combatState = {1000.0f, 1000.0f, 0.0f, true};
    createUnitBody(target, {5.0f, 0.0f}, 0.5f, -2);

    constexpr int N = 64;  // std::vector growth reallocates several times before this
    for (int i = 0; i < N; ++i) {
        mgr.spawn(worldId, {0, 0}, {1, 0}, 10.0f, 5.0f, 5.0f, -1);
    }
    ASSERT_EQ(mgr.activeCount(), N);

    for (int i = 0; i < 300; ++i) {
        step(0.01f);
        mgr.processContactEvents(worldId);
        mgr.cleanup();  // compacts survivors — exercises the re-bind path too
        if (mgr.activeCount() == 0) break;
    }

    EXPECT_EQ(mgr.activeCount(), 0) << "some projectiles never deactivated (stale userData)";
    EXPECT_FLOAT_EQ(target.combatState.currentHealth, 1000.0f - N * 5.0f);
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
