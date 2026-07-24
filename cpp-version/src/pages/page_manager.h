#ifndef PAGE_MANAGER_H
#define PAGE_MANAGER_H

#include "page.h"
#include <memory>
#include <vector>

//------------------------------------------------------------------------------
// PageManager — a stack of Pages. The top page is the active one; each frame
// update() then render() dispatch to it.
//
// push()/pop() are DEFERRED: they queue a transition that is applied at the start
// of the next update()/render(), so a page can safely request `pop()` from inside
// its own update() (it won't be destroyed mid-method).
//------------------------------------------------------------------------------

class PageManager {
public:
    void push(std::unique_ptr<Page> page);  // queue: make `page` the new active top
    void pop();                              // queue: remove the current top

    void update(float dt);                   // apply pending, then top: handleInput()+update()
    void render();                           // apply pending, then top: render()

    bool empty() const { return stack_.empty(); }
    Page* top() const { return stack_.empty() ? nullptr : stack_.back().get(); }

private:
    void applyPending();

    std::vector<std::unique_ptr<Page>> stack_;
    std::vector<std::unique_ptr<Page>> pendingPush_;
    int pendingPops_ = 0;
};

#endif // PAGE_MANAGER_H
