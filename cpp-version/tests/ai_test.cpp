#include <gtest/gtest.h>
#include "ai/ai_manager.h"
#include "units/unit_instance.h"
#include "units/movement_tuning.h"
#include "units/weapon.h"
#include "combat/projectile_manager.h"
#include "physics/body_user_data.h"
#include "level/spawn_config.h"

//------------------------------------------------------------------------------
// Weapon data (same as weapon_test.cpp — loaded once for the fixture)
//------------------------------------------------------------------------------

static const char* TEST_WEAPONS_JSON = R"([
  {"id": 0, "name": "Plasma Bolt", "damage": 11.0, "speed": 17.5, "fireRate": 0.8,
   "maxRange": 20.0, "optimumRange": 10.0, "type": "projectile", "damageType": "plasma", "twin": false},
  {"id": 6, "name": "Disruptor", "damage": 40.0, "speed": 0.0, "fireRate": 1.7,
   "maxRange": 25.0, "optimumRange": 25.0, "type": "area", "damageType": "disruptor", "twin": false}
])";

//------------------------------------------------------------------------------
// Test fixture — sets up a small waypoint graph and Box2D world
//------------------------------------------------------------------------------
//
//  Waypoint layout (2D):
//
//      0 ---- 1 ---- 2
//             |
//             3
//             |
//             4
//
//  Positions:
//    0: (0, 0)    1: (5, 0)    2: (10, 0)    3: (5, 5)    4: (5, 10)
//

class AITestFixture : public ::testing::Test {
protected:
    b2WorldId worldId = b2_nullWorldId;
    b2BodyId originBody = b2_nullBodyId;  // anchor for unit motor joints
    ProjectileManager projectiles;
    AIManager aiManager;

    // Waypoint graph
    std::vector<Vector3> waypointPositions;
    std::vector<std::vector<int>> adjacency;

    // Unit definitions and instances
    UnitDefinition armedDef;
    UnitDefinition unarmedDef;
    UnitDefinition turretDef;
    UnitDefinition omniDef;
    UnitDefinition disruptorDef;
    UnitDefinition headDef;
    UnitDefinition fwmDef;

    void SetUp() override {
        ASSERT_TRUE(loadWeaponsFromJson(TEST_WEAPONS_JSON));

        // Create Box2D world
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {0.0f, 0.0f};
        worldId = b2CreateWorld(&worldDef);

        // Motor-joint anchor at the world origin (matches UnitManager). Units get
        // their movement joint attached in createUnitBody() below.
        originBody = unit_create_origin_body(worldId);

        // Waypoint positions (Vector3: x = X, y = height, z = Y in 2D)
        waypointPositions = {
            {0.0f,  0.0f, 0.0f},   // 0
            {5.0f,  0.0f, 0.0f},   // 1
            {10.0f, 0.0f, 0.0f},   // 2
            {5.0f,  0.0f, 5.0f},   // 3
            {5.0f,  0.0f, 10.0f},  // 4
        };

        // Adjacency: 0-1, 1-2, 1-3, 3-4
        adjacency = {
            {1},       // 0 -> 1
            {0, 2, 3}, // 1 -> 0, 2, 3
            {1},       // 2 -> 1
            {1, 4},    // 3 -> 1, 4
            {3},       // 4 -> 3
        };

        // Set up unit definitions
        setupArmedDef();
        setupUnarmedDef();
        setupTurretDef();
        setupOmniDef();
        setupDisruptorDef();
        setupHeadDef();
        setupFireWhileMovingDef();
    }

    void TearDown() override {
        b2DestroyWorld(worldId);
    }

    void step(float dt) {
        b2World_Step(worldId, dt, 4);
    }

    void setupArmedDef() {
        armedDef.name = "armed";
        armedDef.id = "armed";
        armedDef.collisionRadius = 0.5f;
        armedDef.proximityRadius = 8.0f;
        armedDef.properties.weapon = 0;  // Plasma Bolt
        armedDef.properties.hasTurret = false;
        armedDef.properties.omnidirectional = false;
        armedDef.properties.visualRadius = 15.0f;
    }

    void setupUnarmedDef() {
        unarmedDef.name = "unarmed";
        unarmedDef.id = "unarmed";
        unarmedDef.collisionRadius = 0.5f;
        unarmedDef.proximityRadius = 5.0f;
        unarmedDef.properties.weapon = -1;
        unarmedDef.properties.hasTurret = false;
        unarmedDef.properties.omnidirectional = false;
        unarmedDef.properties.visualRadius = 10.0f;
    }

