#ifndef STATUS_PAGE_H
#define STATUS_PAGE_H

#include "pages/page.h"

// Console sub-screen: current deck name + live droid count. (Alert status is a
// later addition.) ESC/back returns to the console menu.
class StatusPage : public Page {
public:
    using Page::Page;
    void handleInput() override;
    void render() override;
};

#endif // STATUS_PAGE_H
