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
    void applyCameraDistance();  // recompute camera_.position from camDist_ (fixed pitch/yaw)
    // Facing test: the target heading under the mouse cursor (cursor ray → ground plane).
    // Returns false if the ray doesn't meet the ground (keep the previous target).
    bool mouseTargetHeading(float* outAngle) const;

    // Orbit-camera zoom. Distance from the pedestal along the fixed 3/4 view direction;
    // '-'/'=' adjust it (held to zoom continuously), clamped to [MIN, MAX].
    static constexpr float kCamDistDefault = 3.0f;
    static constexpr float kCamDistMin = 1.2f;
    static constexpr float kCamDistMax = 9.0f;
    static constexpr float kCamZoomStep = 0.12f;  // per frame while held

    // Facing test (SPACE): stop the auto-spin and drive the body toward the mouse at its turn
    // rate while the turret/head sections slew at their (different) rate — demonstrates the
    // independent heading + rate. See docs/unit_animation.md.
    static constexpr float kFacingLineLen = 1.3f;    // facing-line length (world units)
    static constexpr float kFacingLineHeight = 0.4f; // draw height for lines/marker
    static constexpr float kFacingMarkerRadius = 1.5f;  // orbiting target-marker distance

    std::vector<std::string> ids_;   // droid definition ids, sorted by class number
    int index_ = 0;
    float spin_ = 0.0f;              // body world angle (auto-spin, or slewed in facing test)
    float camDist_ = kCamDistDefault;
    bool facingTest_ = false;        // SPACE toggles: aim turret/head + body at the mouse
    float facingTarget_ = 0.0f;      // last valid mouse target heading

    b2WorldId world_ = b2_nullWorldId;   // private display world
    UnitManager units_;                  // private manager for the display droid
    UnitInstance* display_ = nullptr;
    Camera3D camera_{};

    // Debug editing (active when the game's debug flag is on): transient save feedback.
    std::string saveMsg_;
    float saveMsgTimer_ = 0.0f;
};

#endif // DROID_LIBRARY_PAGE_H
