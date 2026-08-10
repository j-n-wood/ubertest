# Procedural geometry generation (walls, floors, collision, texturing)

Reference for how the converted 3D levels turn a **path network** (nodes + links + profiles) into
renderable wall/floor meshes, their texture coordinates, tangents, and Box2D collision. This is the
pipeline behind the `incremental_viewer` level editor and the exported `levels3d/` bundles the game's
Objects3D mode consumes. It mirrors the legacy `uber` engine (`uber/source/uberdroid/geometryGen.cpp`,
`material.h`).

> Supersedes the earlier "Phase 1 floors / Phase 2 walls" plan — walls, profiles, texgen modes,
> tangents, and collision are all implemented now.

## Data model

A level is a `Domain` (`shared/scene_convert/scene_types.h`) of `Area`s. Each `Area` carries:

- `tiles` — flat textured floor/wall **tiles** (archetiles), batched by texture.
- `geometry : vector<PathGeometry>` — the **procedural** wall/floor network. Source for swept walls
  and floor polygons.
- `collision` — optional pre-baked collision (legacy; not used by the game — see *Collision*).

A `PathGeometry` holds:

- `nodes` — `PathNode { id, position(Vector3) }`. Positions are **render-metric** (already through
  `gameToRenderCoords` at JSON serialization — see *Coordinate frame*).
- `links` — `PathLink { id, start, finish, control?(bezier), profiles[], useDefaultProfiles }`.
  A link is a wall/floor edge between two nodes. `control` (optional) makes it a quadratic Bézier.
- `profiles` — the geometry's **default** profile-id set (`<Profiles>` in the source). A link with an
  empty `profiles[]` and `useDefaultProfiles == true` sweeps this default set (how interior walls are
  declared).
- `areas` — `PathArea { id, materialId, links[] }`. Each is a closed **floor** region (its boundary
  links) with a material.

## Coordinate frame

Geometry positions are in **render-metric** space: right-handed, **+Y up**, metres, **X-Z is the
floor plane**. The legacy `gameToRenderCoords` transform (`×0.0254`, game-Y negated → render-Z) is
applied **at JSON serialization** (`saveCoords`), so anything read from a loaded `Domain`/bundle is
already metric and needs no further scaling. `1 tile = 64 game units = 64 × 0.0254 ≈ 1.6256 m`
(`WORLD_SCALE`).

## Wall generation — `shared/rendering/wall_mesh.cpp`

`createDomainWallMeshes → createWallMeshes → sweepProfile`. A **wall** is any link that produces a
wall mesh: `!link.profiles.empty() || (link.useDefaultProfiles && geometry has default profiles)`.
The *collision* generator and the wall-link debug overlay use this **same** predicate — keep them in
sync or interior walls render but get no collision.

### 1. Path build + subdivision (uber `subdivide()` pattern)

For each wall link, build a render-space path, **subdivided into limited-length sections** (~one tile
of arc each, `maxSec = 64 × scale`), for **both** straight and Bézier links:

- Straight: `steps = ceil(|p1−p0| / maxSec)`, linear interpolation.
- Bézier: `steps = ceil(controlPolygonLength / maxSec)`, quadratic evaluation.

Uniform sectioning gives consistent vertex resolution and makes the texgen modes behave predictably
(below). Each path point becomes one **cross-section ring**.

### 2. Cross-section sweep + miter joins

`sweepProfile` emits one ring of `N` profile points per path point. Ring `i` at `path[i]`, profile
point `k`:

- lateral axis `perp = {-dir.y, dir.x}` (⊥ the local path tangent, in XZ).
- `position = path[i] + (points[k].x · scale) · perp + (points[k].y · scale) · Yup`
  (profile `x = lateral offset`, `y = height`, both game units).
- At a **junction end** (`startMiter`/`endMiter`) the per-side **miter** vector replaces `perp` so the
  corner extends to the true intersection. `computeMiterSides` handles degree-2 corners and degree-3+
  T-junctions (each side mitres with its angular neighbour; falls back to a square perpendicular).

Consecutive rings are stitched into quads. Dead-end links get flat **caps** (`generateCap`, winding
oriented outward by a `facing` test).

### 3. Outward normals (winding-independent)

`computeSmoothNormals` derives normals from triangle winding — but the swept winding's *world*
orientation depends on path direction (via `perp`), so opposite-winding paths would flip normals
inward. Before stitching, `sweepProfile` **detects** the winding (compare the first quad's normal to
the profile's known-outward direction at its highest edge, whose outward ≈ +Y) and **flips all swept
triangles** if inward. Result: outward normals + correct front-face culling regardless of winding.

### 4. Texture coordinates (texgen modes)

`V` (t) = the profile point's `texcoordT[k]` (runs across/over the cross-section). `U` (s) is
**uniform per ring** and selected by the material's `<TexGen0 type>` (`WallProfile.texgenType`):

- **tile (0)** — `u = centreline length · dsdx`: uniform texel **density** along the wall length.
- **stretch (1)** — `u = section index`: one texture span **fitted per path section** (the default
  wall profile 0 → material 0 is `type=1`). Bézier/straight both section uniformly, so this tiles
  evenly.
- **fixed (2)** — no known use here; treated as tile.

**Miter & U:** U is taken from the **centreline** (which the corner miter does *not* lengthen), so the
miter is ignored — it just stretches the texture over the corner rather than injecting a per-side U
offset that would *propagate* down the wall (that propagation was the systematic opposite-sided skew
seen with per-vertex accumulated U).
*Known deviation from uber:* legacy **tile** mode included the miter's extra length in U (corner-
accurate density). Current tile mode is centreline-based like stretch. If a tile wall needs corner
density, reinstate per-vertex projected length **for tile only** (guard the propagation).

