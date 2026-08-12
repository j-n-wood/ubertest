#ifndef SHIP_DATA_PAGE_H
#define SHIP_DATA_PAGE_H

#include "pages/page.h"

// Console sub-screen: ship metadata (name) plus progress toward clearing the whole ship —
// droids remaining shipwide and the current alert status. ESC/back returns to the console menu.
// See docs/pages.md.
class ShipDataPage : public Page {
public:
    using Page::Page;
    void handleInput() override;
    void render() override;
};

#endif // SHIP_DATA_PAGE_H
