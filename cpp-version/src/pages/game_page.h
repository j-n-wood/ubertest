#ifndef GAME_PAGE_H
#define GAME_PAGE_H

#include "pages/page.h"

//------------------------------------------------------------------------------
// GamePage — the gameplay view-state. Delegates to game_update_gameplay /
// game_render_gameplay, and opens the console (pushes ConsoleMenuPage) when the
// player is on a console tile and presses SPACE.
//------------------------------------------------------------------------------

class GamePage : public Page {
public:
    using Page::Page;
    void update(float dt) override;
    void render() override;
};

#endif // GAME_PAGE_H
