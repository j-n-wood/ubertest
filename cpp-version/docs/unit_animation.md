# Unit animation & facing

How a unit's *visible motion* is produced: the procedural rotation of its **sections**
(turrets, heads, tracks) and the **GLTF skeletal animation** of animated meshes (legs).
This is a **render/sim split** feature like the rest of the engine (see `CLAUDE.md`): section
facing angles and animation frames are *presentation* state advanced in `UnitManager::update`
and drawn read-only; the **firing angle** and **detection** that some of these feed back into
are *simulation* and live in the AI / combat layer.

> **Status.** Implemented. Section roles (turret/head), the head vision cone, `fireWhileMoving`,
> the redefined `omnidirectional`, and idle/move GLTF animation are all wired, for both AI and
> the player-controlled unit. Shipped markers so far: class 16 `section_1` = turret; class 20
> `fireWhileMoving`; the `legs.glb` root of classes 11/13/14/17/19 = `animMoving`. Behaviour is
> covered by `tests/ai_test.cpp` and `tests/unit_json_test.cpp`.

## The section tree

A unit is one physics body (`UnitInstance::bodyId`) plus a tree of render-only
`SectionInstance`s (`shared/units/unit_instance.h`). Each frame `UnitManager::update` reads the
body's world position/rotation into the root section and recurses via
`updateSectionTransforms`, composing each child's `offset` (facing-relative) and rotation onto
its parent. Sections carry a model but **no physics** (physics shapes on sections are only used
for debris when a unit is dismantled).

### Rotation modes (`SectionRotationMode`)

Each `SectionDefinition` has a `rotationMode` (JSON `"rotationMode"`, default `FollowUnit`):

| mode           | world rotation of the section                          |
|----------------|--------------------------------------------------------|
| `FollowUnit`   | `parentWorldRot + localRotation` — rigid with the body (tracks, hull) |
| `FollowFacing` | `section->facingAngle` — an **independent** angle slewed toward an aim target |
| `Fixed`        | `localRotation` — a constant world angle, ignores the body |

`FollowFacing` is the mechanism behind turrets and heads: `facingAngle` is a render-only scalar
that is *slewed* (not snapped) toward a desired angle each frame at the turret rate, so the part
visibly swings around.

## Section roles: turret vs head

Two parts rotate independently but mean different things. They are distinguished by a section
**role** marker (`"role": "turret" | "head"`, which implies `FollowFacing`):

- **Turret** — the section whose facing **determines the firing angle**. AI (and the player)
  aim it at the current target; a shot leaves along the turret's `facingAngle`, and the fire
  gate checks *turret* alignment, not body alignment.
- **Head** — rotates independently exactly like a turret, but is **only used for visibility**.
  A unit with a head can only *see* (detect / keep line of sight on) what lies within a facing
  cone of the head's `facingAngle` (a dot-product test). A head never sets the firing angle.

A unit has at most one of each. The AI caches a pointer to each aiming section at spawn
(`AIComponent::turretSection` / `headSection`, via `unit_find_section_by_role`); `hasTurret` /
`hasHead` are **derived** from their presence (a `hasTurret` prop with no turret section has
nothing to aim, so it is ignored).

### The aim angle: target, else movement direction

There is always a **desired angle** a turret/head slews toward:

- If a **target is selected** (the player, in the AI's `Chase` state; the cursor, for a
  player-controlled unit) → the angle to that target.
- **Otherwise** → the unit's movement/body direction, so an idle or patrolling turret settles
  facing forward instead of freezing at its last angle.

The turret **slew rate** is independent of and generally **faster** than the body's turn rate.
The body turns at the per-unit `turnSpeed` (rad/s, baked into motor torque — see
`movement_tuning.h`); the turret slews at the per-unit `turretTurnSpeed`, and the head at its own
`headTurnSpeed` (each independent), if set, else the global
`TURRET_SLEW_RATE` (8 rad/s). For AI this happens in `AIManager::updateAimingSections`; for the
player-controlled unit, `game_update_player_turret` slews the same sections toward the cursor.

> **Inspecting it.** The Droid Library (F3 in debug mode) has a **facing test**: press SPACE to
> stop the pedestal spin and aim with the mouse — the body slews toward the cursor at `turnSpeed`
> while the turret slews at `turretTurnSpeed` and the head at `headTurnSpeed`, with facing lines
> (body / turret / head) and a target marker drawn so the different heading + rate are visible. The
> debug-edit panel (V) has live `turn (rad/s)`, `turret rate`, and `head rate` sliders. See
> `src/pages/droid_library_page.cpp`.

## Unit-level facing behaviours

How the **body** orients and whether the unit stops to fire depends on a few unit properties:

Stopping to fire is the **general case** — a droid halts inside `optimumRange` to shoot. Only
`fireWhileMoving` and `omnidirectional` units keep maneuvering while firing.

| behaviour            | body facing while engaging                         | stops to fire? | LOS facing gate |
|----------------------|----------------------------------------------------|----------------|-----------------|
| **Standard**         | halts inside `optimumRange`, turns body to the target | yes         | body must be aligned (`AI_FACING_THRESHOLD`) |
| **Turret** unit      | maneuvers while approaching, then **halts**; body holds its facing while the turret aims | yes | turret alignment |
| **fireWhileMoving** (type 20) | body **tracks the target** while still moving | **no** | **none** |
| **omnidirectional**  | never orients — body angle held at **0**           | no             | none (facing is meaningless) |

