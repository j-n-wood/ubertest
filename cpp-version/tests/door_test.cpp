#include <gtest/gtest.h>
#include "box2d/box2d.h"
#include "level/door_manager.h"
#include "physics/body_user_data.h"

//------------------------------------------------------------------------------
// Pure state-machine tests (no physics)
//------------------------------------------------------------------------------

TEST(DoorStateMachine, OpensWhenUnitInRange) {
    DoorSim s;
    EXPECT_EQ(s.state, DoorState::Closed);
    door_advance(s, /*unitInRange=*/true, 0.016f);
    EXPECT_EQ(s.state, DoorState::Opening);
}

TEST(DoorStateMachine, OpeningReachesFullyOpen) {
    DoorSim s;
    s.state = DoorState::Opening;
    int frames = static_cast<int>(DOOR_OPEN_TIME / 0.016f) + 2;
    for (int i = 0; i < frames; i++) door_advance(s, true, 0.016f);
    EXPECT_EQ(s.state, DoorState::Open);
    EXPECT_FLOAT_EQ(s.openFraction, 1.0f);
}

TEST(DoorStateMachine, ClosesAfterDelayWhenClear) {
    DoorSim s;
    s.state = DoorState::Open;
    s.openFraction = 1.0f;
    int delayFrames = static_cast<int>(DOOR_CLOSE_DELAY / 0.016f) + 2;
    for (int i = 0; i < delayFrames; i++) door_advance(s, /*unitInRange=*/false, 0.016f);
    EXPECT_EQ(s.state, DoorState::Closing);
    int closeFrames = static_cast<int>(DOOR_OPEN_TIME / 0.016f) + 2;
    for (int i = 0; i < closeFrames; i++) door_advance(s, false, 0.016f);
    EXPECT_EQ(s.state, DoorState::Closed);
    EXPECT_FLOAT_EQ(s.openFraction, 0.0f);
}

TEST(DoorStateMachine, StaysOpenWhileUnitPresent) {
    DoorSim s;
    s.state = DoorState::Open;
    s.openFraction = 1.0f;
    for (int i = 0; i < 500; i++) door_advance(s, /*unitInRange=*/true, 0.016f);
    EXPECT_EQ(s.state, DoorState::Open);
}

TEST(DoorStateMachine, ClosingReopensIfUnitReturns) {
    DoorSim s;
    s.state = DoorState::Closing;
    s.openFraction = 0.5f;
    door_advance(s, /*unitInRange=*/true, 0.016f);
    EXPECT_EQ(s.state, DoorState::Opening);
}

//------------------------------------------------------------------------------
// Integration: Box2D sensor opens the door; it closes when the unit leaves
//------------------------------------------------------------------------------

class DoorIntegration : public ::testing::Test {
protected:
    b2WorldId world = b2_nullWorldId;

    void SetUp() override {
        b2WorldDef wd = b2DefaultWorldDef();
        wd.gravity = {0.0f, 0.0f};
        world = b2CreateWorld(&wd);
    }
    void TearDown() override { b2DestroyWorld(world); }

    b2BodyId makeUnit(Vector2 pos) {
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = {pos.x, pos.y};
        bd.enableSleep = false;  // real game units never sleep (so sensor end-events fire)
        b2BodyId b = b2CreateBody(world, &bd);
        b2Circle c = {{0, 0}, 0.2f};
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.filter.categoryBits = CATEGORY_UNIT;
        sd.filter.maskBits = MASK_UNIT;
        b2CreateCircleShape(b, &sd, &c);
        return b;
    }
};

TEST_F(DoorIntegration, SensorOpensDoorThenClosesWhenUnitLeaves) {
    DoorManager dm;
    DoorSpec spec;
    spec.orientation = DoorOrientation::Horizontal;
    spec.physicsCenter = {0.0f, 0.0f};
    spec.size = {1.0f, 0.5f};
    dm.init(world, {spec});
    ASSERT_EQ(dm.views().size(), 1u);
    EXPECT_EQ(dm.views()[0].state, DoorState::Closed);

    // Unit inside the sensor region (half-height 0.25+1.5=1.75) but clear of the
    // solid box (half-height 0.25), so the sensor triggers without a collision shove.
    b2BodyId unit = makeUnit({0.0f, 1.0f});
    int openFrames = static_cast<int>(DOOR_OPEN_TIME / 0.016f) + 10;
    for (int i = 0; i < openFrames; i++) {
        b2World_Step(world, 0.016f, 4);
        dm.update(0.016f);
    }
    EXPECT_EQ(dm.views()[0].state, DoorState::Open)
        << "Unit within the sensor should have opened the door";

    // Move the unit far away → out of the proximity region → door closes after delay.
    b2Body_SetTransform(unit, (b2Vec2){100.0f, 100.0f}, b2MakeRot(0.0f));
    int closeFrames = static_cast<int>((DOOR_CLOSE_DELAY + DOOR_OPEN_TIME) / 0.016f) + 20;
    for (int i = 0; i < closeFrames; i++) {
        b2World_Step(world, 0.016f, 4);
        dm.update(0.016f);
    }
    EXPECT_EQ(dm.views()[0].state, DoorState::Closed)
        << "Door should re-close after the unit leaves the sensor";
}

TEST_F(DoorIntegration, ProjectileDoesNotOpenDoor) {
    DoorManager dm;
    DoorSpec spec;
    spec.physicsCenter = {0.0f, 0.0f};
    spec.size = {1.0f, 0.5f};
    dm.init(world, {spec});

    // A projectile-category body sitting in the sensor region must NOT open the door.
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {0.0f, 1.0f};
    b2BodyId proj = b2CreateBody(world, &bd);
    b2Circle c = {{0, 0}, 0.1f};
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.filter.categoryBits = CATEGORY_PROJECTILE;
    sd.filter.maskBits = MASK_PROJECTILE;
    b2CreateCircleShape(proj, &sd, &c);

    for (int i = 0; i < 60; i++) {
        b2World_Step(world, 0.016f, 4);
        dm.update(0.016f);
    }
    EXPECT_EQ(dm.views()[0].state, DoorState::Closed)
        << "Projectiles must not trigger the door sensor";
}
