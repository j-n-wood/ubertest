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

When the game's debug flag (`showAIDebug`) is on, the Droid Library renders the tunable
numeric fields — `maxSpeed`, `acceleration`, `deceleration`, `turnSpeed` (facing turn
rate, rad/s), `armour` — as raygui sliders bound to the in-memory `UnitDefinition`
(`UnitManager::getDefinitionMutable(id)`). Edits take effect for future instances; a
**Save to JSON** button calls `saveUnitDefinitionToFile(assetPath/units/<id>.json, def)`
so tuning persists without a restart. `topdown_game` defaults `assetPath` to the absolute
**source** assets directory (baked at build time via `GAME_SOURCE_ASSETS_DIR`), so saves
land in the real game-data files and survive rebuilds — not a throwaway copy beside the
binary. Override with `--asset-path` if needed. The display droid is intentionally **not** rebuilt
per edit (these fields don't change its appearance and rebuilding reloads models every
dragged frame).

In gameplay, with debug mode on, **F3** jumps straight to the Droid Library (pushed
directly onto gameplay, skipping the console menu) with its editor already up, so
accel/decel can be tuned and ESC drops right back to the game to test. Edits **live-apply
to already-spawned units of that type** (including the controlled player droid): those
instances share the edited definition object, so `acceleration`/`deceleration` are read
live by the motor each frame, and the library retunes `maxSpeed` (linear damping) and
`turnSpeed` (motor max-torque) on any matching live instance via
`unit_apply_movement_tuning`. No respawn or unit-type cycle is needed.

`turnSpeed` bounds the facing turn rate (rad/s). The motor's angular authority
(`maxTorque`) is derived per unit as `rotationalInertia · turnSpeed · UNIT_ANGULAR_DAMPING`,
so terminal turn rate = `turnSpeed`; a value of 0 falls back to `DEFAULT_TURN_SPEED`.
Previously `maxTorque` was a fixed constant against each unit's tiny rotational inertia
(thousands of rad/s²), so facing snapped instantly to the mouse/AI angle.

**V** toggles `showAIDebug` both in gameplay (the AI overlay) and from within the Droid
Library — gameplay input doesn't run while a console page is on top, so the library
mirrors the toggle on the same key so the editor can be turned on without leaving the
console. The description text is word-wrapped to the panel width by `drawWrappedText`,
which also honors any explicit `\n` in the JSON description.

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