    void setupTurretDef() {
        turretDef.name = "turret";
        turretDef.id = "turret";
        turretDef.collisionRadius = 0.5f;
        turretDef.proximityRadius = 8.0f;
        turretDef.properties.weapon = 0;
        turretDef.properties.hasTurret = true;
        turretDef.properties.omnidirectional = false;
        turretDef.properties.visualRadius = 15.0f;
        // Add a turret-role section (the aiming part that drives the firing angle).
        // Role implies FollowFacing; set the rotationMode too since the fixture builds
        // the definition directly rather than via the JSON loader (which coerces it).
        turretDef.rootSection.name = "body";
        turretDef.rootSection.rotationMode = SectionRotationMode::FollowUnit;
        SectionDefinition headSection;
        headSection.name = "head";
        headSection.role = SectionRole::Turret;
        headSection.rotationMode = SectionRotationMode::FollowFacing;
        turretDef.rootSection.children.push_back(std::move(headSection));
    }

    void setupHeadDef() {
        headDef.name = "head";
        headDef.id = "head";
        headDef.collisionRadius = 0.5f;
        headDef.proximityRadius = 8.0f;
        headDef.properties.weapon = 0;
        headDef.properties.visualRadius = 15.0f;
        headDef.rootSection.name = "body";
        SectionDefinition sensor;
        sensor.name = "sensor";
        sensor.role = SectionRole::Head;
        sensor.rotationMode = SectionRotationMode::FollowFacing;
        headDef.rootSection.children.push_back(std::move(sensor));
    }

    void setupFireWhileMovingDef() {
        fwmDef.name = "fwm";
        fwmDef.id = "fwm";
        fwmDef.collisionRadius = 0.5f;
        fwmDef.proximityRadius = 8.0f;
        fwmDef.properties.weapon = 0;
        fwmDef.properties.fireWhileMoving = true;
        fwmDef.properties.visualRadius = 15.0f;
    }

    void setupOmniDef() {
        omniDef.name = "omni";
        omniDef.id = "omni";
        omniDef.collisionRadius = 0.5f;
        omniDef.proximityRadius = 8.0f;
        omniDef.properties.weapon = 0;
        omniDef.properties.hasTurret = false;
        omniDef.properties.omnidirectional = true;
        omniDef.properties.visualRadius = 15.0f;
    }

    void setupDisruptorDef() {
        disruptorDef.name = "disruptor";
        disruptorDef.id = "disruptor";
        disruptorDef.collisionRadius = 0.5f;
        disruptorDef.proximityRadius = 8.0f;
        disruptorDef.properties.weapon = 6;  // Disruptor (area type)
        disruptorDef.properties.hasTurret = false;
        disruptorDef.properties.omnidirectional = false;
        disruptorDef.properties.visualRadius = 30.0f;
    }

    // Create a unit body at a position. Returns the UnitInstance (caller owns it).
    void createUnitBody(UnitInstance& unit, Vector2 pos, float radius, int32_t groupIndex) {
        unit.collisionGroupId = groupIndex;
        unit.bodyUserData.tag = BodyTag::Unit;
        unit.bodyUserData.owner = &unit;

        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_dynamicBody;
        bodyDef.position = {pos.x, pos.y};
        bodyDef.linearDamping = 4.0f;
        bodyDef.angularDamping = 8.0f;
        bodyDef.userData = &unit.bodyUserData;
        unit.bodyId = b2CreateBody(worldId, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.filter.categoryBits = CATEGORY_UNIT;
        shapeDef.filter.maskBits = MASK_UNIT;
        shapeDef.filter.groupIndex = groupIndex;

        b2Circle circle = {{0, 0}, radius};
        b2CreateCircleShape(unit.bodyId, &shapeDef, &circle);

        // Attach the motor joint so AI movement (which drives the joint target)
        // actually moves the body — same control path as the game.
        unit_attach_motor_joint(&unit, worldId, originBody);
    }

    // Build section instances for a unit (needed for turret tests)
    void buildSections(UnitInstance& unit) {
        unit.rootSection = std::make_unique<SectionInstance>();
        unit.rootSection->definition = &unit.definition->rootSection;
        unit.allSections.push_back(unit.rootSection.get());

        for (const auto& childDef : unit.definition->rootSection.children) {
            auto child = std::make_unique<SectionInstance>();
            child->definition = &childDef;
            child->parent = unit.rootSection.get();
            unit.allSections.push_back(child.get());
            unit.rootSection->children.push_back(std::move(child));
        }
    }

    // Helper: init a single enemy at a waypoint
    void initSingleEnemy(UnitInstance& unit, const UnitDefinition& def,
                         int waypointIdx, int32_t groupIndex) {
        unit.definition = &def;
        unit.combatState = {100.0f, 100.0f, 0.0f, true};
        Vector2 pos = {waypointPositions[waypointIdx].x,
                       waypointPositions[waypointIdx].z};
        createUnitBody(unit, pos, def.collisionRadius, groupIndex);
        buildSections(unit);

        SpawnEntry spawn;
        spawn.waypointIndex = waypointIdx;
        std::vector<SpawnEntry> spawns = {spawn};
        std::vector<UnitInstance*> enemies = {&unit};
        aiManager.init(spawns, waypointPositions, adjacency, enemies);
    }
};

//------------------------------------------------------------------------------
// Patrol tests
//------------------------------------------------------------------------------

TEST_F(AITestFixture, PatrolSelectsLinkedWaypoint) {
    // Place armed droid at waypoint 1 (linked to 0, 2, 3)
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);

