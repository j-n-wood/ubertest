# Scenery entities (ship1) — survey + 3D rendering roadmap

The `Objects3D` renderer draws walls/floors + doors/chargers/consoles. Every OTHER placed object in
ship1 is invisible today — dropped at parse time or never exported. This documents what those objects
actually are (by matching each record's `renderIndex` to `renderobjects.txt`) and the plan to render /
handle them. **Documentation only — no code yet; the phases are the sequenced backlog.**

## Design goal

An **extensible, data-driven object system** modelled on the existing **unit type** system. Object
*types* are defined once in JSON (model, scale, whether they float or have a physics footprint, whether
they're destructible + health, which shader/drawtype), and each level's `entities.json` places
*instances* referencing a definition by name plus a transform — exactly as
`assets/units/droid_class_*.json` are definitions and spawns place instances of them. So the plan is
**one `ObjectManager` + object-definition catalog**, not a set of bespoke renderers (console-style).
Rendering must support **switching shaders** per object type, notably for shadow rendering.

## What's actually in ship1

There are **no benches/seats/pillars placed in ship1**, and **no distinct keywords** for
pillar/bench/seat/decal/shadow. Every placed thing is one of **6 object keywords**, and the visual is
selected purely by the `renderIndex` integer. The bench/seat/pillar render-objects (Bench1/47,
BChairTable/48, slickchair/82, table/84…) and the unreferenced `plinth.txt`/`containtube.asc` assets
exist but are placed in other ships, not ship1. (`ship0` was audited — identical object palette, no new
types; the game converts `ship1` via the viewer's `DEFAULT_SOURCE_PATH` → `assets/ships/ship1/levels3d/`.)

### Census — ship1 placed objects (312 records across xmapfile0–15)

| Keyword | renderIndex ×count | model (renderobjects.txt) | drawtype | height | category |
|---|---|---|---|---|---|
| `Door` | 1 ×131 | scenery/door.asc | BUMP | floor | **done** (Door3DRenderer) |
| `Console` | 37 ×54 | scenery/console.asc | DIFFUSE | floor | **done** (Console3DRenderer) |
| `Charger` | 11 ×45 | (particles) | ADD | floor | **done** (Charger3DRenderer) |
| `Object` | 42 ×34 | scenery/alert.asc (alert light) | diffuseglow | **floats above units** (z≈95) | cosmetic, non-interactive, **casts shadow** |
| `Object` | 49 ×30 | scenery/lifttop.asc (lift top) | glowscroll/diffuseglow | **floats** (z≈95) | cosmetic, non-interactive, **casts shadow** |
| `Object` | 147 ×4 | scenery/fan.txt | **SHADOWCAST** | ceiling (z≈102), **SPIN** | **shadow-only caster** (invisible mesh, casts a spinning shadow) |
| `Destructible` | 79 ×12 | scenery/tank.asc | DIFFUSE | floor | **destructible**: static, **collides**, destroyable (EXPLODESIZE/FLAMECOUNT) |
| `Destructible` | 42 ×1 | scenery/alert.asc | diffuseglow | — | destructible (edge case) |
| `Organic` | 65 ×1 | models/grey.md2 | DIFFUSE | floor | destructible, **MD2 — DEFERRED** (we don't support MD2) |

Collision (per the intended behaviour, not the raw `MASS 0` in the data): the **floating `Object`
scenery (alert/lift-top/fan) does NOT collide** — it hovers above the play space; the **`Destructible`
tanks DO collide** (static solid footprints, like the console) and can be destroyed. `FIXED` on the
destructibles.

**`ALWAYSRENDER` (on all these records) is a visibility flag, not a draw-order one:** it means the
object is **exempt from line-of-sight / visibility culling** — an artefact of the uber object class
hierarchy in which **units ARE LOS-culled but scenery is not**, so scenery is always drawn. (Related,
for later: in uber, LOS could suppress a unit's geometry and draw a **marker** in its place instead.
The cpp game has no LOS culling today; this is future context, not part of the object work.)

Record format (all 6 keywords share `object::load`): `id / pos xyz / rot xyz / collide prox /
renderIndex / [MASS|ALWAYSRENDER|FIXED|SPIN] … END`; `Destructible`/`Organic` add
`EXPLODEDAMAGE|EXPLODESIZE|FLAMECOUNT`. Facing is `rot.z`; `SPIN z` gives continuous rotation (fans).

## Object-definition architecture (mirrors unit types)

**Object definitions** — a catalog of JSON files (e.g. `assets/objects/<name>.json`), one per object
*type*, exactly parallel to `assets/units/droid_class_*.json`. Each definition carries:
- `id`/`name`, `model` (gltf path, loaded through the shared `ModelCache` like unit models), `scale`;
- `floating` (bool): if true, no physics footprint (hovers, e.g. alert/lift-top/fan); if false, a
  **collision footprint** (radius or box half-extents, like the console) so it blocks movement;
- `destructible` (bool) + `health`/`explodeDamage`/`explodeSize`/`flameCount`: unit-style health mechanics;
- `shader`/`drawtype` (`diffuse` | `bump` | `glow` | `shadowOnly` | …): selects the render path,
  including whether the mesh is drawn at all (`shadowOnly` = shadow pass only) and whether it casts a shadow;
- `alwaysRender` (bool, default true for scenery): **bypass line-of-sight / visibility culling** —
  always drawn (uber's `ALWAYSRENDER`). Units are LOS-culled by contrast;
- optional `spin` (default continuous rotation), `castsShadow` flag.

**Instances in `entities.json`** — each level's bundle lists placements referencing a definition by
name + a transform: `objects: [ { def: "tank", pos:[x,y,z], rot:[..], spin:[..] }, … ]` — the same
shape as spawns referencing droid classes. **The converter maps uber `renderIndex` → definition name**
(42→`alert_light`, 49→`lift_top`, 79→`tank`, 147→`fan_shadow`) via a small authored table; the map
instances become `entities.json` `objects[]` entries.

**Runtime** — an `ObjectManager` (paralleling `UnitManager`): `preloadDefinitions("assets/objects/")`,
then per level create instances from the bundle placements. It owns per-instance transform + (for
destructibles) health, exposes instances for the renderer, creates static collision bodies for
non-floating defs, and runs a reap sweep for destroyed destructibles. A single manager + catalog
replaces bespoke per-type renderers (console/door/charger can stay as-is or migrate later).

### The four ship1 object types as definitions

| def name | renderIndex | model | floating? | collides? | destructible? | shader |
|---|---|---|---|---|---|---|
| `alert_light` | 42 | scenery/alert.asc | yes (z≈95) | no | no | glow (+ casts shadow) |
| `lift_top` | 49 | scenery/lifttop.asc | yes | no | no | glow (+ casts shadow) |
| `fan_shadow` | 147 | scenery/fan.txt | yes | no | no | **shadowOnly** (mesh never drawn, spins) |
| `tank` | 79 | scenery/tank.asc | no | **yes** | **yes** (health) | diffuse |

(Organic/65 MD2 is out of scope. Barrels/crates/etc. exist in `renderobjects.txt` but aren't placed —
adding one is just a new definition file if ever needed. Runtime `dirty_t` decals + the map `Feature`
detail layer are separate systems, deferred.)

## Shader switching + shadows (the rendering work)

Today `SceneRenderer` binds **one** lit shader to everything. The object system needs the renderer to
**switch shaders per definition drawtype** and to run a **shadow pass**:
- **Shadow pass (first-class):** render all shadow *casters* — **unit models** (the main win: shadows
  are invisible under the old 2D parallel projection but read correctly in 3D perspective, especially
  for units that float/have raised sections), floating scenery (alert/lift-top), and `shadowOnly`
  casters (the fan, whose mesh is drawn ONLY here) — with a dedicated shadow shader into a shadow/depth
  target or as projected blobs. A new shared feature spanning units + objects.
- **Glow:** an emissive term/variant for `glow` definitions (alert/lift-top; `lighting.fs` has no glow term today).
- Definitions declaring `shadowOnly` skip the normal lit pass entirely; `castsShadow` gates the shadow pass.

## Current data-model gaps

- `parseGenericObject` (`shared/scene_convert/domain_parser.cpp:400`) parses `Object` →
  `GenericObject` with `typeId = renderIndex` (`:429`), but there is **no `Destructible`/`Organic`
  dispatch** (`:677` only handles `Object`) — those records are **dropped**. `scene_types.h` has a
  `Destructible` struct (`renderIndex`, `model`, `hitPoints`) that is never populated.
- The bundle export (`tools/incremental_viewer/level_export.cpp writeEntities`) writes
  doors/chargers/consoles but **nothing for generic objects or destructibles**.
- There is no **object-definition catalog / `ObjectManager`**, no `renderIndex → def` mapping, no
  object health/damage/reap, and no **shader switching / shadow pass** or glow. (`renderIndex` is
  captured as `GenericObject.typeId` but currently maps to nothing.)

## Reference patterns to reuse

- **Definition catalog + instancing:** `UnitManager` (`shared/units/unit_manager.h`) —
  `preloadDefinitions(dir)`, `loadDefinition/getDefinition`, `createInstance(defId, pos, rot, …)`, with
  a shared `ModelCache` for gltf models; `UnitDefinition` (`shared/units/unit_types.h`) is the schema
  template. `ObjectManager` mirrors this shape.
- **Bundle parse→export→load→render→collision:** the console pipeline — `parseConsole` →
  `scene_types.h` struct → `writeEntities` → `load3DLevelConsoles` → spec → `Console3DRenderer`
  (`LoadModel`+`sceneRendererApplyShader`+`DrawModelEx`) → `game_create_consoles` (mode-gated build +
  static collision footprint) → teardown. Two differences for objects: **place at the full 3D position**
  (ceiling-height, not floor-seated) and support **spin**.
- **Destructible health/death:** the unit damage path — `UnitCombatState` +
  `applyDamage`/`accumulateRealtimeDamage` + `game_reap_dead` (`src/game.cpp`) — is the model for
  destructible-object health/reap/explosion.

## Phased roadmap

**Phase 1 — Object-definition system + static/floating instances.** Introduce `ObjectDefinition`
(schema above) + `ObjectManager` (mirror `UnitManager`, share `ModelCache`); author the 4 def JSONs
(`assets/objects/{alert_light,lift_top,fan_shadow,tank}.json`) and convert their `.asc` models via
`model_tool`. Converter: parse `Object` + add the missing `Destructible`/`Organic` dispatch, map
`renderIndex → def name`, and export `entities.json objects[]` (def, pos, rot, spin). Load instances
per level and draw each at its **full position + rot + spin** with the plain lit shader. Non-floating
defs (tank) get a static collision footprint; floating defs don't. (`fan_shadow` shows nothing until
Phase 3.) Result: alert lights, lift tops, tanks appear, driven entirely by data.

**Phase 2 — Destructible mechanics (DONE).** A grounded object's static footprint now carries
`BodyUserData{BodyTag::Object, &ObjectInstance}` (new `Object` tag), so a projectile contact resolves
back to the instance: `ProjectileManager::processContactEvents` decrements `inst.health` for
`destructible` defs (single-hit, immediate — same place unit hits call `applyDamage`). A reap sweep
`game_reap_objects` (mirrors `game_reap_dead`, runs right after it each step) fires the shared
`game_spawn_explosion` at the object's ground position (owner group 0 = no unit's group, so the blast
hurts nearby droids) and destroys the footprint. The dead instance is **marked `!alive`, not erased** —
its address lives in the body userData, so keeping the slot avoids dangling; render/shadow/collision
all skip it. The footprint's `collisionBodies` slot is invalidated before `b2DestroyBody` so
level-teardown can't double-free. `ObjectInstance` gained `bodyId` + `bodyUserData`; `ObjectManager`
gained `instancesMut()`. Tanks (deck 10, health 50) block movement and explode when shot to death.

**Continuous damage + blast scaling.** Beyond single-hit projectiles, objects now take *continuous*
damage from the two accumulating sources, via a per-instance accumulator (`pendingDamage` /
`damageAccumTimer`) flushed on the shared `REALTIME_DAMAGE_INTERVAL` tick in `ObjectManager::update` —
the exact `UnitCombatState` pattern, so `health` is now a float. `accumulateObjectDamage()` (a no-op
on floating/cosmetic/dead instances) is the single entry point: **explosions** — `EffectManager`'s
overlap now masks `CATEGORY_UNIT | CATEGORY_STATIC` and its callback branches on the body tag
(`Unit` → `accumulateRealtimeDamage`, `Object` → `accumulateObjectDamage`; untagged walls ignored),
so a tank blast **chains** to neighbouring tanks; **beams** — the beam cast already stops on
`CATEGORY_STATIC`, so it now also carries the hit `ObjectInstance` and feeds it `dps*dt`. Explosions
**scale** by a per-effect `sizeScale` (radius, core, cutoff, and the visual billboard all stretch
together — the falloff curve is reused by dividing measured distance by the scale); a destroyed
object passes its def's `explodeSize` (tank = 1.6), so a bigger object makes a bigger boom with wider
chain reach. Spark burst is left unscaled for now.

**Phase 3 — Shadow mapping (DONE; the biggest new feature).** Real depth-map shadows that land on
**every** surface below a caster — so a ceiling fan/light shadows units passing underneath (the planar
projection couldn't; it only flattened onto the floor). `ShadowMap` (`shared/rendering/shadow_map.*`)
renders a depth pass from the light's POV — orthographic, straight down, following the camera target,
matching the scene's vertical directional light — into a sampleable depth FBO (raylib shadowmap
pattern, texture unit 15 so per-mesh material binds don't clobber it). The depth pass draws all
casters (level mesh + `Object3DRenderer::renderDepth` for `castsShadow` instances incl. the
`shadowOnly` fan whose mesh is drawn *only* here + `UnitManager::renderAll`). `lighting.fs`
`shadowFactor()` projects each fragment into light space and does 3×3 PCF with a small depth bias,
attenuating diffuse + specular only (ambient is left, so shadows aren't pure black). `useShadows`
defaults 0 (safe no-op when the map isn't bound). Wired in `game_render_gameplay` before the main
`BeginMode3D`. Shader *switching* per drawtype is not needed yet — one lit shader + the shadow term
covers the current defs; revisit if a def needs a genuinely different pipeline. Tuning knobs: FBO
resolution (`build(2048)`), light `extent`/`height`, and `bias` (`apply`, 0.0015).

**Phase 4 — Glow materials (DONE).** `lighting.fs` gained an `emissive` uniform (float strength,
default 0 = safe no-op): a glow surface adds its material colour back as self-illumination **after**
the shadow and lights-out steps, so a glowing object stays bright in shadow and when the level's
lights go out. `Object3DRenderer::render` sets the uniform per-draw — `glowIntensity` for a `Glow`
def, 0 otherwise — and **resets it to 0 after the loop** so the shared scene shader never glows the
tiles/units drawn elsewhere in the frame (uniforms persist across draws in one program). Rather than
a second shader program, this reuses the one lit shader with a branch — the deferred "shader
switching" from Phase 3 still isn't needed. Data-driven via a new `glowIntensity` def field (default
1.0). Alert lights + lift tops on deck 10 now read as self-lit beacons. (`consoleglow` for console
screens is a separate renderer and out of scope here.)

*Dynamic alert glow (uber `gs_alert`).* A glow def can set `glowSource: alert` (vs the default
`static`): its glow colour then comes from the ship's alert **band** — green→yellow→amber→red
(`scoring.h alert_band_color`, matching uber's colours) — pulsed by a sine whose **rate rises with
the alert level** (`alert_pulse_hz`, ~0.4 Hz calm → ~2 Hz at red; uber used a fixed 0.4 Hz, faster-
with-alert is our addition). The game integrates the pulse into `alertGlowPhase` (so a rate change
never jumps the wave), computes `bandColour × pulse`, and feeds it to `Object3DRenderer::setAlertGlow`
each frame; the shader picks it via `emissiveTint = 1` (a flag, not "is the colour black?", so the
pulse trough can legitimately reach black). The alert light (renderIndex 42) uses it; it glows green
when calm. uber's other glow source, `gs_wave` (self-coloured sine pulse + a **scrolling glow-mask
texture**), is **deferred** — the real driver is unit type 21 (`852.gltf`, scrolling blue-tek skin);
the lift top is a scroll-only bonus. Full plan + difficulties in [glowscroll.md](glowscroll.md).

**Phase 5 — Decals (deferred).** Runtime `dirty_t`-style projected sprites (explosion scorch, drips)
and, separately, the map-placed `Feature` wall-detail layer (own index table) — lower priority.

**Out of scope:** MD2 model format (the single Organic).

## Verification (per phase)

Convert models; re-export bundles (`incremental_viewer --export-all …`); run
`topdown_game --renderer 3d --deck <n>` on a deck with objects (e.g. tanks on deck 0), screenshot via
the established framing-debug flow; confirm models appear at the mapped positions/orientations; keep
the test suite green. Phase 2 (done): on deck 10 (four tanks, health 50) shoot a tank — it explodes
(shared blast + sparks), its footprint drops (you can walk through where it stood), and it stops
rendering/casting a shadow; the blast damages nearby droids. Phase 3 (done):
`--deck 10` (four ceiling fans) — the fan blades cast crisp cross shadows straight down onto the
floor, and units/tanks passing under a caster are shadowed. Phase 4 (done): on `--deck 10` the alert
light + lift top read as self-lit glowing beacons against the scene (and stay lit in shadow), while
neighbouring tiles/units render normally — no emissive leak. 152 tests green.
