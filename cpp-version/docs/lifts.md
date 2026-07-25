# Ship lifts

Lifts (elevators) let the player travel between decks. Standing on a lift tile and
pressing **SPACE** opens the [side-on ship view](pages.md); the player picks a destination
deck up/down the elevator shaft and travels there, arriving on that deck's lift tile. The
player also **spawns on a lift tile** of the starting level when one exists (falling back
to the first waypoint), so the lift is usable immediately. In the ship view, **W/S** (or
UP/DOWN) choose the deck and ENTER travels — W/S mirror the movement keys so the controls
don't change. (The droid-library type browser accepts W/S the same way.)

## Historic handling (FreedroidClassic)

The classic engine used two data files (here, `uber/uberdroid/ship0/`):

- `lifts.txt` — **rendering**: 8 `Elevator` shaft rects + 16 `Domain` (deck) rect groups,
  in **pixel coordinates of the 578×211 `ship_on.png`** (used 1:1 as source rects into the
  image; `ship.c` `ShowLifts`). The classic UI lit the current deck by blitting sub-rects
  of a "lights on" image over a "lights off" base.
- `transport.txt` — **logic**: `Label` entries, each a stop `(Deck, PosX, PosY)` linked to
  other stops via `LevelUp`/`LevelDown`, grouped by `LiftRow` (elevator). A lift was
  identified by *(current deck, tile x, tile y)*; up/down walked the linked stops; the
  destination stop's `PosX/PosY` became the player's new tile (`map.c` `GetCurrentLift`,
  `ship.c` `EnterLift`). Deck N == the level whose number is N.

## New handling

**Logical control data lives in the maps.** Each lift tile carries a point **object** (on
a Tiled object layer, e.g. `lifts`) with int custom properties `elevator` and
`stop_index`. The TMX loader (`shared/level/tmx_loader.cpp`) parses any object with an
`elevator` property into `TmxLevel::lifts` (`TmxLift{col,row,elevator,stopIndex}`),
converting the object's pixel position to a tile via `col=x/tileWidth`, `row=y/tileHeight`.
(Objects without `elevator` remain waypoints.) Per-cell tile-layer properties aren't used
because Tiled shares tile properties across all placements of a tile id.

**`LiftManager`** (`shared/level/lift_manager.{h,cpp}`) builds the elevator graph once from
all loaded levels: every `TmxLift` becomes a `LiftStop {level, levelNumber, col, row,
elevator, stopIndex, physicsCenter}` (physics centre via the origin-centred
`{col+0.5-w/2, row+0.5-h/2}` formula). Stops are grouped per elevator and ordered by
`stopIndex` (**0 = lowest deck, ascending upward**). `update(playerPos, currentLevel)` sets
`onLift()`/`currentStop()` within `LIFT_USE_RADIUS`; `stepStop(stop, ±1)` returns the
adjacent stop on the same elevator (null at the ends). Logic uses the **runtime** level
index, so it is unaffected by the lexicographic level sort.

**Rendering data** — `assets/ships/ship1/shipmap.json`, converted from `lifts.txt` as
**fractions of 578×211** (`x/578`, `y/211`) so it's image-size independent:
```json
{ "image": "ship_on.png", "refWidth": 578, "refHeight": 211,
  "elevators": [ {"x","y","w","h"}, ... ],          // index = elevator id
  "decks":     [ {"level": N, "rects": [ {"x","y","w","h"}, ... ] }, ... ] }
```
`ShipMap` (`shared/level/ship_map.{h,cpp}`) loads it; `decks[].level` is the stable level
**number** N (parsed from `level_<N>_name.tmx` into `TmxLevel::number`), so highlights map
to the right deck regardless of load order. Two classic 578×211 images are used (the old
`ship.jpg` is unused — wrong aspect): `image` = `ship_off.png` (the dim "lights off" base)
and `imageLit` = `ship_on.png` (the "lights on" version). The view draws the dim base,
then "lights up" the accessed elevator shaft and the current/selected decks by blitting the
matching sub-rects of the lit image over the base (as the classic engine did) — so the
current deck is clearly highlighted rather than lost in an all-lit image.

**`ShipViewPage`** (`src/pages/ship_view_page.{h,cpp}`) draws the ship image scaled to fit
(`DrawTexturePro`, aspect-preserved), then overlays translucent rectangles: the accessed
elevator shaft (blue), the player's starting deck (green), and the selected destination
deck (yellow). UP/DOWN move the selection along the elevator via `stepStop`; ENTER calls
`game_switch_to_stop` (switch level + place the player on the destination lift tile) and
closes; ESC cancels. Opened from `GamePage` when `liftManager.onLift()` and SPACE is
pressed (lift and console tiles are distinct, so only one action fires).

`game_switch_to_stop` (`src/game.cpp`) shares the level-rebuild + player-teleport path with
debug level switching (`game_change_level` / `game_teleport_player`), replacing the old
stale GID-based `game_find_lift_positions`.

## Authoring

To add a lift, place a point object on a `lifts` object layer at the lift tile with
`elevator` (int) and `stop_index` (int, ascending upward). Stops sharing an `elevator`
across levels form one shaft. **All 8 elevators** (0–7, 30 stops) are authored across the
`level_*.tmx` files, matching the classic `transport.txt` graph — each stop placed on the
real `type=lift` tile whose column matches the classic `PosX`. Deck highlighting requires a
matching `decks[].level` entry (by level number) in `shipmap.json`.

Note: the classic `PosY` values don't line up with the new maps (only `PosX`/column does),
so stops were matched to lift tiles by column. Deck 5 is the one spot where two elevators
(3 and 4) share column 4 with two lift tiles; they were paired by ascending `PosY`↔row and
may warrant a visual check.

## Tests

`tests/lift_test.cpp`: `LiftManager` groups/orders stops per elevator and `stepStop`
walks up/down (null at ends); separate elevators don't chain; proximity is true only near
the tile centre on the current level; `ShipMap` parses fractional rects and resolves decks
by number.
