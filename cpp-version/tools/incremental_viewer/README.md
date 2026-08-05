# Incremental Scene Viewer

Interactive tool for **loading**, **diagnosing/validating**, **editing**, and **exporting** the
original uberdroid procedural level geometry. It reconstructs walls from the legacy geometry
generator, lets you fix data issues live (a raygui link inspector), saves edits to a JSON copy set
(originals untouched), and exports GLTF + Box2D collision for the engine. A class-14 droid can be
spawned as an on-screen size reference.

> **Resuming?** Jump to [Current state & intentions](#current-state--intentions) at the bottom.

---

## Pipeline

1. **Parse** — a deck source (`xmapfile{N}.txt`) is parsed by the shared scene-convert code, which
   pulls in the procedural geometry XML (`lvl{n}section{m}.xml`) → `PathGeometry`
   (nodes / links / profiles / areas, with Bézier control points) plus the tile grid.
   The parsed domain is written to `<output>/domain.json` and reloaded (same path the game uses).
2. **Build** (`viewerRebuildMeshes`) — from the in-memory domain:
   - **Tiles** — `shared/rendering/tile_mesh` batches tiles by texture.
   - **Floors** — `shared/rendering/geometry_mesh` ear-clips each area boundary (Bézier-subdivided).
   - **Walls** — `shared/rendering/wall_mesh` sweeps each link's cross-section profile (below).
3. **Validate** (`V`) — per-deck report: counts, degenerate/zero-area triangles, non-finite verts,
   Box2D-invalid collision polys, missing textures, finite bounds.
4. **Export** (`X`) — GLTF + manifest + collision per deck (below).

### Coordinate convention (important)

`gameToRenderCoords` (`shared/rendering/tile_mesh.h`) maps game → render:
`renderX = gameX·scale`, `renderY = gameZ·scale` (up), **`renderZ = −gameY·scale`**. The game Y
(forward) axis is **negated** — the source data Y is inverted. This is a reflection, so the winding
of **all** generated geometry (tiles, floors, walls, caps) is chosen to face correctly under it.
`scale` defaults to `0.0254` (inches → metres). Nodes/control points are stored render-space after
the JSON round-trip; only a profile's local `{lateral, height}` offsets are raw game units.

---

## Build & run

```bash
cmake -B build && cmake --build build --target incremental_viewer
cd build/tools/incremental_viewer      # run here: build copies assets/{shaders,textures,models} next to the binary

./incremental_viewer \
  --source        /ABS/PATH/uber/uberdroid/ship1/xmapfile7.txt \
  -t              /ABS/PATH/uber/uberdroid/data/tiles.txt \
  -x              /ABS/PATH/uber/uberdroid/data/textures.txt \
  --textures-base /ABS/PATH/uber/uberdroid/ \
  --materials     /ABS/PATH/uber/uberdroid/data/materials.xml \
  -o              /ABS/PATH/cpp-version/viewer_output \
  --save-dir      /ABS/PATH/cpp-version/viewer_output/edited
```

`--source` sets the **starting** deck; the directory is scanned so all decks cycle with `[` / `]`.
`--materials` is required for wall profiles (default `<srcDir>/../data/materials.xml`).

### CLI options

| Option | Meaning |
|--------|---------|
| `--source <xmapfile{N}.txt>` | Starting deck (also scans its directory for all decks) |
| `-o, --output <dir>` | Working output (`domain.json`, `export/…`). Default `output` |
| `-t, --tiles <path>` | `tiles.txt` (archetile expansion) |
| `-x, --textures <path>` | `textures.txt` (texture index table) |
| `--textures-base <dir>` | Base dir for resolving texture files |
| `--materials <path>` | `materials.xml` — profile/material definitions for walls |
| `--save-dir <dir>` | Edited-deck JSON folder. Default `<output>/edited` |
| `-s, --scale <f>` | Scale override (default 0.0254) |
| `--no-caps` / `--no-miter` | Disable wall end caps / corner miters |
| `--no-reference` / `--no-textures` | Skip the reference model / textures |
| `--export-all <dir>` | Headless: load + validate + export every deck, then exit |
| `--export-split <dir>` | Headless: split export (one file per shape) every deck |

---

## Controls

**Camera:** `WASD`+`QE` move · `Shift` faster · wheel zoom · `T`/`I`/`P` top-down/iso/perspective ·
`R` reset · `0`–`5` debug shader modes.

**Display:** `F1` grid · `F2` reference model · `F3` tiles · `F4` geometry (floors+walls) ·
`F5` wireframe · `F6` tile indices · `F7` backface culling (default **on**) · `H` help.

**Decks & diagnosis:**

| Key | Action |
|-----|--------|
| `[` / `]` | Previous / next deck |
| `N` | Node markers (spheres, depth-test off) + id labels — identify the failing path |
| `J` | Dump per-link profile assignment + trim side to the console |
| `V` | Validity report panel |
| `U` | Class-14 reference droid (real `UnitManager`, at origin) |
| `K` / `M` | Toggle wall **caps** / **miter** (rebuilds the deck) |
| `F9` | Reload the current deck **from source XML** (after editing the XML) |

**Edit (link inspector) — `L`:** a raygui panel listing every link:
- `L{id} {start}>{finish}`, profile checkboxes `0 1 2 3 4`, **Rev** (reverse start/finish), **Del**.
- Toolbar: **Save** · **Save All** · **Revert** · `s`/`f` node boxes + **Add link**.
- Every edit rebuilds geometry immediately (full rebuild — inefficient but instant).

**Save / export:** `F10` save current deck to JSON · `X` export combined · `Shift+X` export split.

---

## Edited-copy workflow (JSON)

The project's "new" data is JSON, so edits are saved as JSON, not XML:

- **F10 / inspector Save** → writes the current deck to `<save-dir>/level_<n>.json` (full domain via
  `saveDomainToFile`). **Originals (XML) are never touched.**
- **Loads prefer the saved JSON** for a deck if present (else the original XML). The HUD shows
  `Source: EDITED json` (green) vs `original XML`.
- **Revert** (inspector) → deletes the current deck's saved edit and reloads the original XML.
  Only ever deletes inside `--save-dir`.
- **Save All** (inspector) → saves current in-memory edits, then converts every deck lacking a copy
  from its XML → JSON, producing a complete self-contained JSON ship in `--save-dir` (existing edits
  preserved; doesn't disturb the loaded deck).

Inspector edits are in-memory/transient until saved — cycling or `F9` before saving discards them.

---

## Wall geometry (ported generator)

`shared/rendering/wall_mesh` ports the legacy uberdroid generator (`uber/source/uberdroid/
geometryGen.cpp`: `loadStandard`, `buildLink`). Each `PathLink`'s cross-section **profile** is swept
along the link path (straight, or subdivided quadratic Bézier via the control point).

- **Profiles** — resolved from `materials.xml`: a link uses its explicit `<Profile>` children; else
  the deck's **default set** (the ids in `<Profiles>`, typically `{0,1,2}`) **unless** it has
  `defaultProfiles="0"`. Standard shapes (ported from `loadStandard`):
  `0` Default Wall (curved arch), `1` Border-Left, `2` Border-Right, `3`/`4` curved/glass walls.
  **Trim side follows winding:** Border-Left = profile `1` on the **LEFT of start→finish**;
  Border-Right = `2` on the right. Reverse a link (or swap profile 1↔2) to flip the trim side.
- **Caps** — open (dead-end) link ends are closed by triangulating the **actual profile outline**
  (`triangulate2D`), so cap edges align with the swept sides; each cap triangle is wound to face
  **outward** (away from the wall body) via its geometric normal.
- **Miter joins** — at every junction, each **side** (+/- lateral) mitres with its **angular
  neighbour** using the standard 2D stroke miter `(n1+n2)/(1+n1·n2)`, computed from the **forward**
  tangent so the section never twists. Works for degree-2 corners **and degree-3+ T-junctions**;
  degree-2 reduces to the symmetric case (left == right). Near-degenerate turns keep a square end.
- **Duplicate links** — exact-duplicate wall links (same endpoints + profiles + control) are
  **deduped** (e.g. lvl7 links 23 & 43 are both `42→43`); the redundant one would otherwise draw a
  coincident wall and break the miter/border at that node.

---

## Export outputs (`level_<n>/`)

- **`level_<n>.gltf`** — one mesh per procedural shape (`kind: floor`/`wall`) + one per tile batch
  (`kind: tile`), materials grouped by texture. raylib loads each glTF primitive as its own
  `model.meshes[i]`, so the engine can cull per-mesh (draw a subset + `GetMeshBoundingBox`).
- **`level_<n>.manifest.json`** — per-mesh `{ index, kind, id, material, textureIndex, min, max }`.
  raylib drops node names on load, so this restores stable identity + bounds for culling.
- **`level_<n>.collision.json`** — Box2D-ready: CCW convex `polygons` (≤8 verts) + `chains`
  (`loop` flag), 2D in the game plane (X, Y-forward), pre-scaled to match the mesh.
- **Split export** (`Shift+X` / `--export-split`) — one `.gltf` per shape named `<kind>_<id>.gltf`
  + `_index.json` (per-file bounds + max magnitude) to isolate a bad section.

---

## Key source files

| File | Role |
|------|------|
| `main.cpp` | CLI, window, main loop, initial deck load |
| `viewer.{h,cpp}` | State, input, 3D render, HUD/overlay, `viewerRebuildMeshes`, reload |
| `level_manager.cpp` | Deck cycling, validity, profile dump, **save / revert / save-all** |
| `inspector.cpp` | raygui link inspector (edit/add/remove/reverse, save buttons) |
| `level_export.cpp` | GLTF + manifest + collision export (combined & split) |
| `unit_ref.cpp` | Class-14 reference droid via `UnitManager` + tiny Box2D world |
| `shared/rendering/wall_mesh.{h,cpp}` | Profile table + swept walls, caps, miter, dedupe |
| `shared/rendering/geometry_mesh.cpp` | Floor tessellation (ear-clip, up-facing winding) |
| `shared/scene_convert/*` | XML parse, `PathGeometry`, JSON round-trip (`useDefaultProfiles`) |

---

## Current state & intentions

**Working:** parse → build (tiles/floors/walls) → validate → live-edit (inspector) → save JSON →
export. All 16 decks parse/build/export. Coordinate reflection + winding are consistent across
tiles/floors/walls/caps. Miter handles degree-2 and degree-3. Ear-clip fixed (no fan fallback).
Duplicate links deduped. 150/150 unit tests pass.

**Known data characteristic — one-sided trim.** Perimeter links carry explicit `[0,1]` (arch +
Border-Left only). The trim lands on the *left of start→finish*; for some runs that is the exterior
side, so it reads as "missing" from the interior (e.g. lvl7 nodes 18–19–20–21). **Fix per link in
the inspector** (add profile `2`, or `Rev`), then **Save**. Confirmed this repairs the visual.

**Deferred / next:**
- **Swept-solid collision** — collision is still the legacy 2D link chains, not the 3D swept wall
  solids. (`generateCollideria`-style quads exist in the original.)
- **Texture copy in export** — writes `.jpg`/`.png` URIs but some source textures are `.bmp`/`.tga`;
  exports may render untextured until textures are converted/copied with matching URIs.
- **JSON as the new format** — the edited JSON already round-trips the full domain; intention is a
  richer JSON loader that detects new structures/features by extension (walls, profiles, edits) so
  the JSON set becomes the authoritative "new data".
- **Inspector polish** — `Add link` uses click-to-edit value boxes (could be spinners); no undo yet.

**Verification note:** GUI interactions (inspector clicks, marker overlay, mid-frame rebuild) need a
real windowed launch to eyeball — headless checks cover parse/build/export/save-load round-trips.
