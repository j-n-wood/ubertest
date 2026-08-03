# Transfer (influence-device control)

The player is not a combat droid but an **influence device** — a fixed **type-0** unit
(`droid_class_0`) that rides on top of and pilots AI droids, in the style of classic
Paradroid "transfer". The device is always present and rendered. It takes control of an AI
unit by ramming it in *transfer mode*; while piloting, the device is invulnerable and
non-colliding, and player input drives the captured unit instead of the device.

## Type-0 rule

There is only ever **one** type-0 unit: the player's device (classId 0). AI never spawns
classId 0 — `buildTypeClassMap` (`shared/level/spawn_config.cpp`) excludes it from the
spawnable pool, and placed-droid entries with classId 0 are skipped.

## Control states

`ControlMode` (in `src/transfer_control.h`). The **controlled unit** is the captured unit
when piloting, otherwise the device.

- **Free** — the device is driven directly (normal movement), collides normally, and is
  **vulnerable**. In transfer mode, ramming an AI unit begins a transfer.
- **Controlling(captured)** — player input drives the captured unit
  (`unit_set_move_target(captured, …)`); the captured unit's **AI is disabled**. The device
  is **rigidly welded** to the captured unit (a Box2D weld joint) so it tracks with no
  frame lag, its **collision is disabled** (so it can't be hit by projectiles or rammed),
  its motor is neutered (the weld owns its position), and it renders lifted on top. If the
  captured unit is destroyed, the device **detaches** in place (see below).
- **Transferring(from → target, 1.5s)** — the target's AI is disabled immediately (it holds
  station); the device **lerps** from its current position to the target over
  `TRANSFER_DURATION` (1.5s); **movement input is locked** and the device is invulnerable.
  On arrival it becomes Controlling(target).

A captured unit is **never handed back to the AI** — leaving it (by transferring away or by
its destruction) destroys it.

## Transfer mode

Hold **RMB or Left-Ctrl** to arm transfer mode (`Input::transferMode`). While armed:

- the player **cannot fire** (weapon fire is a later task; the suppression guard is in place);
- if the **active body** (the captured unit if piloting, otherwise the device) rams an AI
  unit — distance ≤ the sum of collision radii — a transfer to that unit begins. Any prior
  captured unit is destroyed first.

The capture plays a 1.5s fly-over animation (device moving from the old position — over the
old captured unit, or the free device position — to over the new unit) by linear
interpolation, during which the device is invulnerable (collision disabled).

## Detach on destruction

While Controlling, if the captured unit's health reaches 0 (`combatState`) or its body
becomes invalid, the device **detaches**: it is placed at the captured unit's last position,
its collision is re-enabled (vulnerable again), the captured unit is destroyed (with its death
explosion), and the mode returns to Free. **Gameplay rule:** losing your captured droid this way
**restores the device to full health** (`playerUnit->combatState.currentHealth = maxHealth`).

## Device overlay (weld + render lift)

Because physics is 2D, "on top" is a **render** offset: `UnitInstance::renderHeightOffset`
lifts a unit's whole model by a world-Y amount (0 for normal units). The device uses
`DEVICE_ATTACH_HEIGHT` (a constant in `src/transfer_control.h`) while overlaid. Horizontal
tracking uses a rigid **weld joint** created when Controlling begins and destroyed whenever
there is no captured unit (detach, transfer-away, reset), so the device moves with the
piloted unit in the same physics step (no lag). During the fly-over there is no captured
unit yet, so the device is hard-teleported along the lerp instead. If a per-type attach
height is wanted later, add a `UnitDefinition` property and use it in place of the constant.

## Invulnerability / collision

There is no damage-side "is player" guard; hittability is purely the Box2D **shape filter**.
`unit_set_collision_enabled(UnitInstance*, bool)` (`shared/units/unit_instance.cpp`) fetches
the unit's shape (`b2Body_GetShapes`) and sets the filter to `{CATEGORY_UNIT, MASK_UNIT,
collisionGroupId}` (on) or `{0, 0, groupIndex}` (off — collides with nothing, un-hittable),
mirroring the door open/closed filter toggle. The device uses "off" whenever it is over a
unit (Controlling/Transferring) and "on" when Free.

## AI disable

`AIComponent` gains a `controlled` flag. `AIManager::update` and `processCollisions` skip
components whose `controlled` is set, so a piloted unit is neither AI-driven nor
collision-redirected. `AIManager::findComponent(unit)` locates the component to toggle.

## Level transitions

A piloted unit travels with the player. On a level switch (`game_change_level`), the
captured unit's class **and current health** are recorded (`transfer_captured_class` /
`transfer_captured_health`) before transfer control is released; after the player device is
migrated into the new level's world, a unit of that class is re-piloted at the device and
its carried health restored (`transfer_recapture_class(class, health)`), so a damaged droid
stays damaged. `transfer_reset` **destroys the old captured instance** on the departed level
(silently — no explosion/score) so it isn't left behind as a duplicate that reappears on
re-entry. The captured unit is created in the **active level's world** (units live in
per-level worlds — see [levels.md](levels.md)); it is a respawn keyed by class+health, not a
physical body move. The device itself migrates worlds via `unit_rebind_world`.

## F1 / F2 (debug capture)

F1/F2 are a debug tool to test piloting without hunting for a droid: they create a captured
unit of the next/prev class (skipping class 0) at the device position and enter Controlling,
or change the current captured unit's type. The device itself always stays type 0.

## AI debug (V)

Toggle **V** for the AI overlay. Each AI unit shows its collision ring plus a
**detection-radius ring** that is **bright red while the unit is HOSTILE** (chasing the
player) and faint green otherwise, and a `HOSTILE>wp` tag (vs `P`/`F`) — so aggro state and
range are obvious at a glance. See docs/console.md for tuning `proximityRadius` live in the
droid library.

## Debugging collisions

Hold **B** in-game to draw *every* shape in the active world — including bodies not attached
to any game object — coloured by type (static=red, dynamic=green, kinematic=blue). It draws
each shape's true geometry (a **circle** for units, a box for polygon colliders), not its
AABB. Unlike the per-object overlays (**C** = level collision, **U** = unit shapes), it
iterates the whole Box2D world (`b2World_OverlapAABB` over a world-spanning box), so it
exposes stray/orphaned colliders behind "invisible wall" movement blocks. (With per-level
worlds it only ever shows the active level.)

## Deferred

- **Weapon fire** — LMB firing the controlled unit's weapon (and suppression in transfer
  mode) is the next task; only `Input::fire`/`transferMode` and the guard are added here.
- **Free-device death / game-over** is out of scope.

## Tests

`tests/transfer_test.cpp`: the spawn pool never yields classId 0 after exclusion; the pure
transfer-progress/lerp helper (fraction over 1.5s, clamped, position interpolation).
