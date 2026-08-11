#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "level/console_manager.h"
#include "util/index_wrap.h"
#include "pages/page.h"
#include "pages/page_manager.h"

//------------------------------------------------------------------------------
// ConsoleManager: the "use" zone is a box the size of the console footprint placed IN FRONT of the
// console (the console body blocks its own centre). Its centre is consoleUseZoneCenter().
//------------------------------------------------------------------------------

TEST(ConsoleManagerTest, InRangeInFrontNotAtCentre) {
    ConsoleManager cm;
    ConsoleSpec s;
    s.physicsCenter = {5.0f, 5.0f};
    s.facingRad = 0.0f;
    cm.init({s});

    const Vector2 front = consoleUseZoneCenter(s);

    cm.update(front);                            // standing in front -> usable
    EXPECT_TRUE(cm.playerInRange());

    // In front but offset along the wide face (within CONSOLE_HALF_Z) -> still usable.
    cm.update({front.x + 0.7f, front.y});
    EXPECT_TRUE(cm.playerInRange());

    cm.update({5.0f, 5.0f});                      // the console centre itself is blocked -> not usable
    EXPECT_FALSE(cm.playerInRange());

    // Opposite side (mirror of the front zone about the console) -> not usable.
    cm.update({2.0f * s.physicsCenter.x - front.x, 2.0f * s.physicsCenter.y - front.y});
    EXPECT_FALSE(cm.playerInRange());

    cm.update({50.0f, 50.0f});                    // far away
    EXPECT_FALSE(cm.playerInRange());
}

TEST(ConsoleManagerTest, NoConsolesNeverInRange) {
    ConsoleManager cm;
    cm.init({});
    cm.update({0.0f, 0.0f});
    EXPECT_FALSE(cm.playerInRange());
}

TEST(ConsoleManagerTest, InFrontOfOneOfMultipleConsoles) {
    ConsoleManager cm;
    ConsoleSpec a, b;
    a.physicsCenter = {0.0f, 0.0f};
    b.physicsCenter = {10.0f, 0.0f};
    a.facingRad = b.facingRad = 0.0f;
    cm.init({a, b});

    cm.update(consoleUseZoneCenter(b));   // in front of the second console
    EXPECT_TRUE(cm.playerInRange());

    cm.update({5.0f, 0.0f});              // midway, in front of neither
    EXPECT_FALSE(cm.playerInRange());
}

//------------------------------------------------------------------------------
// Library navigation wrap.
//------------------------------------------------------------------------------

TEST(IndexWrapTest, WrapsBothEnds) {
    EXPECT_EQ(wrapIndex(0, +1, 3), 1);
    EXPECT_EQ(wrapIndex(2, +1, 3), 0);   // wrap forward past the end
    EXPECT_EQ(wrapIndex(0, -1, 3), 2);   // wrap backward past the start
    EXPECT_EQ(wrapIndex(1, -1, 3), 0);
}

TEST(IndexWrapTest, DegenerateCounts) {
    EXPECT_EQ(wrapIndex(0, +1, 0), 0);   // empty list
    EXPECT_EQ(wrapIndex(0, +1, 1), 0);   // single entry stays put
    EXPECT_EQ(wrapIndex(0, -1, 1), 0);
}

//------------------------------------------------------------------------------
// PageManager: push/pop drive activate/deactivate and route to the top page.
// Push/pop are deferred and applied at the start of update()/render().
//------------------------------------------------------------------------------

namespace {
struct Counts {
    int activate = 0, deactivate = 0, handleInput = 0, update = 0, render = 0;
};

class FakePage : public Page {
public:
    FakePage(Counts* c) : Page(nullptr, nullptr), c_(c) {}
    void activate() override { c_->activate++; }
    void deactivate() override { c_->deactivate++; }
    void handleInput() override { c_->handleInput++; }
    void update(float) override { c_->update++; }
    void render() override { c_->render++; }
private:
    Counts* c_;
};
}  // namespace

TEST(PageManagerTest, PushActivatesAndRoutes) {
    PageManager pm;
    Counts c;
    pm.push(std::make_unique<FakePage>(&c));

    // Deferred: nothing happens until the next update()/render().
    EXPECT_EQ(c.activate, 0);

    pm.update(0.016f);
    EXPECT_EQ(c.activate, 1);
    EXPECT_EQ(c.handleInput, 1);
    EXPECT_EQ(c.update, 1);

    pm.render();
    EXPECT_EQ(c.render, 1);
}

TEST(PageManagerTest, PushDeactivatesPreviousTopAndPopRestores) {
    PageManager pm;
    Counts a, b;
    pm.push(std::make_unique<FakePage>(&a));
    pm.update(0.016f);          // a activated, is top
    EXPECT_EQ(a.activate, 1);

    pm.push(std::make_unique<FakePage>(&b));
    pm.update(0.016f);          // a deactivated, b activated + routed
    EXPECT_EQ(a.deactivate, 1);
    EXPECT_EQ(b.activate, 1);
    EXPECT_EQ(b.update, 1);
    EXPECT_EQ(a.update, 1);     // a did NOT update again (no longer top)

    pm.pop();
    pm.update(0.016f);          // b deactivated + destroyed, a reactivated + routed
    EXPECT_EQ(b.deactivate, 1);
    EXPECT_EQ(a.activate, 2);
    EXPECT_EQ(a.update, 2);
}

TEST(PageManagerTest, EmptyStackIsSafe) {
    PageManager pm;
    pm.update(0.016f);   // no top page — must not crash
    pm.render();
    SUCCEED();
}