**Trim/border profiles** (near-zero height, e.g. floor borders): swept UVs skew badly on a flat,
laterally-wide strip, so these use **planar XZ UVs** (`position · 1/64`, world-X tangent), exactly like
the floor. Detected by profile vertical extent `< 2` game units (same test the collision trim filter
uses).

### 5. Tangents — `computeVertexTangents` (Lengyel, from UV + position)

raylib's `GenMeshTangents` is a coarse, non-mikktspace derivation and is **skipped once a mesh has
tangents**, so tangents are computed here the way uber did — from **position edges + UV gradients**:
accumulate a per-triangle tangent/bitangent, Gram-Schmidt the tangent against the vertex normal, and
derive handedness `w` from the bitangent sign. This keeps tangent-space normal mapping consistent
across **path joins and winding** (an explicit path-direction tangent flips the bump lighting on
opposite-winding neighbours — the join "inversion").

## Floor generation — `shared/rendering/geometry_mesh.cpp`

`PathArea` boundaries triangulate to floor polygons (fan for convex, ear-clip for non-convex). Floors
use **planar XZ texgen** (`DEFAULT_FLOOR_MATERIAL`, `texgenScale = 1/64`) and a fixed `FLOOR_TANGENT`.
Floors are **walkable** — they are *not* solid collision.

## Materials & profiles — `uber/uberdroid/data/materials.xml`

- `<Material id texture0 texture1 drawtype type>` with children `<TexGen0 s t type>` / `<TexGen1 …>`.
  `texture0` = **diffuse**, `texture1` = **bump/normal**. `drawtype 7` = bump; `drawtype 5` = bump +
  environment map + additive (env/additive **not** replicated — see below). `TexGen*.type` =
  `txg_type_t { tile=0, stretch=1, fixed=2 }` (`material.h`).
- `<Profile id default materialID occlusionHeight>` — `default` selects a built-in cross-section
  **shape** (`standardShape()` in `wall_mesh.cpp`): 0 = arch wall (32 wide × 60 tall), 1/2 =
  border-left/right trim (~0.3 tall), 3 = curved tunnel, 4 = box wall, 5 = pillar, … `materialID`
  links the profile to its textures + texgen mode.
- Archetiles (`tiles.txt`, `archetile_parser.cpp`) carry a texture **trio** `diffuse bump spec` →
  `textureIndex1` (diffuse) / `textureIndex2` (bump). Specular is currently ignored.

## Collision generation — wall footprint polygons

`writeCollision` (`tools/incremental_viewer/level_export.cpp`) exports **wall footprint quads**, not
floor areas:

- For each wall link, rebuild the same subdivided path, sweep it by the profile's **lateral extent**
  (`min/max points[].x · scale`) → one convex quad per path segment. Real wall **thickness**, follows
  the Bézier.
- **Trim excluded**: profiles with vertical extent `< 2` game units (borders) are skipped so they
  don't create or widen collision.
- Coordinates are the game's 2D physics plane (render X, render Z); the game builds them as static
  `b2` polygons (`physics_create_static_polygon`). Floor areas are deliberately not solid.

(The `PathGeometry`-embedded `CollisionData` from `generateCollisionFromGeometry` is legacy/unused by
the game — the footprint-quad export above is authoritative.)

## Bundle export + JSON

`viewerExportLevel` writes per-deck `levels3d/level_<n>/`:
`level_<n>.gltf` (geometry + per-material diffuse **and** tangent-space `normalTexture`, TANGENT
attribute), `.manifest.json`, `.collision.json`, `.entities.json` (waypoints/spawns/doors/chargers/
consoles), plus a ship-wide `levels3d/transporters.json` and a **shared** `levels3d/textures/` folder
(`texture_dir = "../textures"`, deduped, normals as lossless PNG).

## Import / export / regeneration

- The viewer reads **legacy** source (`xmapfile*.txt` geometry XML, `tiles.txt`, `materials.xml`) and
  writes/reads the **new JSON** (`domain.json` / edited `level_<n>.json`).
- The procedural geometry **fully round-trips through JSON**: `PathGeometry` (`nodes`, `links` incl.
  `control`/`profiles`/`useDefaultProfiles`, `profiles`, `areas`) is serialized under
  `areas[].geometry[]` (`scene_json.cpp` `pathGeometryToJson`/`jsonToPathGeometry`). So **wall/floor
  geometry can be regenerated from the JSON alone** — no need to re-parse the legacy XML.
- **Texgen inputs preserved:** node/control positions (render-metric), link profile assignment, and
  `PathProfile.points`. **Not yet** in the domain JSON: the material `TexGen` mode/scale and the
  `standardShape` cross-sections — these still come from `materials.xml` (the `WallProfileTable`). To
  make regeneration fully self-contained (mode + shape + dsdx per profile), extend the profile/material
  JSON to carry `texgenType`, `dsdx`, and the resolved cross-section points.
- Not serialized (reset to defaults on load): `Objects::generic`, some effect/console/destructible
  sub-fields (see `scene_json.cpp`).

## Not yet replicated from uber

- `drawtype 5` **environment-mapped / additive** materials (glass) — bump is applied, env map +
  additive blending are not (`envmapblue.jpg` is copied but unused).
- Specular (third archetile texture / material specular colour).
- Tile-mode **corner** density (uses centreline U; see *Texture coordinates*).

## Viewer diagnostics

- **N** — path-node markers + node-id labels.
- **K** — wall-link overlay: centreline (green start → red finish), yellow bezier control point, and a
  label `L<id> <start>-><finish> [B]`. Use to pin UV/normal artefacts to exact links/nodes.
- **J** — dump per-link profile assignment. **F9** — reload deck from source XML.
