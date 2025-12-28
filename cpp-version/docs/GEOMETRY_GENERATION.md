# Geometry Generation Plan

This document describes the plan to read PathGeometry XML files and generate raylib meshes for rendering floor areas, using the same bump-mapped lighting shader as tile rendering.

## Overview

PathGeometry XML files define procedural geometry using:
- **Nodes**: 3D waypoints (junction points)
- **Links**: Connections between nodes (edges), optionally with Bezier control points
- **Areas**: Floor regions bounded by link chains
- **Profiles**: Cross-section definitions for wall extrusion (future phase)

**Phase 1** focuses on floor areas (2D polygon tessellation).
**Phase 2** (future) will add profile extrusion for walls and borders.

---

## Source Data Format

### Geometry XML Structure

```xml
<Path>
    <Nodes>
        <Node id="0" x="544" y="976" z="1" />
        <!-- z is height (typically 0-1 for floors) -->
    </Nodes>
    <Links>
        <Link id="0" start="24" finish="26">
            <Control x="288" y="96" z="1" />  <!-- Bezier curve -->
            <Profile id="0" />
            <Profile id="1" />
        </Link>
        <Link id="49" start="26" finish="52" defaultProfiles="0" />
    </Links>
    <Areas>
        <Area id="0" materialID="0">
            <Link id="0" />
            <Link id="49" />
            <!-- Links form closed polygon boundary -->
        </Area>
    </Areas>
</Path>
```

### Materials XML (data/materials.xml)

```xml
<MaterialData>
    <Materials>
        <Material id="0" texture0="79" texture1="1" name="floor">
            <TexGen0 s="0.015625" t="0.015625" type="1"/>
        </Material>
    </Materials>
    <Profiles>
        <Profile id="0" default="0" materialID="0" occlusionHeight="80.0"/>
    </Profiles>
</MaterialData>
```

---

## Coordinate Systems

### Game Space (XML)
- X: Horizontal (left-right)
- Y: Horizontal (forward-backward)
- Z: Height (vertical)

### Render Space (Raylib)
- X: Horizontal (unchanged)
- Y: Vertical (UP) ← from game Z
- Z: Horizontal (depth) ← from game Y

### Transform
```cpp
Vector3 gameToRenderCoords(float gameX, float gameY, float gameZ, float scale) {
    return {
        gameX * scale,      // X unchanged
        gameZ * scale,      // Game Z → Render Y (up)
        gameY * scale       // Game Y → Render Z (depth)
    };
}
// Scale: 0.0254 (game units in inches → meters)
```

---

## Implementation

### Data Structures

```cpp
// Vertex with all attributes for lighting shader
struct GeometryVertex {
    Vector3 position;
    Vector3 normal;      // {0,1,0} for floors (UP)
    Vector4 tangent;     // {1,0,0,1} for floors (+X, right-handed)
    Vector2 uv;          // Planar-projected texture coordinates
    Color color;         // From material (default WHITE)
};

// Single mesh (one per material)
struct GeometryMesh {
    std::vector<GeometryVertex> vertices;
    std::vector<uint32_t> indices;
    int materialId;
    BoundingBox bounds;
};

// Collection from one PathGeometry
struct GeometryMeshCollection {
    std::vector<GeometryMesh> meshes;
    BoundingBox totalBounds;
    bool success;
    const char* error;
};
```

### Area Tessellation Algorithm

1. **Collect boundary vertices** from area's link chain:
   ```
   for each link in area.links:
       if link has control point:
           subdivide Bezier curve into segments
       add node positions to boundary polygon
   ```

2. **Transform to render space** using `gameToRenderCoords()`

3. **Tessellate polygon**:
   - Simple fan triangulation for convex polygons
   - Ear-clipping for non-convex polygons

4. **Generate vertex attributes**:
   - Normal: `{0, 1, 0}` (UP for all floor vertices)
   - Tangent: `{1, 0, 0, 1}` (aligned with +X/U texture axis)
   - UV: Planar projection using material TexGen scale
   - Color: WHITE (texture provides color)

