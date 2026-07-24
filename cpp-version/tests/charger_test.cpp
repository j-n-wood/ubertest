#include <gtest/gtest.h>
#include "box2d/box2d.h"
#include "level/charger_manager.h"
#include "physics/body_user_data.h"

class ChargerTest : public ::testing::Test {
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
        bd.enableSleep = false;
        b2BodyId b = b2CreateBody(world, &bd);
        b2Circle c = {{0, 0}, 0.2f};
        b2ShapeDef sd = b2DefaultShapeDef();
        sd.filter.categoryBits = CATEGORY_UNIT;
        sd.filter.maskBits = MASK_UNIT;
        b2CreateCircleShape(b, &sd, &c);
        return b;
    }
};

TEST_F(ChargerTest, StartsIdleAndReportsView) {
    ChargerManager cm;
    ChargerSpec s;
    s.physicsCenter = {0.0f, 0.0f};
    cm.init(world, {s});
    ASSERT_EQ(cm.views().size(), 1u);
    EXPECT_EQ(cm.views()[0].state, ChargerState::Idle);
}

TEST_F(ChargerTest, ChargingWhenUnitOnItThenIdleWhenGone) {
    ChargerManager cm;
    ChargerSpec s;
    s.physicsCenter = {0.0f, 0.0f};
    cm.init(world, {s});

    // Unit standing on the charger tile.
    b2BodyId unit = makeUnit({0.0f, 0.0f});
    b2World_Step(world, 0.016f, 4);
    cm.update(0.016f);
    EXPECT_EQ(cm.views()[0].state, ChargerState::Charging);

    // Unit leaves.
    b2Body_SetTransform(unit, (b2Vec2){50.0f, 50.0f}, b2MakeRot(0.0f));
    b2World_Step(world, 0.016f, 4);
    cm.update(0.016f);
    EXPECT_EQ(cm.views()[0].state, ChargerState::Idle);
}

TEST_F(ChargerTest, ProjectileDoesNotCharge) {
    ChargerManager cm;
    ChargerSpec s;
    s.physicsCenter = {0.0f, 0.0f};
    cm.init(world, {s});

    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = b2_dynamicBody;
    bd.position = {0.0f, 0.0f};
    b2BodyId proj = b2CreateBody(world, &bd);
    b2Circle c = {{0, 0}, 0.1f};
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.filter.categoryBits = CATEGORY_PROJECTILE;
    sd.filter.maskBits = MASK_PROJECTILE;
    b2CreateCircleShape(proj, &sd, &c);

    b2World_Step(world, 0.016f, 4);
    cm.update(0.016f);
    EXPECT_EQ(cm.views()[0].state, ChargerState::Idle)
        << "Only units should trigger charging";
}
