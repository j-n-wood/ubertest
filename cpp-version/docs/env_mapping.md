# Environment mapping (old-school spherical / matcap)

Several legacy droid/scenery models used the uberdroid engine's `DRAWTYPE ENVMAP` /
`EFFECTTEXTURE` system: a **view-relative** reflection sampled from a flat texture using the
surface normal. It is a cheap fake reflection (a "matcap"), not a real cubemap.

This is **not a standard glTF feature** — an env map is *context* (camera-relative), not part of
the model. When the `.asc` models were converted to glTF the converter had no concept of it, so it
baked the env texture in as the material's **base colour**. That is the bug you see on e.g.
**type 2** (the disk): its only texture was `centercongrey.jpg`, which the legacy data
(`uber/uberdroid/data/renderobjects.txt` + `textures.txt`) shows is the generic *environment*
texture (index 4), never a diffuse. So the disk rendered the flat grey console env painted on.

## How it works now

The env texture is carried in each material's glTF **`extras`** and applied by our own shader —
Raylib's glTF loader ignores `extras`, so nothing standard is disturbed.

### Data: `extras` on a glTF material

```json
"materials": [
  {
    "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 }, ... },
    "extras": {
      "envTexture":   "textures/envmapgold.png",   // path, relative to the .gltf's directory
      "envColor":     [0.8, 0.4, 0.2, 1.0],         // optional; legacy SPECULARITY modulation (RGBA), default white
      "envIntensity": 1.0                           // optional; additive strength, default 1.0
    }
  }
]
```

`baseColorTexture` stays the **real diffuse** (for the disk: `copper1.jpg`). The env texture is a
separate, view-relative layer.

### Load: `shared/rendering/env_map.{h,cpp}`

- `envMapReadExtras(gltfPath)` — pure JSON read (no GPU), returns one `EnvMapEntry` per material
  that declares an `envTexture`. Unit-tested in `tests/env_map_test.cpp`.
- `envMapApplyExtras(model, gltfPath)` — loads each env texture and binds it into the material's
  **`MATERIAL_MAP_METALNESS`** slot (which Raylib samples as `texture1`), writes `envColor` to that
  map's colour (surfaced to the shader as `colSpecular`) and `envIntensity` to the map's scalar
  `value`. Called right after `LoadModel` in both `ModelCache::get` (shared static models) and the
  per-instance load path in `UnitManager`. The textures are owned by the `Model` and freed by its
  `UnloadModel`; a fresh copy is loaded per material so shared files never double-free.

The `MATERIAL_MAP_METALNESS` slot is otherwise unused by the lighting shader, so it is free to
repurpose as the env channel.

### Shader: `assets/shaders/lighting.fs`

Gated by a `useEnvMap` uniform (set per-material during the draw loop, below):

```glsl
vec3 nEye  = normalize((matView * vec4(normal, 0.0)).xyz); // world normal -> eye space
vec2 envUV = nEye.xy * 0.5 + 0.5;                          // (0.5,0.5) = facing camera
vec4 e     = texture(texture1, envUV) * colSpecular;       // legacy SPECULARITY modulation
result    += envIntensity * e.a * e.rgb;                    // additive on top of the lit surface
```

The eye-space normal's XY becomes the UV (normals toward the camera map to the texture centre, and
are scaled by 0.5), giving the classic spherical reflection. `colSpecular` (the legacy
`SPECULARITY`) modulates all four channels before the additive blend. `matView` is provided
automatically by Raylib (`SHADER_LOC_MATRIX_VIEW`).

### Draw: `UnitManager::drawModelWithEnv`

Because one shared shader draws every material, `useEnvMap` / `envIntensity` are **per-material**
state. `renderSection`/`renderDebris` therefore draw meshes one at a time (a manual equivalent of
`DrawModelEx`): a material is env-mapped iff a texture is bound in its metalness slot, in which case
`useEnvMap=1` and `envIntensity` are pushed before `DrawMesh`. The flag is reset to `0` afterwards
so tiles/level geometry (which share the shader) never pick up a stray env term — `SceneRenderer`
also initialises it to `0`.

## Blend model

Additive: `result += envIntensity * SPECULARITY * envSample`. This reads as a reflective / chrome /
glow highlight layered on the normally-lit model, and covers both the shiny reflective droids and
the additive glow effect-textures. Turn `envIntensity` down for a subtle sheen, up for full chrome.

## Coverage: all unit models migrated

Every glTF used by a `droid_class_*.json` unit now carries env `extras` on the materials that had a
legacy `EFFECTTEXTURE` — **66 materials across 43 models**. The env texture is the resolved
`EFFECTTEXTURE`, `envColor` is that entry's `SPECULARITY`, and `envIntensity` is `1.0`. Where the
legacy env was the generic grey (`centercongrey`, index 4) the effect is a subtle sheen; the
distinctive ones stand out — e.g. the disk's gold (`envmapgold`), the reflective chrome droids
(`metal_grey_1_256` on probe/gonk/mouse/r5d2), and the energy-scroll look (`beam_2_64` on the
crew/rect torsos and cylinder heads).

Notable exemplars:

| Model | glTF | Env texture(s) (`envColor`) | Legacy entry |
|-------|------|-----------------------------|--------------|
| Disk (type 2) | `disk.gltf` | `envmapgold` (0.8,0.4,0.2) | **testdisk** (base fixed to `copper1`; converter had baked the env in) |
| Dalek body | `dalek_body.gltf` | `centercongrey`, `centercongrey`, `dalek_thermfin` (0.3) | **dalekbody** |
| Probe / Gonk / Mouse / R5D2 | `probe/gonk/mouse/r5d2.gltf` | `metal_grey_1_256` (chrome) | `DRAWTYPE ENVMAP` |
| Crew / Rect torso, cylinder heads | `crew_torso/rect_torso/cylinder_head_small.gltf` | `beam_2_64`, `envmapgold` | `DRAWTYPE ENVMAP` |

The mapping was generated from `renderobjects.txt` + `textures.txt` (material *i* ↔ legacy slot *i*,
matching the `.asc` material order), so it's faithful to the original per-slot assignments. The
migration only edits `materials[].extras` — geometry, buffers and base-colour images are untouched.

### Still to do (scenery, not units)

Scenery/prop models (doors, consoles, generators, the `letters/*` ENVMAP models, ship parts) are not
part of the unit set and were left alone. To migrate one, resolve its `renderobjects.txt` entry the
same way and add the `extras` block — no code changes are needed, the mechanism handles any model
whose materials carry the extras.

### Base-texture fixes made alongside

Two unit models had the converter point their **base** diffuse at the wrong/missing file; corrected
to the `renderobjects.txt` `TEXTURE 0`: `999base.gltf` → `drttrimmulti.jpg` (was `n_crate.jpg`) and
`lasergun.gltf` → `lasergun.png` (was `G_SKIN.jpg`; converted from `lasergun.bmp`). Every base and
env texture referenced by a model glTF now resolves to a shipped file.
