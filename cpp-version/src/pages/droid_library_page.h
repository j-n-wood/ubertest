#ifndef DROID_LIBRARY_PAGE_H
#define DROID_LIBRARY_PAGE_H

#include "pages/page.h"
#include "box2d/box2d.h"
#include "units/unit_manager.h"
#include "raylib.h"
#include <string>
#include <vector>

//------------------------------------------------------------------------------
// Console sub-screen: browse droid types. Shows the selected droid's model spinning
// on its vertical axis (own private Box2D world + UnitManager, like the unit_test
// tool) alongside its stats + description. UP/DOWN cycle types; ESC returns.
// (Debug property editing + save is added in Stage 3.)
//------------------------------------------------------------------------------

class DroidLibraryPage : public Page {
public:
    using Page::Page;
    ~DroidLibraryPage() override;

    void activate() override;
    void deactivate() override;
    void handleInput() override;
    void update(float dt) override;
    void render() override;

private:
    void rebuildDisplay();   // (re)create the display droid for the current index
    void teardown();         // destroy display world/unit (idempotent)

    std::vector<std::string> ids_;   // droid definition ids, sorted by class number
    int index_ = 0;
    float spin_ = 0.0f;

    b2WorldId world_ = b2_nullWorldId;   // private display world
    UnitManager units_;                  // private manager for the display droid
    UnitInstance* display_ = nullptr;
    Camera3D camera_{};

    // Debug editing (active when the game's debug flag is on): transient save feedback.
    std::string saveMsg_;
    float saveMsgTimer_ = 0.0f;
};

#endif // DROID_LIBRARY_PAGE_H
