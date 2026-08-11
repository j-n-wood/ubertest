# Scrolling glow (uber `gs_wave`) — deferred port plan

**Status: DEFERRED.** Low-value / high-specificity. Captured here so it can be picked up later
without re-deriving the uber behaviour. The related, higher-value pieces are already done:
`gs_alert` (pulsing alert-band glow) and the flat `emissive` glow term — see
[scenery_entities.md](scenery_entities.md) Phase 4. This doc covers only the remaining `gs_wave`
flavour: a **scrolling additive glow-mask texture with a sine intensity pulse**.

## What it is (decoded from uber)

Source: `uber/source/uberdroid/render_control.cpp` (glow params), `render.cpp` (uniform upload),
`uber/uberdroid/shaders/diffuseEnvmapGlowShadow.frag`, `data/renderobjects.txt` (per-object config).

The `diffuseglow` shader adds, on top of the lit + env-mapped surface:

```glsl
glowcolour = texture2D(glow_texture, texture_coordinate_glow + scroll) * glow_colour;
gl_FragColor = colour + envcolour * shade + glowcolour;   // additive, unaffected by shadow
```

- `glow_texture` = uber `EFFECTTEXTURE2` — a glow **mask** (only its bright texels glow).
- `scroll` = `glowScrollDirection * glowScrollWave.value(t)` — a UV offset that ramps continuously
  (waveform 2 = `wf_ramp`), so the mask scrolls across the surface (moving energy band).
- `glow_colour` = `glowColour * glowColourWave.value(t)` — base colour times a sine pulse
  (waveform 1 = `wf_sin`), so the intensity throbs.

`wave_t::value(t) = zero + amp * waveform(speed * t)` (see `uber/.../wave.cpp`). `wf_sin` period ≈
`1/speed` seconds; `wf_ramp` is a 0→1 sawtooth (continuous scroll).

### The two placed users (from `renderobjects.txt`)

| object | idx | in ship1 | config |
|---|---|---|---|
| **852_model** = **unit type 21** (`droid_class_21`, `models/852.gltf`, single section) | 123 | yes (as a unit) | env `blue-glass` (**done**) + glow `mtltekfloor_fx.jpg`, `glowscroll 1 0 0`, `glowcolourwave 1 0.5 0.25 0.5` (pulse), `glowscrollwave 2 1.0 0.12 0.0` (scroll) |
| **lift_top** | 49 | yes (scenery) | glow `glow_yellow_128`, `glowscroll 1 0 0`, `glowscrollwave 2 1.0 0.4 0.0` — **scroll only, no pulse** |
| 852-family decor `101_transmat` | 124 | no | ignore per decision |

So the only reason to do this is **unit type 21** (the scrolling blue-tek skin); the lift-top yellow
scroll is a free bonus once the channel exists.

## Why it's cheap-ish to bolt on (existing scaffolding)

- **Env-map half already works on class 21**: `852.gltf` Material_0 carries `envTexture:
  blue-glass.jpg` extras and renders via the shader's env term. Nothing to do for the env part.
- **The per-material toggle pattern already exists** and already spans **units + objects**:
  `UnitManager::drawModelWithEnv` (`shared/units/unit_manager.cpp`) iterates section materials,
  checks the `MATERIAL_MAP_METALNESS` slot, toggles `useEnvMap`/`envIntensity` per material, and
  restores after the loop. The glow channel mirrors this exactly.
- **`MATERIAL_MAP_EMISSION` (texture unit 5) is free** and semantically correct for the glow mask.
  The Phase-4 `emissive` scalar uses no texture, so there's no clash. (Env = texture1/METALNESS,
  shadow map = slot 15, diffuse = 0, normal = 2.)
- The extras loader + test can be copied from `shared/rendering/env_map.{h,cpp}` +
  `tests/env_map_test.cpp`.

## Implementation steps (when resumed)

1. **Assets** — copy `mtltekfloor_fx.jpg` and `glow_yellow_128.bmp` from
   `uber/uberdroid/textures/` into `assets/textures/`; set **REPEAT** wrap (`SetTextureWrap`) so the
   scroll tiles.
2. **Shader** (`assets/shaders/lighting.fs`) — add `uniform sampler2D texture5; uniform int
   useGlowTex; uniform vec2 glowScroll; uniform vec3 glowTexColor;` and, after the shadow /
   lights-out steps (alongside the existing `emissive` + env terms):
   `if (useGlowTex == 1) result += texture(texture5, fragTexCoord + glowScroll).rgb * glowTexColor;`
   Register `texture5` as `SHADER_LOC_MAP_EMISSION` in `sceneRendererInit`.
3. **Extras + loader** (mirror `env_map.{h,cpp}`) — parse gltf material-extras keys `glowTexture`,
   `glowScroll` (xy), `glowScrollSpeed`, `glowColourWave` ([amp, speed, zero]); bind the texture
   into the `MATERIAL_MAP_EMISSION` slot at load.
4. **Draw wiring** (mirror `drawModelWithEnv`, and add to `Object3DRenderer::render`) — per material
   with a glow texture: compute `scrollOffset = dir * ramp(speed * t)` and
   `pulse = zero + amp * sin(2*PI * speed * t)` from an **accumulated** clock, set `useGlowTex = 1`,
   `glowScroll`, `glowTexColor = glowColour * pulse`; else `useGlowTex = 0`. Restore `0` after loop.
5. **Author extras** into `852.gltf` (Material_0: `mtltekfloor_fx`, scroll `[1,0]`, speed 0.12,
   pulse `[0.5,0.25,0.5]`) and `lifttop.gltf` (`glow_yellow`, scroll speed 0.4, **no** pulse).
6. **Tests** — extras parser (copy `env_map_test`) + the pure scroll/pulse math.
7. **Verify** — screenshot unit type 21 (scrolling blue-tek glow, pulsing) and lift_top (scrolling
   yellow, steady).

## Difficulties / risks

- **Main new design decision: per-material wave params have nowhere to live in raylib's
  `Material`.** Env map got away with one scalar (`map.value`) + the bound texture; glow needs ~6
  floats per material (scroll dir/speed, pulse amp/speed/zero). Hold a **side table keyed by (model,
  materialIndex)** parsed from the extras at load, owned by the renderer. (Alternative: pack into
  unused `map.color`/`map.value` fields — hacky, not recommended.)
- **Animated uniforms per frame.** Env uniforms are static; glow needs a monotonic clock, and the
  scroll offset must **accumulate** (not `speed * t`) so a rate change never jumps the phase — same
  trick as the alert-glow phase.
- **Asset provenance.** The env extras in `852.gltf` were **hand-authored** — no converter writes
  them, and the model converter doesn't read `renderobjects.txt`. So glow extras are likewise
  hand-authored (or a tiny injector script); the params can't be auto-derived from the `.asc`.
- **UV / tiling.** Reuse the diffuse UV (`fragTexCoord`) + scroll offset. If 852 used a distinct
  glow UV in uber, tiling density may need a visual tweak.
- **Three independent additive terms** (Phase-4 `emissive` scalar, env map, new glow texture) must
  stay separately gated so they don't stomp each other — 852 uses env + glow together.

## Effort

Moderate: one shader term, one loader (copy `env_map`), one draw-loop extension (copy
`drawModelWithEnv`), the per-material side-table, plus authoring two gltfs and copying two textures.
The side-table is the only genuinely new piece.