A turret unit is not special-cased for *halting* — it stops to fire like a standard droid. What
the turret buys is **aiming**: the turret tracks the player throughout (including the approach,
while the body follows its movement direction), and when the body halts it **holds its facing**
rather than swinging to the player, since the turret is doing the aiming.

### `fireWhileMoving` (type 20)

Type 20 "treats the entire unit like a turret": the **body itself** is the aiming part — it
faces the target when one is selected, else the movement direction — and the unit **does not
halt to fire**. It also drops the line-of-sight facing restriction (dot-product threshold `0`),
because a body that is still slewing onto the target should not have its shots withheld. Marker:
unit-level `"fireWhileMoving": true`. Such a unit skips the standard halt-at-optimum-range path,
faces the player while moving, and `canFire` returns without a facing check.

### `omnidirectional` (redefined)

Per these notes, `omnidirectional` means the unit **does not consider facing at all** and holds
a **zero rotation** — a shape that reads the same from every side. This is a **behavioural change**
from the current code, where `omnidirectional` is (mis)used to mean "faces the player while
moving." No shipped unit sets `omnidirectional: true` today, so the redefinition has no data
impact; the old "face-while-moving" behaviour is what `fireWhileMoving` now covers.

## GLTF skeletal animation: idle vs moving

Some meshes (`legs.glb`, used by classes 11, 13, 14, 17, 19) ship with skeletal animation
**clips**. These models are loaded **per-instance** (the `ModelCache` probes for animations and
refuses to share an animated model, so each unit holds its own skinned pose —
`unit_manager.cpp` / `model_cache.cpp`), and their `ModelAnimation*` handles are already loaded
onto the `SectionInstance`.

Such sections are marked `anim_moving`. In the shipped `legs.glb` the clips are ordered **clip 0
= walk, clip 1 = idle** (the reverse of the old paradroid note), so the playback rule
(`UnitManager::update`) is:

- A section marked `"animMoving": true` selects `currentAnim = (unit is moving) ? 0 : 1`, where
  "moving" is the unit's body speed above `ANIM_MOVING_SPEED` (0.25 u/s). A single-clip model
  always uses clip 0. Switching clips resets to frame 0.
- Frames advance at 30 fps (`ANIM_FRAME_TIME`) and loop.
- Sections **without** the marker keep the prior behaviour (no skeletal playback).

## Minimum marker set

The smallest set of hand-added JSON markers that unlocks every behaviour above:

| marker | level | type | meaning |
|--------|-------|------|---------|
| `role` | section | `"turret"` \| `"head"` | independently-aiming section; turret drives firing angle, head drives visibility. Implies `FollowFacing`. |
| `animMoving` | section | bool | this section's GLTF clip 1 plays while moving, clip 0 while idle. |
| `fireWhileMoving` | unit (`properties`) | bool | body aims at target, unit does not halt to fire, no LOS facing gate. |
| `omnidirectional` | unit (`properties`) | bool | never orient; hold body angle 0. (Existing field, redefined.) |
| `turretTurnSpeed` | unit (`properties`) | float, optional | per-unit **turret** slew rate (rad/s); absent → global `TURRET_SLEW_RATE`. |
| `headTurnSpeed` | unit (`properties`) | float, optional | per-unit **head** slew rate (rad/s), independent of the turret; absent → global `TURRET_SLEW_RATE`. |

`hasTurret` / `hasHead` on the unit are **derived** from the presence of a `role` section (kept as
a cached capability flag the AI reads); they need not be hand-authored alongside the section role.

These are applied **by hand** to the unit JSON — the legacy converter is retired (see `CLAUDE.md`).

## Worked examples

- **Class 16 (turret).** `section_1` (`dischead_med.gltf`) is marked `"role": "turret"`. The hull
  and tracks stay `FollowUnit`; the disc head swings toward the player and the fire gate checks the
  head.
- **Class 20 (fire-while-moving).** `properties` has `"fireWhileMoving": true`. It has no turret
  section — its **body** is the aimer, so it keeps moving while facing and shooting the player and
  never withholds fire on facing. (Twin weapon 7 still fires from the mirrored `fireOffset`.)
- **Legs units (classes 11/13/14/17/19).** The `legs.glb` root section is marked
  `"animMoving": true`; its walk cycle (clip 0) plays while the unit is moving, the idle pose
  (clip 1) when stopped.
- **A head unit.** Mark the sensor/head section `"role": "head"`; the unit only detects and keeps
  LOS on the player within the head's forward cone, but fires along its body like a standard unit.
  (No shipped unit is marked this way yet — the behaviour is covered by tests.)

## Sim/render split & tests

Facing angles and animation frames are advanced in `UnitManager::update` (presentation) and read
read-only when drawing. The two values that feed back into simulation — the **turret firing
angle** and the **head visibility cone** — are consumed in the AI/combat layer
(`shared/ai/ai_manager.cpp`: `updateChase` slews the turret, `canFire` gates on its angle,
`updatePatrol` gates detection on the head cone). Keep the split: the AI *reads* `facingAngle`,
the manager *advances* it. New behaviour should come with unit tests alongside
`tests/ai_test.cpp` / `tests/weapon_test.cpp` (turret alignment gating fire, head cone gating
detection, move-animation clip selection by speed).