    // Put player far away so no detection
    Vector2 playerPos = {100.0f, 100.0f};

    // Update — should select a target waypoint
    aiManager.update(0.016f, playerPos, worldId, nullptr);

    auto& ai = aiManager.components()[0];
    EXPECT_EQ(ai.state, AIState::Patrol);
    ASSERT_GE(ai.targetWaypoint, 0);

    // Target must be a linked neighbour of waypoint 1
    const auto& neighbours = adjacency[1];
    bool isNeighbour = std::find(neighbours.begin(), neighbours.end(),
                                  ai.targetWaypoint) != neighbours.end();
    EXPECT_TRUE(isNeighbour) << "Target " << ai.targetWaypoint
                              << " is not a neighbour of waypoint 1";
}

TEST_F(AITestFixture, PatrolBackAvoidanceBias) {
    // Place droid at waypoint 1, with previous=0
    // Neighbours are 0, 2, 3. Waypoint 0 should be chosen less often.
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.previousWaypoint = 0;

    Vector2 playerPos = {100.0f, 100.0f};

    int backCount = 0;
    const int N = 2000;

    for (int i = 0; i < N; i++) {
        ai.targetWaypoint = -1;
        aiManager.update(0.016f, playerPos, worldId, nullptr);
        if (ai.targetWaypoint == 0) backCount++;

        // Reset for next trial
        ai.currentWaypoint = 1;
        ai.dwellTimer = 0.0f;
    }

    // With 3 neighbours and back-avoidance (0.2 weight vs 1.0), expected ratio
    // for back-waypoint: 0.2 / (0.2 + 1.0 + 1.0) ≈ 0.091
    // Without bias it would be ~0.333
    float ratio = static_cast<float>(backCount) / N;
    EXPECT_LT(ratio, 0.20f) << "Back waypoint chosen " << ratio * 100
                              << "% — bias not working";
    EXPECT_GT(ratio, 0.01f) << "Back waypoint never chosen — may be blocked entirely";
}

TEST_F(AITestFixture, PatrolDwellAtWaypoint) {
    // Place droid at waypoint 0 (linked to 1 only — so it goes to 1)
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);

    Vector2 playerPos = {100.0f, 100.0f};

    auto& ai = aiManager.components()[0];

    // Force the droid to arrive at waypoint 1
    ai.targetWaypoint = 1;
    // Move droid body to waypoint 1's position
    b2Body_SetTransform(unit.bodyId,
                        {waypointPositions[1].x, waypointPositions[1].z},
                        b2MakeRot(0.0f));

    // Update — should arrive and set a dwell timer
    aiManager.update(0.016f, playerPos, worldId, nullptr);

    // Since previous (0) and current (1) and next could be colinear (0→1→2 are all y=0),
    // the dwell may be skipped for colinear paths. So let's test with waypoint 3 approach.
    // Reset: place at waypoint 3 (linked to 1, 4), with previous = 4
    ai.currentWaypoint = 3;
    ai.previousWaypoint = 4;
    ai.targetWaypoint = 1;  // Moving toward 1
    ai.dwellTimer = 0.0f;

    // Teleport to waypoint 1
    b2Body_SetTransform(unit.bodyId,
                        {waypointPositions[1].x, waypointPositions[1].z},
                        b2MakeRot(0.0f));

    // Arrival from 3→1, then next waypoint could be 0, 2, or 3
    // Direction 3→1 is (0, -5) normalized = (0, -1)
    // Direction 1→0 is (-5, 0) normalized = (-1, 0) — dot = 0 → not colinear → dwell
    // Direction 1→2 is (5, 0) normalized = (1, 0) — dot = 0 → not colinear → dwell
    // Direction 1→3 is (0, 5) normalized = (0, 1) — dot = -1 → not colinear → dwell
    // So all next waypoints from 3→1 arrival produce dwell
    aiManager.update(0.016f, playerPos, worldId, nullptr);

    EXPECT_GT(ai.dwellTimer, 0.0f) << "Droid should dwell at non-colinear waypoint";
}

//------------------------------------------------------------------------------
// Detection and hostility tests
//------------------------------------------------------------------------------

