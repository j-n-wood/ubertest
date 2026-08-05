# Incremental Scene Viewer

Loads the original procedural level data, lets you **check each deck's validity** interactively, and
**exports** GLTF-suitable geometry + Box2D collision for use in the engine. Also spawns a class-14
droid as an on-screen size reference.

## Pipeline

1. **Parse** — a deck source (`xmapfile{N}.txt`) is parsed via the shared scene-convert code, which
   pulls in the procedural geometry XML (`lvl{n}section{m}.xml`) → `PathGeometry` (nodes/links/areas,
   Bézier boundaries) and the tile grid.
2. **Build** — `shared/rendering/geometry_mesh` tessellates the floor areas (Bézier-subdivided) into
   `GeometryVertex` (position/normal/tangent/uv/color); `shared/rendering/tile_mesh` batches the tiles
   by texture.
3. **Validate** — per-deck report: area/mesh/triangle counts, degenerate (zero-area) triangles,
   non-finite verts, Box2D-invalid collision polys (>8 or <3 verts), open chains (info), missing
   textures, finite bounds.
4. **Export** — one `.gltf` per deck with **one mesh per procedural shape** (floor area) **+ one mesh
   per tile batch**, a **manifest** for per-mesh culling, and a **collision** file.

## Build & run

```bash
cmake -B build && cmake --build build --target incremental_viewer
cd build/tools/incremental_viewer
./incremental_viewer \
    --source ../../../../uber/uberdroid/ship1/xmapfile0.txt \
    -t ../../../../uber/uberdroid/data/tiles.txt \
    -x ../../../../uber/uberdroid/data/textures.txt \
    --textures-base ../../../../uber/uberdroid/
```

The directory containing `xmapfile{N}.txt` is scanned so all decks can be cycled in-app.

### Controls (additions)

| Key | Action |
|-----|--------|
| `[` / `]` | Previous / next deck |
| `U` | Toggle the class-14 reference droid (spawned at the origin via the real `UnitManager`) |
| `V` | Toggle the validity report panel (also logged to console) |
| `X` | Export the current deck (combined) to `<output>/export/level_<n>/` |
| `Shift+X` | Split export: one `.gltf` per shape + `_index.json` (for isolating bad sections) |

(Existing keys: WASD/QE move, T/I/P camera presets, F1–F5 toggles, H help.)

### Headless batch export

```bash
./incremental_viewer --export-all out/ \
    --source ../../../../uber/uberdroid/ship1/xmapfile0.txt \
    -t .../tiles.txt -x .../textures.txt --textures-base .../uberdroid/
```

Loads + validates + exports every deck to `out/level_<n>/`, then exits.

## Export outputs (`level_<n>/`)

- **`level_<n>.gltf`** — one mesh per floor area + one per tile batch. Materials are grouped by
  texture. raylib loads each glTF primitive as its own `model.meshes[i]`, so the engine can cull
  per-mesh (draw a subset + `GetMeshBoundingBox`). Textures are copied next to the file.
- **`level_<n>.manifest.json`** — per-mesh `{ index, kind (floor/tile), id, material, textureIndex,
  min, max }`. raylib drops glTF node names on load, so this gives stable identity + bounds for
  culling.
- **`level_<n>.collision.json`** — Box2D-ready shapes: CCW convex `polygons` (≤8 verts) and `chains`
  (`loop` flag), 2D in the game plane (X, Y-forward), **pre-scaled** to match the exported mesh.

Wall geometry (`kind:"wall"`) is the legacy uberdroid generator ported into
`shared/rendering/wall_mesh` — each `PathLink`'s cross-section **profile** (the curved arch "Default
Wall", floor borders, etc., from `geometryGen.cpp::loadStandard`) swept along the link path, with
materials/heights resolved from `data/materials.xml` (`--materials`, default `<srcDir>/../data`).
Profile-less links inherit the geometry's **default profile set** (the `<Profiles>` ids) unless they
carry `defaultProfiles="0"` — that's how interior walls are declared.

- **End caps** — a flat fill of the cross-section (`mCapPoints`/`mCapTriangles`) is placed at a
  link's *open* ends (nodes with a single incident wall link).
- **Miter joins** — at degree-2 corners each section's lateral offset is replaced by the standard
  2D stroke **miter vector** (`(n1+n2)/(1+n1·n2)`), so the corner extends to the true intersection
  (an improvement over the legacy clip-to-fixed-length). Both links at the node compute coincident
  outlines, so the walls join seamlessly; near-degenerate turns keep a square end.

Deferred: T-junctions / crossings (degree > 2) are not yet mitred; the swept solid isn't emitted as
collision (the 2D link chains still are).

Notes:
- Triangle **strips** were not used — raylib draws indexed triangles at runtime, so strips add
  loader/runtime work for no gain here.