### Bezier Curve Subdivision

Links with `<Control>` elements define quadratic Bezier curves:

```cpp
Vector3 evaluateQuadraticBezier(Vector3 p0, Vector3 control, Vector3 p1, float t) {
    float u = 1.0f - t;
    return p0 * (u * u) + control * (2 * u * t) + p1 * (t * t);
}

// Subdivide into 8 segments for smooth curves
for (int i = 0; i <= 8; i++) {
    float t = i / 8.0f;
    vertices.push_back(evaluateQuadraticBezier(start, control, end, t));
}
```

### UV Generation

Planar projection matching original game's TexGen type 1:

```cpp
Vector2 generatePlanarUV(Vector3 renderPos, float scaleS, float scaleT) {
    // Project onto X-Z plane (floor is horizontal in render space)
    return { renderPos.x * scaleS, renderPos.z * scaleT };
}

// Material scales from materials.xml:
// Floor:  s=0.015625, t=0.015625 (64 units per texture repeat)
// Wall:   s=0.02, t=0.02 (50 units per texture repeat)
// Border: s=0.015625, t=0.015625
```

---

## API

```cpp
// Generate meshes from PathGeometry
GeometryMeshCollection createGeometryMeshes(
    const PathGeometry& geometry,
    float scale = SCALE_UNITS_TO_METERS
);

// Load as raylib Model
Model loadGeometryModel(const GeometryMeshCollection& collection);

// Apply lighting shader with bump mapping
void applyGeometryShader(Model* model, SceneRenderer* renderer);

// Cleanup
void freeGeometryMeshCollection(GeometryMeshCollection* collection);
```

---

## Rendering

Uses the same `lighting.vs/fs` shader as tiles:
- Diffuse texture from material `texture0`
- Normal/bump map from material `texture1`
- Blinn-Phong lighting with bump mapping

Vertex attributes match shader expectations:
- `vertexPosition` → position
- `vertexNormal` → normal (0,1,0 for floors)
- `vertexTangent` → tangent (1,0,0,1 for floors)
- `vertexTexCoord` → uv

---

## JSON Output Format

For saving generated geometry in a modern format:

```json
{
  "version": "1.0",
  "type": "PathGeometryMesh",
  "sourceFile": "lvl0section0.xml",
  "meshes": [
    {
      "materialId": 0,
      "primitiveType": "triangles",
      "vertexCount": 24,
      "indexCount": 36,
      "vertices": {
        "position": [[x,y,z], ...],
        "normal": [[0,1,0], ...],
        "tangent": [[1,0,0,1], ...],
        "texcoord": [[u,v], ...]
      },
      "indices": [0, 1, 2, ...],
      "bounds": {
        "min": [x, y, z],
        "max": [x, y, z]
      }
    }
  ],
  "materials": [
    {
      "id": 0,
      "name": "floor",
      "diffuseTexture": 79,
      "normalTexture": 1,
      "texgenScale": [0.015625, 0.015625]
    }
  ]
}
```

---

## Files to Modify/Create

| File | Action |
|------|--------|
| `shared/rendering/geometry_mesh.h` | **Create** - API and structures |
| `shared/rendering/geometry_mesh.cpp` | **Create** - Implementation |
| `shared/scene_convert/scene_types.h` | **Modify** - Add GeometryMaterial struct |
| `cmake/SharedSources.cmake` | **Modify** - Add geometry_mesh.cpp |
| `tools/incremental_viewer/viewer.cpp` | **Modify** - Render geometry meshes |

---

## Future: Profile Extrusion (Phase 2)

For walls, borders, pillars:
1. Load profile cross-sections from materials.xml `default` attribute
2. Extrude profile points along subdivided link paths
3. Generate side-facing normals based on path direction
4. Compute tangents from extrusion direction
5. Create collision geometry from solid types (quad, walls, points)

Profile types from original code:
- `default=0`: Floor edge profile
- `default=1`: Wall profile (vertical extrusion)
- `default=2`: Border profile (decorative trim)
- `default=3`: Glass wall
- etc.