TEST_F(AITestFixture, ArmedDroidBecomesHostileOnDetection) {
    // Armed droid at waypoint 0, detection radius = 8
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);

    // Player within detection radius
    Vector2 playerPos = {3.0f, 0.0f};  // distance = 3 < 8

    aiManager.update(0.016f, playerPos, worldId, nullptr);

    auto& ai = aiManager.components()[0];
    EXPECT_EQ(ai.state, AIState::Chase);
    EXPECT_TRUE(ai.hostile);
}

TEST_F(AITestFixture, UnarmedDroidIgnoresPlayer) {
    // Unarmed droid at waypoint 0
    UnitInstance unit;
    initSingleEnemy(unit, unarmedDef, 0, -10);

    // Player very close
    Vector2 playerPos = {1.0f, 0.0f};

    aiManager.update(0.016f, playerPos, worldId, nullptr);

    auto& ai = aiManager.components()[0];
    EXPECT_EQ(ai.state, AIState::Patrol);
    EXPECT_FALSE(ai.hostile);
}

TEST_F(AITestFixture, DamageTriggerHostileArmed) {
    // Armed droid at waypoint 0, player far away (no detection)
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);

    auto& ai = aiManager.components()[0];
    EXPECT_EQ(ai.state, AIState::Patrol);

    // Simulate taking damage
    aiManager.onDamageTaken(&unit);

    EXPECT_EQ(ai.state, AIState::Chase);
    EXPECT_TRUE(ai.hostile);
}

TEST_F(AITestFixture, DamageTriggerFleeUnarmed) {
    // Unarmed droid at waypoint 0
    UnitInstance unit;
    initSingleEnemy(unit, unarmedDef, 0, -10);

    auto& ai = aiManager.components()[0];
    EXPECT_EQ(ai.state, AIState::Patrol);

    aiManager.onDamageTaken(&unit);

    EXPECT_EQ(ai.state, AIState::Flee);
    EXPECT_TRUE(ai.hostile);
}

//------------------------------------------------------------------------------
// Chase tests
//------------------------------------------------------------------------------

TEST_F(AITestFixture, ChaseSelectsWaypointCloserToPlayer) {
    // Droid at waypoint 1 (linked to 0, 2, 3). Player at (50, 0) — nearest wp is 2 at (10,0)
    // Player far enough to be beyond optimum range so standard droid won't halt
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;
    ai.visualRange = 100.0f; // Ensure player stays within visual range

    Vector2 playerPos = {50.0f, 0.0f};
    aiManager.update(0.016f, playerPos, worldId, nullptr);

    EXPECT_EQ(ai.targetWaypoint, 2) << "Should chase toward waypoint 2 (closest to player)";
}

