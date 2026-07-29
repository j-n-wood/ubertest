# Weapons & projectiles

Firing droids. The weapon system is data-driven from
[`assets/data/weapons.json`](../assets/data/weapons.json) and split into a **simulation**
layer (Box2D projectile bodies + damage) and a **render** layer (additive flare sprites),
mirroring the door/charger split. See also [transfer.md](transfer.md) (what "controlled
unit" means) and [scoring.md](scoring.md) (kills award score).

## Phase 1 scope

This phase covers **projectile** weapons: weapon 0 (Plasma Bolt, the device/class 0) plus the
laser bolts weapons 2 (Laser Rifle) and 4 (Rapid Laser) carried by combat droids. Beam / area
/ instant weapons, floor "marks"
(`mark_radius`/splash), per-weapon colour/light, and particle systems are later phases —
non-projectile weapon types are parsed but do not fire yet (they're gated off in both the
player and AI fire paths so they don't consume cooldown or spawn bad projectiles).

## Data (`weapons.json`)

Each weapon (`shared/units/weapon.h : WeaponDefinition`):

| field          | meaning                                             |
|----------------|-----------------------------------------------------|
| `id`           | weapon id (a droid's `weapon` property indexes this; −1 = unarmed) |
| `name`         | display name                                        |
| `damage`       | damage per hit (subtracted from unit health)        |
| `speed`        | projectile velocity (world units/s)                 |
| `fireRate`     | cooldown between shots (seconds)                     |
| `maxRange`     | max travel distance (world units) → lifetime = `maxRange/speed` |
| `optimumRange` | AI preferred engagement range                       |
| `type`         | `projectile` \| `beam` \| `area` \| `instant`       |
| `damageType`   | armour-interaction tag (plasma/flame/cutter/…)      |
| `twin`         | fire two projectiles per shot                        |
| `radius`       | projectile physics (collision) radius, world units (default 0.1) |

Weapon 0 = Plasma Bolt: damage 11, speed 3.0 (world units/s), fireRate 0.8s, maxRange 12,
projectile → lifetime 4s. Speed/range are hand-tuned gameplay values (slow enough to read
the bolt in flight); the reference `weapons.txt` units are uncertain and not used directly.

The table is loaded once in `game_init` via `loadWeaponsFromFile`. **Until it's loaded,
`getWeaponDefinition` returns a no-weapon definition and nothing fires** — the table being
loaded is what makes both AI and player firing live.

## Player firing (`game_update_player_fire`, `src/game.cpp`)

LMB (`input.fire`, already suppressed while in transfer mode). Runs each frame in the
un-paused sim block, before the physics step, so bolts move the frame they spawn.

- Effective weapon = the **controlled unit's** weapon if it is armed, else the **device's**
  plasma bolt (the device is class 0 → weapon 0). `game->playerWeapon` is re-initialised
  whenever the effective weapon id changes; its cooldown ticks every frame.
- Only `WeaponType::Projectile` fires this phase; `tryFire` enforces the fire-rate cooldown.
- Aim direction is the unit's **current** body facing (`{-sin, cos}` of
  `b2Body_GetRotation`), not `playerDesiredRotation` — the body may still be slewing toward
  the cursor, so the bolt leaves along where it actually points this frame. Spawn position =
  controlled unit's body position + its `fireOffset` (rotated into world space, and clamped
  to the unit's `collisionRadius` so a stray offset can't spawn the bolt inside a wall).
- Owner = the controlled unit's `collisionGroupId` (Box2D `groupIndex`), so a bolt never
  hits the unit that fired it (nor the piloted unit while transferring).
- Skipped while `transfer.mode == Transferring` (fly-over) **and** while `input.transferMode`
  is armed (RMB / Left-Ctrl held) — you're aiming a capture then, not firing.

## AI firing (`shared/ai/ai_manager.cpp`)

Hostile armed droids fire at the player from the Chase state (`tryFireAtPlayer`). `canFire`
requires: armed, cooldown ready, within `maxRange`, **line of sight** (a wall cast via
`pathClear` — so they hold fire around corners), and facing alignment (body or turret head;
area weapons ignore facing). Same projectile-type gate and owner `groupIndex` as the player.

## Projectiles (`shared/combat/projectile_manager.h`)

A projectile is a dynamic Box2D bullet body: zero damping/gravity, constant linear velocity
(no drag). Physics radius is per-weapon (`WeaponDefinition::radius`, default
`PROJECTILE_RADIUS = 0.1`; weapon 3 = 0.2) — passed to `spawn` and stored on the projectile.
Category `PROJECTILE`, mask `UNIT|STATIC|DOOR`, `groupIndex = ownerId`.

Per-frame in the sim block (after the physics step): `update` (lifetime) → `syncFromPhysics`
→ `processContactEvents` → `cleanup`. On any contact the projectile deactivates and vanishes;
if it hit a unit, `applyDamage` subtracts `damage` from health. A unit reaching 0 health is
removed by `game_reap_dead` (which also awards score — see [scoring.md](scoring.md)). Walls
stop the bolt but take no damage. Same-owner bolts never collide (shared negative
`groupIndex`) and bolts don't collide with each other (`MASK_PROJECTILE` excludes
`CATEGORY_PROJECTILE`).

