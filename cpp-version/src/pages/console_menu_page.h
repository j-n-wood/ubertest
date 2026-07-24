#ifndef CONSOLE_MENU_PAGE_H
#define CONSOLE_MENU_PAGE_H

#include "pages/page.h"

// Top-level console screen: raygui menu that pushes sub-pages (Droid Library,
// Status) or pops back to gameplay. Pushed by GamePage when SPACE is pressed on a
// console tile; the game simulation is frozen while any console page is on top.
class ConsoleMenuPage : public Page {
public:
    using Page::Page;
    void handleInput() override;
    void render() override;
};

#endif // CONSOLE_MENU_PAGE_H
