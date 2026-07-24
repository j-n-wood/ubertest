#ifndef PAGE_H
#define PAGE_H

//------------------------------------------------------------------------------
// Page — one full-screen "view" (gameplay, console, title, ...). The PageManager
// keeps a stack of these and drives the top one each frame:
//   handleInput() -> update(dt) -> render()
// activate()/deactivate() fire as a page becomes / stops being the active top.
// A page holds non-owning pointers to the shared Game state and the PageManager
// (so it can push/pop other pages). See docs/pages.md.
//------------------------------------------------------------------------------

struct Game;
class PageManager;

class Page {
public:
    Page(Game* game, PageManager* pages) : game_(game), pages_(pages) {}
    virtual ~Page() = default;

    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    virtual void activate() {}          // became the active (top) page
    virtual void deactivate() {}        // no longer the active page
    virtual void handleInput() {}       // per-frame input (before update)
    virtual void update(float dt) {}    // per-frame logic
    virtual void render() {}            // per-frame draw

protected:
    Game* game_ = nullptr;
    PageManager* pages_ = nullptr;
};

#endif // PAGE_H
