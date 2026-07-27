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