TEST_F(AITestFixture, StandardDroidHaltsToFire) {
    // Standard armed droid (no turret, not omni) at waypoint 1
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    // Player within optimum range (10.0) of droid at (5, 0)
    // Place player at (8, 0) — distance = 3 < optimumRange 10
    Vector2 playerPos = {8.0f, 0.0f};

    // Record velocity before
    b2Vec2 velBefore = b2Body_GetLinearVelocity(unit.bodyId);

    // Run several ticks
    for (int i = 0; i < 10; i++) {
        aiManager.update(0.016f, playerPos, worldId, nullptr);
        step(0.016f);
    }

    // Standard droid should halt (no waypoint movement force applied)
    // The body should have near-zero velocity due to damping and no force
    b2Vec2 vel = b2Body_GetLinearVelocity(unit.bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    EXPECT_LT(speed, 1.0f) << "Standard droid should be nearly halted within optimum range";
}

TEST_F(AITestFixture, TurretDroidTracksHeadWhileApproaching) {
    // Turret droid at waypoint 1 (5,0), player beyond optimum range (10) at (20,0): the
    // droid is still approaching, so it keeps moving while the turret tracks the player.
    UnitInstance unit;
    initSingleEnemy(unit, turretDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    Vector2 playerPos = {20.0f, 0.0f};  // dist 15 > optimumRange 10 → approaching, not halting

    for (int i = 0; i < 30; i++) {
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }

    // Still closing the distance, so the body is moving.
    b2Vec2 vel = b2Body_GetLinearVelocity(unit.bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    EXPECT_GT(speed, 0.1f) << "Turret droid should keep moving while still approaching";

    // Head (turret) section should be tracking the player.
    ASSERT_NE(ai.turretSection, nullptr);
    b2Vec2 up = b2Body_GetPosition(unit.bodyId);
    float expected = facing_angle_to(playerPos.x - up.x, playerPos.y - up.y);
    EXPECT_NEAR(ai.turretSection->facingAngle, expected, 0.35f)
        << "Turret head should track the player";
}

TEST_F(AITestFixture, TurretDroidHaltsToFire) {
    // Turret droid WITHOUT fireWhileMoving stops to fire once inside optimum range — the
    // general case (only fireWhileMoving/omnidirectional units keep moving).
    UnitInstance unit;
    initSingleEnemy(unit, turretDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    Vector2 playerPos = {8.0f, 0.0f};  // dist 3 < optimumRange 10

    for (int i = 0; i < 10; i++) {
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }

    b2Vec2 vel = b2Body_GetLinearVelocity(unit.bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    EXPECT_LT(speed, 1.0f) << "Turret droid should halt within optimum range to fire";

    // The turret still tracks the player even while the body is stopped.
    ASSERT_NE(ai.turretSection, nullptr);
    b2Vec2 up = b2Body_GetPosition(unit.bodyId);
    float expected = facing_angle_to(playerPos.x - up.x, playerPos.y - up.y);
    EXPECT_NEAR(ai.turretSection->facingAngle, expected, 0.35f)
        << "Turret head should track the player while halted";
}

TEST_F(AITestFixture, OmnidirectionalFiresWhileMoving) {
    // Omni droid at waypoint 1, player at (5, 5) (waypoint 3 direction)
    UnitInstance unit;
    initSingleEnemy(unit, omniDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    // Player at (5, 5) — near waypoint 3. Chase target should be waypoint 3.
    Vector2 playerPos = {5.0f, 5.0f};

    for (int i = 0; i < 30; i++) {
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }

    // Should be moving
    b2Vec2 vel = b2Body_GetLinearVelocity(unit.bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    EXPECT_GT(speed, 0.1f) << "Omnidirectional droid should keep moving while chasing";
}

TEST_F(AITestFixture, FireWhileMovingUnitDoesNotHalt) {
    // A fireWhileMoving unit (type 20) does NOT halt inside optimum range the way a
    // standard droid does — it keeps maneuvering while shooting.
    UnitInstance unit;
    initSingleEnemy(unit, fwmDef, 1, -10);  // at (5, 0)

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    Vector2 playerPos = {8.0f, 0.0f};  // dist 3 < optimumRange 10 (would halt a standard droid)

    for (int i = 0; i < 30; i++) {
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }

    b2Vec2 vel = b2Body_GetLinearVelocity(unit.bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    EXPECT_GT(speed, 0.1f) << "fireWhileMoving droid should keep moving inside optimum range";
}

TEST_F(AITestFixture, OmnidirectionalHoldsZeroFacing) {
    // Redefined omnidirectional: the body never orients, it is pinned to angle 0 even
    // while chasing a player that is off-axis.
    UnitInstance unit;
    initSingleEnemy(unit, omniDef, 1, -10);  // at (5, 0)

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    Vector2 playerPos = {5.0f, 5.0f};  // off the zero-facing axis

    for (int i = 0; i < 30; i++) {
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }

    float angle = b2Rot_GetAngle(b2Body_GetRotation(unit.bodyId));
    EXPECT_NEAR(angle, 0.0f, 0.15f) << "Omnidirectional body facing should stay at 0";
}

TEST_F(AITestFixture, HeadUnitIgnoresTargetBehind) {
    // A head unit only detects a player inside its forward vision cone. The head starts
    // facing angle 0 (forward = +Y); a player directly behind (−Y) must go unnoticed.
    UnitInstance unit;
    initSingleEnemy(unit, headDef, 1, -10);  // at (5, 0)

    auto& ai = aiManager.components()[0];
    ASSERT_TRUE(ai.hasHead);

    Vector2 behind = {5.0f, -5.0f};  // within detectionRadius 8 but behind the head
    aiManager.update(0.016f, behind, worldId, nullptr);

    EXPECT_EQ(ai.state, AIState::Patrol) << "Head unit should not see a target behind it";
}

TEST_F(AITestFixture, HeadUnitDetectsTargetInFront) {
    UnitInstance unit;
    initSingleEnemy(unit, headDef, 1, -10);  // at (5, 0)

    auto& ai = aiManager.components()[0];
    ASSERT_TRUE(ai.hasHead);

    Vector2 front = {5.0f, 5.0f};  // within detectionRadius 8 and in front (+Y)
    aiManager.update(0.016f, front, worldId, nullptr);

    EXPECT_EQ(ai.state, AIState::Chase) << "Head unit should detect a target in its forward cone";
}

//------------------------------------------------------------------------------
// Disruptor (area weapon) test
//------------------------------------------------------------------------------

TEST_F(AITestFixture, AreaWeaponDeferredThisPhase) {
    // Phase 1 fires projectile weapons only (see docs/weapons.md). A disruptor is an
    // Area weapon (speed 0) — tryFireAtPlayer gates it off so it doesn't spawn a
    // stationary "bad projectile" now that the weapon table loads. When area weapons
    // land in a later phase, this should assert the disruptor fires regardless of
    // facing (the canFire Area branch already ignores facing).
    UnitInstance unit;
    initSingleEnemy(unit, disruptorDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    // Face the droid away from the player and place the player in range (dist 3 < 25).
    b2Body_SetTransform(unit.bodyId, {5.0f, 0.0f}, b2MakeRot(PI));
    Vector2 playerPos = {8.0f, 0.0f};

    aiManager.update(0.016f, playerPos, worldId, &projectiles);

    EXPECT_EQ(projectiles.activeCount(), 0)
        << "Area weapons do not fire in phase 1 (projectile weapons only)";
}

//------------------------------------------------------------------------------
// Line-of-sight uses the projectile width, not the droid body
//------------------------------------------------------------------------------

// Build a static wall box centred at `c` with half-extents (hx, hy).
static void makeWall(b2WorldId worldId, Vector2 c, float hx, float hy) {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_staticBody;
    bd.position = {c.x, c.y};
    b2BodyId w = b2CreateBody(worldId, &bd);
    b2Polygon box = b2MakeBox(hx, hy);
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.filter.categoryBits = CATEGORY_STATIC;
    sd.filter.maskBits = 0xFFFF;
    b2CreatePolygonShape(w, &sd, &box);
}

TEST_F(AITestFixture, FiresThroughGapNarrowerThanBody) {
    // Droid at (0,0) facing +Y, player at (0,4). Two walls at y=2 leave a 0.4-wide gap at
    // x∈[-0.2,0.2] — wider than the bolt (radius 0.1) but far narrower than the droid body
    // (radius 0.5). The fire LOS must use the projectile width, so the shot goes through.
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);  // waypoint 0 = (0,0), identity rotation (faces +Y)
    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    makeWall(worldId, {-1.2f, 2.0f}, 1.0f, 0.5f);
    makeWall(worldId, { 1.2f, 2.0f}, 1.0f, 0.5f);

    Vector2 playerPos = {0.0f, 4.0f};
    aiManager.update(0.016f, playerPos, worldId, &projectiles);

    EXPECT_GT(projectiles.activeCount(), 0)
        << "A thin bolt should fire through a gap the droid body can't fit through";
}

TEST_F(AITestFixture, HoldsFireWhenWallFullyBlocksSightline) {
    // Same setup but a solid wall spans the whole sightline — no gap, so it holds fire.
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);
    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    makeWall(worldId, {0.0f, 2.0f}, 3.0f, 0.5f);  // solid across the line of fire

    Vector2 playerPos = {0.0f, 4.0f};
    aiManager.update(0.016f, playerPos, worldId, &projectiles);

    EXPECT_EQ(projectiles.activeCount(), 0)
        << "A wall across the sightline should hold fire";
}

// Build a door box with the same filter the DoorManager uses: category CATEGORY_DOOR, and
// maskBits MASK_DOOR_SOLID when closed / 0 when open.
static void makeDoor(b2WorldId worldId, Vector2 c, float hx, float hy, bool closed) {
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_staticBody;
    bd.position = {c.x, c.y};
    b2BodyId w = b2CreateBody(worldId, &bd);
    b2Polygon box = b2MakeBox(hx, hy);
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.filter.categoryBits = CATEGORY_DOOR;
    sd.filter.maskBits = closed ? MASK_DOOR_SOLID : 0;
    b2CreatePolygonShape(w, &sd, &box);
}

TEST_F(AITestFixture, HoldsFireWhenClosedDoorBlocksSightline) {
    // A CLOSED door across the sightline blocks fire (firing LOS includes doors).
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);
    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    makeDoor(worldId, {0.0f, 2.0f}, 3.0f, 0.5f, /*closed=*/true);

    Vector2 playerPos = {0.0f, 4.0f};
    aiManager.update(0.016f, playerPos, worldId, &projectiles);

    EXPECT_EQ(projectiles.activeCount(), 0)
        << "A closed door should block the firing line of sight";
}

TEST_F(AITestFixture, FiresThroughOpenDoor) {
    // An OPEN door clears its filter, so it does not block fire.
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);
    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    makeDoor(worldId, {0.0f, 2.0f}, 3.0f, 0.5f, /*closed=*/false);

    Vector2 playerPos = {0.0f, 4.0f};
    aiManager.update(0.016f, playerPos, worldId, &projectiles);

    EXPECT_GT(projectiles.activeCount(), 0)
        << "An open door should not block fire";
}

//------------------------------------------------------------------------------
// Disengage test
//------------------------------------------------------------------------------

TEST_F(AITestFixture, DisengageOnVisualRangeLost) {
    // Armed droid at waypoint 1, visual range = 15
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    // Player far beyond visual range: out of sight, so it gives up after the lose-sight
    // timeout (not immediately).
    Vector2 playerPos = {100.0f, 100.0f};

    int frames = static_cast<int>(AI_LOSE_SIGHT_TIME / 0.016f) + 5;  // just past 2 s
    for (int i = 0; i < frames; i++) {
        aiManager.update(0.016f, playerPos, worldId, nullptr);
    }

    EXPECT_EQ(ai.state, AIState::Patrol);
    EXPECT_FALSE(ai.hostile);
}

TEST_F(AITestFixture, StaysHostileWhileSightMaintained) {
    // Player in clear view the whole time: the unit never gives up.
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);  // at (0,0)
    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    Vector2 playerPos = {0.0f, 4.0f};  // in range, clear line of sight
    for (int i = 0; i < 200; i++) {    // 3.2 s
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }

    EXPECT_EQ(ai.state, AIState::Chase);
    EXPECT_TRUE(ai.hostile);
}

TEST_F(AITestFixture, LosesHostilityAfterLostSightTimeout) {
    // Player is in range but a wall breaks the line of sight. The unit holds the chase
    // briefly, then reverts to Patrol once AI_LOSE_SIGHT_TIME elapses.
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 0, -10);  // at (0,0)
    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    makeWall(worldId, {0.0f, 2.0f}, 3.0f, 0.5f);  // blocks LOS to the player at (0,4)
    Vector2 playerPos = {0.0f, 4.0f};

    // Just under the timeout — still chasing.
    for (int i = 0; i < 100; i++) {  // 1.6 s
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }
    EXPECT_EQ(ai.state, AIState::Chase) << "Should still pursue within the grace window";

    // Past the timeout — gives up and does not re-detect through the wall.
    for (int i = 0; i < 40; i++) {  // +0.64 s → 2.24 s total
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }
    EXPECT_EQ(ai.state, AIState::Patrol);
    EXPECT_FALSE(ai.hostile);
}

//------------------------------------------------------------------------------
// Flee test
//------------------------------------------------------------------------------

TEST_F(AITestFixture, FleeSelectsWaypointAwayFromPlayer) {
    // Unarmed droid at waypoint 3 (linked to 1, 4). Player at (5, 0) = waypoint 1.
    // Waypoint 1 is at (5, 0), waypoint 4 is at (5, 10).
    // Player at (5, 0) → waypoint 4 is farther → should flee to 4.
    UnitInstance unit;
    initSingleEnemy(unit, unarmedDef, 3, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Flee;
    ai.hostile = true;

    // Teleport to waypoint 3
    b2Body_SetTransform(unit.bodyId,
                        {waypointPositions[3].x, waypointPositions[3].z},
                        b2MakeRot(0.0f));

    Vector2 playerPos = {5.0f, 0.0f};  // At waypoint 1

    aiManager.update(0.016f, playerPos, worldId, nullptr);

    EXPECT_EQ(ai.targetWaypoint, 4)
        << "Fleeing droid should pick waypoint farthest from player (4, not 1)";
}

//------------------------------------------------------------------------------
// Collision response tests
//------------------------------------------------------------------------------

TEST_F(AITestFixture, CollisionReversesCourseToCurrentWaypoint) {
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);  // currentWaypoint = 1
    auto& ai = aiManager.components()[0];
    ai.hostile = false;
    ai.currentWaypoint = 1;   // came from waypoint 1
    ai.previousWaypoint = 0;
    ai.targetWaypoint = 2;    // heading toward waypoint 2 (edge 1->2)

    UnitInstance other;  // dummy collision partner (identity only)
    aiManager.onCollision(&unit, &other);

    // Reverse along the edge we're on: head back to waypoint 1 (a valid path), not to the
    // prior waypoint 0 (off the current edge) or a fresh reselection.
    EXPECT_EQ(ai.targetWaypoint, 1) << "Collision should reverse course to the current waypoint";
    EXPECT_EQ(ai.previousWaypoint, 2) << "Blocked target is biased against on the next pick";
    EXPECT_GT(ai.collideCooldown, 0.0f) << "Redirect decision is debounced";
}

TEST_F(AITestFixture, CollisionCooldownDebouncesRedirect) {
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);
    auto& ai = aiManager.components()[0];
    ai.hostile = false;
    ai.currentWaypoint = 1;
    ai.previousWaypoint = 0;
    ai.targetWaypoint = 2;

    UnitInstance other;
    aiManager.onCollision(&unit, &other);  // reverse -> target 1, cooldown armed
    ai.targetWaypoint = 2;                 // pretend AI re-picked 2
    aiManager.onCollision(&unit, &other);  // within cooldown -> ignored

    EXPECT_EQ(ai.targetWaypoint, 2) << "A second collision within the cooldown is ignored";
}

TEST_F(AITestFixture, CollisionWithoutCurrentWaypointForcesReselect) {
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);
    auto& ai = aiManager.components()[0];
    ai.hostile = false;
    ai.currentWaypoint = -1;   // knocked off the graph — no edge to reverse along
    ai.targetWaypoint = 2;

    UnitInstance other;
    aiManager.onCollision(&unit, &other);

    EXPECT_EQ(ai.targetWaypoint, -1) << "With no current waypoint the target is dropped for reselection";
}

TEST_F(AITestFixture, StuckUnitAbandonsBlockedRoute) {
    UnitInstance unit;
    initSingleEnemy(unit, unarmedDef, 1, -10);  // unarmed: won't switch to chase
    auto& ai = aiManager.components()[0];
    ai.state = AIState::Patrol;
    ai.hostile = false;
    ai.currentWaypoint = 1;
    ai.previousWaypoint = 0;
    ai.targetWaypoint = 2;  // heading 1 -> 2

    // Sit at waypoint 1 and never step physics, so speed stays ~0 (blocked).
    b2Body_SetTransform(unit.bodyId,
                        {waypointPositions[1].x, waypointPositions[1].z}, b2MakeRot(0.0f));
    Vector2 playerPos = {100.0f, 100.0f};

    int frames = static_cast<int>(AI_STUCK_TIME / 0.016f) + 5;
    for (int i = 0; i < frames; i++) {
        aiManager.update(0.016f, playerPos, worldId, nullptr);
    }

    EXPECT_EQ(ai.previousWaypoint, 2) << "The blocked route becomes previousWaypoint";
    EXPECT_NE(ai.targetWaypoint, 2) << "A stuck unit reselects a different route";
}

TEST_F(AITestFixture, OffCourseUnitReacquiresNearestReachableWaypoint) {
    UnitInstance unit;
    initSingleEnemy(unit, unarmedDef, 1, -10);
    auto& ai = aiManager.components()[0];
    ai.hostile = false;
    ai.currentWaypoint = 1;
    ai.previousWaypoint = 0;
    ai.targetWaypoint = 2;

    // Wall across the straight path toward wp2 (10,0). Static shape proxies are
    // queryable immediately at creation, so no physics step is needed.
    b2BodyDef wallDef = b2DefaultBodyDef();
    wallDef.type = b2_staticBody;
    wallDef.position = {7.5f, 0.0f};
    b2BodyId wall = b2CreateBody(worldId, &wallDef);
    b2Polygon box = b2MakeBox(0.5f, 3.0f);
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.filter.categoryBits = CATEGORY_STATIC;
    sd.filter.maskBits = 0xFFFF;
    b2CreatePolygonShape(wall, &sd, &box);

    // Park the unit OFF the graph at (5,2): near wp1 (5,0) but not on it, with the
    // wall between it and wp2. No stepping -> speed stays 0 (stuck). The nearest
    // reachable waypoint is wp1, which it must travel to (so it won't immediately
    // "arrive" and mask the re-acquire).
    b2Body_SetTransform(unit.bodyId, (b2Vec2){5.0f, 2.0f}, b2MakeRot(0.0f));
    b2Body_SetLinearVelocity(unit.bodyId, (b2Vec2){0, 0});

    Vector2 playerPos = {100.0f, 100.0f};
    int frames = static_cast<int>(AI_STUCK_TIME / 0.016f) + 5;
    for (int i = 0; i < frames; i++) {
        aiManager.update(0.016f, playerPos, worldId, nullptr);
    }

    // The wall blocks the path to wp2, so the unit re-acquires the nearest reachable
    // waypoint (wp1) rather than taking the unit-deadlock reselect path.
    EXPECT_EQ(ai.currentWaypoint, -1) << "Re-acquire resets currentWaypoint";
    EXPECT_EQ(ai.targetWaypoint, 1) << "Re-acquire heads to the nearest reachable waypoint";
}

TEST_F(AITestFixture, HostileUnitIgnoresCollision) {
    UnitInstance unit;
    initSingleEnemy(unit, armedDef, 1, -10);
    auto& ai = aiManager.components()[0];
    ai.hostile = true;        // pursuing — must not back off
    ai.previousWaypoint = 0;
    ai.targetWaypoint = 2;

    UnitInstance other;
    aiManager.onCollision(&unit, &other);

    EXPECT_EQ(ai.targetWaypoint, 2) << "Hostile unit should not redirect";
    EXPECT_FLOAT_EQ(ai.collideCooldown, 0.0f);
}
