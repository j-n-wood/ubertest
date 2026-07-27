# Texture ownership (TextureManager)

All of the game's GPU textures are owned by a single **`TextureManager`**
([`shared/rendering/texture_manager.h`](../shared/rendering/texture_manager.h)), keyed by the
`TextureId` enum, instead of being scattered raw `Texture2D` handles freed with per-texture
`UnloadTexture` boilerplate.

## Why

- One teardown point (`unloadAll()`), so the raylib rule "every `UnloadTexture` must run while
  the GL context is alive (before `CloseWindow`)" is satisfied in exactly one place.
- Resource lifetime is decoupled from `Game` domain logic — `Game` no longer holds texture
  handles.
- An enum-indexed array is extensible and is a natural home for frame-based animation later
  (a contiguous run of `TEX_*` frames).

## Lifetime (the important part)

The manager is a **scoped instance**, deliberately *not* a static-duration singleton (whose
destructor would run at process exit, after `CloseWindow`, unloading into a dead GL context).
In [`src/main.cpp`](../src/main.cpp):

```
InitWindow(...);
auto textures = std::make_unique<TextureManager>();   // registers gTextures()
Game game{}; game_init(&game, ...);                    // loads via gTextures()
... loop ...
game_destroy(&game);
textures.reset();                                      // unloadAll() BEFORE CloseWindow
CloseWindow();
```

`gTextures()` returns the one live instance (a file-scope pointer set in the ctor, cleared in
the dtor). It is valid only while that instance exists.

## Slots (`TextureId`)

`TEX_TILE_ATLAS`, `TEX_TILE_BUMP` (tileset diffuse + optional bump, from `game_load_levels`),
`TEX_FLARE`, `TEX_BLASTER_BLOB` (projectile sprites, see [weapons.md](weapons.md)),
`TEX_SHIP_MAP`, `TEX_SHIP_MAP_LIT` (ship-view images). All are loaded once at startup in
`game_init` and are app-lifetime; the ship images in particular are small, so they are loaded
once and retained rather than reloaded on each ship-view open.

## API

`set(id, Texture2D)` adopts a handle (unloading any previous occupant); `loadFile(id, path)`
`LoadTexture`s into a slot; `get(id)` returns the handle for raylib draw calls; `loaded(id)`
tests a slot; `unloadAll()` frees everything (also called by the destructor).

## Not (yet) consolidated

`SceneRenderer::defaultNormalMap` and per-model textures (owned by `ModelCache` /
`SectionInstance`, freed via `UnloadModel`) remain under their own teardown. They could move
here later if a single policy is wanted.

## Dynamic textures / GLTF & future direction

The manager is intentionally **enum-indexed and preload-only** today: every slot is known at
compile time and loaded once at startup. That covers all of the game's own textures. Two things
are deliberately left out, with a clear evolution path.

### Model (GLTF) textures stay with the model

Droid/unit GLTFs are loaded by raylib `LoadModel(path)`, which resolves each model's external
image URIs (e.g. `assets/models/textures/chrome1.jpg`) and loads them straight into
`model.materials[m].maps[k].texture`. Those textures are owned and freed by `UnloadModel`, and
that teardown is already RAII: `ModelCache::destroy` (shared static models) and
`SectionInstance::~SectionInstance` gated by `ownsModel` (per-instance/animated), plus
`UnitManager::clearDebris` for debris. **So model textures are not — and for now should not be —
routed through the `TextureManager`.** `ModelCache` dedups whole models by path, but not
textures across models, so a shared image (`chrome1.jpg` is referenced by ~17 GLTFs) can be
resident several times. These are small JPEGs, so the VRAM cost is negligible.

Interning them would need a **custom GLTF loader**: raylib gives no image-load hook and discards
the source path, so dedup can't happen after `LoadModel`. You'd resolve each image URI through
the manager and assign a shared handle into the material — and then, because `UnloadModel` frees
material textures, neutralize every model's material textures (`= {0}`) before unloading to
avoid a double-free. High cost, real hazard, tiny payoff — deferred unless profiling says
otherwise.

### The handle-by-path evolution (when a real caller appears)

If/when textures must be referenced whose identity isn't known at compile time (runtime or
moddable assets, a decals / floor-marks system), grow the manager into a single handle store
rather than adding a second manager:

- `using TexHandle = int;` — a handle is just an index into one contiguous store.
- The current `TextureId` enum values become the fixed handles `[0, TEX_COUNT)` (named
  preloads); dynamic textures append at `>= TEX_COUNT`.
- Add `TexHandle acquire(const std::string& path)` — normalize the path, dedup via an
  `unordered_map<string, TexHandle>`, load on miss, return the handle. `get(TexHandle)` then
  serves both enum ids and dynamic handles uniformly.
- Frame-based animation is a small struct over a contiguous run of handles.

This keeps one owner and one teardown point while adding "ask by path, get a handle, deduped".
It is **not built yet** — there is no caller, and speculative infrastructure isn't worth the
complexity until there is one.
