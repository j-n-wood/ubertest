# Pages & the PageManager

The game's screens are modelled as a **stack of pages**. A *page* is one full-screen
"view" — gameplay, a console screen, a (future) title screen. Only the top page of the
stack is live: it receives input, updates, and renders each frame. This is the
view-state system the console feature is built on; adding a new screen means adding a
new `Page`, not branching the main loop.

## `Page` (`src/pages/page.h`)

Abstract base. Holds non-owning pointers to the shared `Game` state and the owning
`PageManager` (so a page can push/pop others). Virtuals, all optional to override:

| Method            | When it fires                                            |
|-------------------|----------------------------------------------------------|
| `activate()`      | Became the active (top) page (on push, or when the page above it popped). |
| `deactivate()`    | Stopped being the active page (a page was pushed over it, or it was popped). |
| `handleInput()`   | Per frame, before `update`.                              |
| `update(float dt)`| Per-frame logic.                                         |
| `render()`        | Per-frame draw.                                          |

A page owns whatever transient resources its screen needs and frees them in
`deactivate()`/destructor (e.g. `DroidLibraryPage` owns a private Box2D world +
`UnitManager` for its spinning display droid).

## `PageManager` (`src/pages/page_manager.{h,cpp}`)

Owns `std::vector<std::unique_ptr<Page>>` as a stack and drives the top page:
`update(dt)` calls `handleInput()` then `update(dt)`; `render()` calls `render()`. `top()`
returns the active page (or `nullptr` if empty); `empty()` tests the stack.

**Push/pop are deferred.** `push()` / `pop()` only queue the request; the change is
applied by `applyPending()` at the *start* of the next `update()`/`render()`. This lets a
page call `pages_->pop()` (or push) from inside its own `handleInput`/`update` without
destroying itself mid-method. Pops are applied before pushes, so a pop-then-push in the
same frame ends with the pushed page on top. As the top changes, the outgoing page gets
`deactivate()` and the new top gets `activate()`.

## Wiring (`src/main.cpp`)

`main` owns the `Game` and a `PageManager`, pushes a `GamePage`, and the loop is just:

```cpp
pages.update(dt);
pages.render();
```

`GamePage` (`src/pages/game_page.{h,cpp}`) wraps the existing gameplay unchanged: its
`update` calls `game_update_gameplay(game_, dt)`, its `render` calls
`game_render_gameplay(game_)`. When the player is on a console tile and presses SPACE it
pushes a `ConsoleMenuPage`. Because the console pages sit *on top*, `GamePage` stops
updating while they're open — the simulation freezes exactly like `paused`, and resumes
where it left off when they pop. See [console.md](console.md).

## Current pages (`src/pages/`)

| Page              | Purpose                                              | Pushed from |
|-------------------|------------------------------------------------------|-------------|
| `GamePage`        | The gameplay view — the base of the stack; wraps `game_update_gameplay`/`game_render_gameplay`. | `main` at startup |
| `ConsoleMenuPage` | Console menu shown when using a console tile.        | `GamePage` (SPACE on a console tile) |
| `StatusPage`      | Ship/droid status readout.                           | `ConsoleMenuPage` |
| `DroidLibraryPage`| Spinning-droid library/debug viewer; owns a private Box2D world + `UnitManager` for its display droid. | `ConsoleMenuPage`, and `GamePage` F3 in debug mode |
| `ShipViewPage`    | Side-on ship diagram + lift destination chooser. Reads the ship images from the shared `TextureManager` (doesn't own them). | `GamePage` (SPACE on a lift tile) |

While any of these sit on top of `GamePage`, gameplay is frozen (its `update` isn't called),
resuming when they pop. `raygui_impl.cpp` provides the immediate-mode GUI backend some pages use.

## Adding a page

1. Subclass `Page`, override what you need, free resources in `deactivate()`/dtor.
2. Push it from wherever the transition happens (`pages_->push(std::make_unique<T>(game_, pages_))`).
3. Pop (or let ESC pop) to return. `src/pages/*.cpp` is picked up automatically by the
   `GLOB_RECURSE` in `CMakeLists.txt` — no CMake edit needed for the game target.

Tested in `tests/console_test.cpp` (`PageManagerTest`): a fake page verifies push
activates + routes, a second push deactivates the previous top, and pop reactivates it.
