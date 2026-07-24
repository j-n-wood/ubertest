# Consoles

Consoles are static, **usable** map tiles (`type="console"`). Standing near the centre
of a console and pressing **SPACE** opens the console UI: the game simulation freezes
(as with `paused`) and a different set of screens render on top, built on the
[page system](pages.md). They are the simplest of the tile objects — no collision, no
animation, no Box2D body — the manager only tracks whether the player is close enough to
offer the "use" action.

In the original uberDroid the console showed deck/ship status, ship maps, and a droid
library. This build implements the **Droid Library** and **Status** screens; the alert
state and the top-down / side-on ship maps are deferred.

## Tile tagging

Console tiles carry one custom TSX property (parsed into `TmxTileProperties`):

- `type` (string) = `"console"`.

Console tiles stay in the baked static mesh (they are ordinary art — no animation), so
unlike doors/chargers there is no "type-only level" exclusion.

## Detection & proximity — `ConsoleManager` (`shared/level/console_manager.{h,cpp}`)

- `game_detect_consoles` scans tiles whose `tileProperties[gid-firstGid].type ==
  "console"` → `ConsoleSpec { physicsCenter, col, row }` (centre =
  `{col + 0.5 - halfW, row + 0.5 - halfH}` in physics space).
- `game_create_consoles` calls `consoleManager.init(specs)`; run on level load and on
  level switch, beside `game_create_chargers`.
- `update(Vector2 playerPos)` sets `playerInRange()` = player within
  `CONSOLE_USE_RADIUS` (0.4) of **any** console centre. Called each frame after the sim
  block in `game_update_gameplay`. No physics — a pure distance check.
- Torn down in `game_destroy` via `consoleManager.destroy()`.

`GamePage::update` shows a "Press SPACE to use console" prompt and, on SPACE while in
range, pushes `ConsoleMenuPage`.

## Screens (pages)

- **`ConsoleMenuPage`** — raygui menu: *Droid Library*, *Status*, *Exit*. Buttons push
  the sub-page; Exit / ESC pops back to gameplay (which resumes exactly where it froze —
  the sim never ran while console pages were on top).
- **`DroidLibraryPage`** — browse droid types. Owns a **private** zero-gravity Box2D
  world + `UnitManager` and a 3/4 orbit camera to show the selected droid's model
  spinning (same pattern as the `unit_test` tool), lit by the shared scene shader.
  UP/DOWN (or LEFT/RIGHT) cycle the type list (`getDefinitionIds()` sorted by class
  number, wrapped via `wrapIndex`); the panel shows name, class/type/drive/brain, weapon
  name, armour/energy, speed/accel/decel, turret/omni, and the description.
- **`StatusPage`** — current deck name (`levels[currentLevel].name`) and live droid count
  (`enemyUnits` with a valid body / `active`). No alert state yet.

## Debug editing (Droid Library)

When the game's debug flag (`showAIDebug`, toggled by **V**) is on, the Droid Library
renders the tunable numeric fields — `maxSpeed`, `acceleration`, `deceleration`,
`armour` — as raygui sliders bound to the in-memory `UnitDefinition`
(`UnitManager::getDefinitionMutable(id)`). Edits take effect for future instances; a
**Save to JSON** button calls `saveUnitDefinitionToFile(assetPath/units/<id>.json, def)`
so tuning persists without a restart. The display droid is intentionally **not** rebuilt
per edit (these fields don't change its appearance and rebuilding reloads models every
dragged frame).

raygui is used for all interactive UI. It ships with raylib
(`examples/shapes/raygui.h`); `RAYGUI_INCLUDE_DIR` exposes that directory and
`src/pages/raygui_impl.cpp` provides the single `RAYGUI_IMPLEMENTATION` translation unit.

## Tests

`tests/console_test.cpp`:
- `ConsoleManagerTest` — `playerInRange()` true only within `CONSOLE_USE_RADIUS` of a
  centre; empty list is never in range; nearest-of-many.
- `IndexWrapTest` — `wrapIndex` cycles both ends and handles empty/single lists.
- `PageManagerTest` — push activates + routes; a second push deactivates the previous
  top; pop reactivates it; empty stack is safe.

## Authoring

To exercise consoles in-game, a map tile must be authored with the `type="console"`
custom property in its TSX tileset. Walk onto it → "Press SPACE" → the console menu.
