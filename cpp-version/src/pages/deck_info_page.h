#ifndef DECK_INFO_PAGE_H
#define DECK_INFO_PAGE_H

#include "pages/page.h"

// Console sub-screen: the current deck's info — a zoomed-out top-down MAP of the level geometry
// (geometry only; no live units, particles or static objects), plus the deck name and live droid
// count. ESC/back returns to the console menu. See docs/pages.md.
class DeckInfoPage : public Page {
public:
    using Page::Page;
    void handleInput() override;
    void render() override;
};

#endif // DECK_INFO_PAGE_H
