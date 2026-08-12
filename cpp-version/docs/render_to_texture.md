# Render-to-texture: projected decals + distortion (deferred)

Two future effects both need the scene rendered into an offscreen **RenderTexture** first, so they
should be planned (and probably built) together rather than each growing its own path:

1. **Projected decals** — floor marks (and future scorch/blood) that **conform to walls, trim, and
   floor-height changes** instead of being flat quads on the `Y≈0` plane (today's decals; see
   [decals.md](decals.md)).
2. **Distortion** — a screen-space warp/refraction post-effect (explosion shockwaves, heat haze),
   which uber did with the same FBO.

Status: **not started.** This is a self-contained mini-project (~1–2 sessions), captured here so it
can be picked up without re-deriving it.

## Why they share a foundation

The game is a **forward renderer** that draws straight to the backbuffer inside `BeginMode3D`
(`game_render_gameplay`, `src/game.cpp`). The only depth texture that exists is the **shadow map**
(`shared/rendering/shadow_map.*`), which is rendered from the *light's* POV — wrong projection for
screen effects. Neither the camera-space **colour** nor **depth** is available as a sampleable
texture mid-frame.

Both effects need exactly that offscreen target:

| Effect | Needs from the RenderTexture |
|---|---|
| Projected decals | the **depth** attachment (reconstruct each pixel's world position, test it against a decal box, blend the decal on) |
| Distortion | the **colour** attachment (re-sample it with warped UVs) |

raylib's `RenderTexture2D` carries both a colour and a depth texture, so **one** "render the opaque
scene to an RT, then composite to the screen" change unlocks both — and any later post-FX (bloom,
colour grading, scanlines).

## How uber did it (reference)

- Scene is rendered into an FBO (`renderer_t::mUseFBO` / `mFBOFrame`, `beginRender(pFBO)` /
  `endRender(...)`, `uber/source/uberdroid/render.cpp:1191-1210`, `framebufferObject.cpp`,
  `baseApp.cpp:165-176`). Also used for shadows.
- **Distortion** — `renderer_t::endRender(bool inApplyDistortion, float inDistortionPhase)`
  (`render.cpp:1456-1568`): composites the FBO to the screen, and when `inApplyDistortion` draws a
  full-screen quad with the **`dt_distort`** shader (`shaders/distort.vert` / `distort.frag`, wired
  at `render.cpp:1391-1397`). It binds the scene texture + a distortion **normal map**
  (`textures/bump/norm_distort_1_256.bmp`) on unit 1 and animates the TU1 texcoords by a radius
  `1 - 0.5·sin(phase)` (`render.cpp:1519-1568`) — a ripple that expands with the effect's phase.
  Used for explosion shockwaves.
- The `distort` **drawtype** also exists per-material (`default.cpp:9` drawtype table), i.e. uber
  could tag specific surfaces as distorting, not only the full-screen pass.

## Sketch for the cpp port

1. **Scene RT.** Create a `RenderTexture2D` sized to the window (recreate on resize). In
   `game_render_gameplay`, wrap the opaque 3D pass (floor + objects + units, keeping the existing
   shadow depth pass before it) in `BeginTextureMode(sceneRT)` / `EndTextureMode()`. Then draw the
   RT's colour to the screen (a full-screen textured quad / `DrawTextureRec` flipped) — this is the
   composite. Care points: the existing **glass** (env-mapped, depth-write-off) and **additive
   effect/particle/beam** passes and the **HUD** must stay in the right order relative to the
   composite (HUD after; transparent world FX likely still inside the RT).
2. **Projected decals.** Replace the flat-quad decal pass with a projection pass that, per decal,
   draws its footprint and in the fragment shader reads `sceneRT` depth → reconstructs world position
   (needs the inverse view-projection) → rejects fragments outside the decal's oriented box → blends
   the decal texture with the mark's alpha. This makes marks climb trim/walls and follow ramps. Keep
   the `DecalManager` data model as-is (`shared/effects/decal_manager.*`) — only the *draw* changes.
   Bonus: fixes decals on floor-height changes, the more visible win for this top-down camera.
3. **Distortion.** Port uber's effect: a distort shader that samples `sceneRT` colour with UVs
   offset by a distortion normal map (bring over `norm_distort_1_256`), animated by a per-effect
   phase. Drive it from `EffectManager` explosions (a shockwave that expands as the blast plays) —
   either full-screen tinted by distance to the blast, or a localized quad over the blast.

## Assessment / recommendation

- **Cost:** moderate — restructuring the opaque pass through an RT is the bulk; the two shaders are
  small. The risk is pass ordering (shadows / glass / additive FX / HUD) and resize handling.
- **Payoff:** projected decals are a *modest* visual gain under the near-top-down camera (walls read
  thin; flat marks already clip cleanly at wall bases) — the floor-height conforming is the better
  part. Distortion is good **combat juice** (explosion shockwaves) and, with the RT in place, opens
  the door to further post-FX cheaply.
- **Recommendation:** do them **together** when picked up, because the RT + composite is the shared
  90%. Until then the flat-floor decals stay the uber-equivalent baseline.
