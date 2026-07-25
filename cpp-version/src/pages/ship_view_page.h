#ifndef SHIP_VIEW_PAGE_H
#define SHIP_VIEW_PAGE_H

#include "pages/page.h"
#include "level/lift_manager.h"
#include "raylib.h"

//------------------------------------------------------------------------------
// Ship-view page: the side-on ship diagram shown when the player uses a lift.
// Draws the ship image (from ShipMap), highlights the accessed elevator shaft and
// the current/selected deck, and lets the player move UP/DOWN through the stops of
// that elevator. ENTER travels to the selected stop (switching level + placing the
// player on its lift tile); ESC cancels. See docs/lifts.md.
//------------------------------------------------------------------------------

class ShipViewPage : public Page {
public:
    using Page::Page;

    void activate() override;
    void deactivate() override;
    void handleInput() override;
    void render() override;

private:
    Texture2D shipTex_{};                 // dim base (ship_off)
    Texture2D litTex_{};                  // lit overlay (ship_on), blitted per deck/shaft
    bool texLoaded_ = false;
    bool litLoaded_ = false;
    const LiftStop* origin_ = nullptr;    // the lift the player used (elevator + start deck)
    const LiftStop* selected_ = nullptr;  // currently highlighted destination stop
};

#endif // SHIP_VIEW_PAGE_H
