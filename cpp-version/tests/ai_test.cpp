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
        // Add a FollowFacing section for the head
        turretDef.rootSection.name = "body";
        turretDef.rootSection.rotationMode = SectionRotationMode::FollowUnit;
        SectionDefinition headSection;
        headSection.name = "head";
        headSection.rotationMode = SectionRotationMode::FollowFacing;
        turretDef.rootSection.children.push_back(std::move(headSection));
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

TEST_F(AITestFixture, TurretDroidFiresWhileMoving) {
    // Turret droid at waypoint 1, player at (12, 0)
    UnitInstance unit;
    initSingleEnemy(unit, turretDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    Vector2 playerPos = {12.0f, 0.0f};

    // Run several ticks to build velocity
    for (int i = 0; i < 30; i++) {
        aiManager.update(0.016f, playerPos, worldId, &projectiles);
        step(0.016f);
    }

    // Turret droid should be moving (velocity > 0)
    b2Vec2 vel = b2Body_GetLinearVelocity(unit.bodyId);
    float speed = sqrtf(vel.x * vel.x + vel.y * vel.y);
    EXPECT_GT(speed, 0.1f) << "Turret droid should keep moving while chasing";

    // Head section should be tracking player (facingAngle set)
    bool headTracking = false;
    for (auto* section : unit.allSections) {
        if (section->definition &&
            section->definition->rotationMode == SectionRotationMode::FollowFacing) {
            headTracking = true;
            // Head should track the player. Compare against the shared facing
            // convention rather than a hard-coded angle so the test stays valid
            // regardless of the internal angle convention.
            b2Vec2 up = b2Body_GetPosition(unit.bodyId);
            float expected = facing_angle_to(playerPos.x - up.x, playerPos.y - up.y);
            EXPECT_NEAR(section->facingAngle, expected, 0.35f)
                << "Turret head should face the player";
            break;
        }
    }
    EXPECT_TRUE(headTracking) << "Turret droid should have a FollowFacing head section";
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

//------------------------------------------------------------------------------
// Disruptor (area weapon) test
//------------------------------------------------------------------------------

TEST_F(AITestFixture, DisruptorIgnoresFacing) {
    // Disruptor droid at waypoint 1, facing away from player
    UnitInstance unit;
    initSingleEnemy(unit, disruptorDef, 1, -10);

    auto& ai = aiManager.components()[0];
    ai.state = AIState::Chase;
    ai.hostile = true;

    // Face the droid away from the player (body angle = PI, facing -X)
    b2Body_SetTransform(unit.bodyId, {5.0f, 0.0f}, b2MakeRot(PI));

    // Player at (8, 0) — within max range 25, distance 3
    Vector2 playerPos = {8.0f, 0.0f};

    // Disruptor should be able to fire regardless of facing
    // The canFire check in AIManager should return true for Area weapons
    // Update once to trigger firing attempt
    aiManager.update(0.016f, playerPos, worldId, &projectiles);

    // The disruptor fired — check projectile count
    // Note: Disruptor is area type with speed=0, but our spawn still creates a projectile
    EXPECT_GE(projectiles.activeCount(), 1)
        << "Disruptor should fire without facing requirement";
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

    // Player far beyond visual range
    Vector2 playerPos = {100.0f, 100.0f};

    aiManager.update(0.016f, playerPos, worldId, nullptr);

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