Health is `UnitInstance::combatState` (`currentHealth`/`maxHealth`, from `initCombatState`:
`energy × HEALTH_PER_ENERGY`, min 10). The droid's `energy` stat **is** its health
(droidclasses.txt: 20, 40, 100, …), so `HEALTH_PER_ENERGY` is 1. `armour` is a **flat**
damage reduction (`damage − armour`, per the original `destructible::take_damage`); a hit that
doesn't beat the armour is fully absorbed. Values are small (0–7). The controlled unit's health shows under the score on the HUD (numeric + colour-graded
bar); each AI unit's `cur / max` is appended to its V-mode debug label, so incoming damage is
visible.

> **Data fix.** The `droid_tool` parser read droidclasses.txt line 1 as
> `render_index typeCode energy armour weapon`, but the authoritative format
> (`uber/source/uberdroid/droid_class.cpp`) is `render_index number type energy armour` with
> **weapon on line 2**. The one-column shift meant every shipped unit JSON had `energy` set to
> the small 0-9 *tier* and `armour` set to the real energy — so e.g. class 16 had
> `armour: 100` (100% reduction = *immune*) and 100× health. The parser and the 24
> `assets/units/droid_class_*.json` energy/armour values are now corrected (weapon was already
> right). Per-damage-type armour and disruptor-immunity from the original loader
> (`cutter = ½ armour`, `disruptor = 0`, `disruptor_shielded`) are **not** modelled yet — only
> a single flat armour value is used, which is exact for plasma (the one active weapon).

Each Box2D body stores a pointer to its `Projectile::userData` for contact identification.
Because the projectiles live in a `std::vector` that reallocates on growth and compacts in
`cleanup`, `spawn`/`cleanup` re-bind every moved body's userData pointer to its new address —
without this a projectile spawned after a reallocation is never recognised on contact and
never deactivates (it bounces off units / lodges in wall corners).

## Rendering

Render-only, in `game_render_gameplay` inside `BeginMode3D` after `unitManager.renderAll()`,
as additive billboards at height 0.5 (lifted clear of the floor so the additive quad doesn't
z-fight with the ground). Each projectile carries its firing `weaponId`; the renderer selects
the look from it (per weapon id, since several plasma weapons must look different):

- **Weapon 3** (Plasma Cannon) → **animated ASMD blast**: a single **sprite sheet**
  (`asmd4x1.png`, 512×128 = a 4×1 row of frames, `TEX_ASMD`) cycled at 10 fps, drawn ~0.8
  across. The frame is chosen by moving the **source rect** across the sheet — one texture
  bind, no per-frame rebinds — from the projectile's own `age` via `SpriteAnimation`
  (see [textures.md](textures.md)), so bolts animate **independently, not in sync**.
- **Laser** (weapons 2, 4) → `blaster_blob.png` (128×32, a horizontal streak), drawn ~0.9
  long with its own aspect ratio, and **rotated to the travel direction** so the streak
  points the way the bolt is moving.
- **Everything else** (plasma weapons 0, 5, 7, …) → `flare.png`, a round glow, ~0.6 across.
  Weapons 5 and 7 are plasma but deliberately use the plain flare, not the ASMD blast.

Sprites use `DrawBillboardPro` with the billboard up-vector set to `camera.up`, **not** plain
`DrawBillboard`: the latter hardcodes up `{0,1,0}`, which for this straight-down camera is
parallel to the view direction, so `cross(up, toCamera)` degenerates and the quad collapses
to a 1px line. `camera.up` (`{0,0,-1}`) is perpendicular to the view, so the sprite lies flat
in the ground plane facing the camera, and `origin` = half-size centres it on the projectile.

The blaster_blob's long axis is image +X = the billboard's `right` (world +X at rotation 0).
`DrawBillboardPro` spins the quad about the view axis (+Y here), and rotating +X about +Y by
`a` gives `(cos a, 0, −sin a)`, so `a = atan2(−vz, vx)` (degrees) aligns the streak with the
velocity `(vx, vz)`. `flareTexture` and `blasterBlobTexture` load in `game_init`, unload in
`game_destroy`.

In **V mode** (`showAIDebug`) an opaque magenta `DrawSphere` is drawn at each projectile's
render position — an unmissable marker to distinguish a missing/washed-out additive sprite
from a bad position or a projectile that dies on contact after only a frame or two.

## Tests (`tests/weapon_test.cpp`)

Weapon-table parsing (types, twin, damage types, count, cooldown gating) plus a
`WeaponFileTest.ShippedPlasmaBolt` that loads the real `weapons.json` and asserts weapon 0's
stats. `ProjectileTestFixture` drives real Box2D: travel along heading, lifetime expiry, hit
+ damage, miss, and owner `groupIndex` pass-through (no self-hit).

## Deferred (later phases)

Beam / area / instant weapon behaviour; floor **marks** (`mark_radius`/splash); particle
systems and beam rendering; per-weapon colour/light. Visual sprite size is independent of the
per-weapon physics radius.
