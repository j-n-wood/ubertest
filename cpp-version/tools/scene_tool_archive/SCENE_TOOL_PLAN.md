# Scene Import Tool Implementation Plan

Convert ship1 level files to structured JSON format with 3D viewer and 2D physics debug visualization.

## Overview

Create a `scene_tool` in `cpp-version/tools/scene_tool/` that:
1. Parses ship1 hierarchy: Ship → Domains → Areas
2. Outputs structured JSON with inlined geometry data
3. Provides a 3D rendered viewer with 2D physics debug overlay
4. Uses shared rendering code (SceneRenderer, lighting) from model_tool/unit_test
5. Uses Box2D for physics with static collision shapes from geometry

## Rendering Architecture

Uses the existing shared rendering infrastructure:

```
shared/
├── rendering/scene_renderer.h/cpp   # 3D lighting, shaders, debug modes
├── lighting/light.h/cpp             # Light management
└── utils/string_utils.h/cpp         # Path utilities
```

**Key Components:**
- `SceneRenderer` - Standard lighting shader setup (same as model_tool, unit_test, game)
- `box2d` - 2D physics for static collision geometry
- Debug rendering: wireframe physics shapes overlaid on 3D scene

## Data Hierarchy

```
Ship (ship1.txt)
├── Name, Crew, Capacity, Description
├── Transporters (transport.txt) - Lifts connecting domains
├── Decks (lifts.txt) - Visual layout for deck plan UI
└── Domains[0-15] (xmapfile{n}.txt)
    ├── Level header, Name, Ambience, Profile[9]
    ├── Areas
    │   ├── Bounds
    │   ├── Tiles (floor geometry) → 3D mesh
    │   ├── Archetiles (64x64 quads) → convert to tiles
    │   ├── Features → GLTF models + physics shapes
    │   └── Geometry XML → 3D mesh + static physics collision
    ├── Waypoints (navigation graph)
    └── Objects: Doors, Consoles, Chargers, Destructibles
```

## Directory Structure

```
cpp-version/
├── shared/
│   ├── rendering/scene_renderer.h/cpp  # Existing - 3D rendering
│   ├── lighting/light.h/cpp            # Existing - Lighting
│   └── scene_convert/                  # NEW - parsing code
│       ├── scene_types.h               # Data structures
│       ├── ship_parser.h/cpp           # Ship file parser
│       ├── domain_parser.h/cpp         # Domain/xmapfile parser
│       ├── archetile_parser.h/cpp      # tiles.txt loader
│       ├── geometry_xml_parser.h/cpp   # Path XML parser
│       └── scene_json.h/cpp            # JSON serialization
├── tools/
│   └── scene_tool/
│       ├── CMakeLists.txt
│       ├── AGENTS.md
│       ├── main.cpp                    # CLI entry point
│       ├── scene_viewer.h/cpp          # 3D viewer with physics debug
│       ├── transport_parser.h/cpp      # transport.txt parser
│       ├── lifts_parser.h/cpp          # lifts.txt parser
│       └── output/                     # Generated JSON
│           ├── ship.json
│           └── domains/
└── docs/
    ├── SCENE_TOOL_PLAN.md              # This file
    ├── ORIGINAL_FORMATS.md             # Original format documentation
    └── JSON_FORMATS.md                 # New JSON format specification
```

## Implementation Steps

### Step 1: Create scene_types.h
Define C++ structs for all data types:
- Ship, Transporter, Deck, Elevator, DomainRect
- Domain, Area, Tile, Feature, PathGeometry, Waypoint
- SceneObject variants (Door, Console, Charger, Destructible)
- PhysicsShape (circle, box, polygon) for collision geometry
- Spawn definitions (PLACEDROID)

### Step 2: Implement ship_parser
Parse ship1.txt:
- `Name`, `Crew`, `Capacity`, `Desc`
- `Domain <path>` references (collect list)
- `Transporters <path>` → parse transport.txt
- `Decks <path>` → parse lifts.txt

### Step 3: Implement domain_parser (xmapfile)
Parse domain/level format:
- `Level N` header
- `Area` with 4 bounds vectors
- `Tile` inline geometry (vertices, UVs, textures, properties)
- `Archetile <index> <x> <y>` → convert to tile geometry (see Step 4)
- `Feature` (position, rotation, flags, archetype) → model path + physics shape
- `Geometry <path>` → inline XML content + generate collision shapes
- `Waypoint` (id, position, 5 flags, 6 neighbors)
- Object types: Door, Console, Charger, Destructible
- `EndDomain` footer with `NAME`, `AMBIENCE`, `PROFILE`
- `PLACEDROID` spawn definitions

### Step 4: Implement archetile_parser
Parse tiles.txt (24 archetypes):
- Header: `number 24`
- Each archetype: `<Name> <Index>` followed by tile::load() format
- 4 vertices (x, y, z), two UV coordinate sets, texture indices
- Optional: DiffuseColour, SpecularColour, EffectTexture, blend modes

**Archetile Conversion Process:**
1. Load tiles.txt archetypes into memory (24 definitions)
2. For each `Archetile <index> <x> <y>` in domain:
   - Look up archetype by index
   - Copy 4 vertices with position offset (x, y)
   - Preserve texture indices, colors, and effect properties
3. Post-process: merge adjacent same-texture tiles into larger triangle fans

### Step 5: Implement geometry_xml_parser
Add tinyxml2 dependency and parse Path XML:
- `<Nodes>` with `<Node id="" x="" y="" z="" />`
- `<Links>` with optional `<Control>` and `<Profile>` children
- `<Profiles>` definitions
- `<Areas>` with materialID and link references

**Collision Shape Generation:**
1. Parse path nodes and links from XML
2. Generate edge chains from linked nodes (for wall segments)
3. Generate polygon shapes from closed areas
4. Store as Box2D-compatible collision data:
   - Edge chains for walls (b2ChainDef)
   - Polygons for solid areas (b2Polygon)
5. Output collision shapes in JSON for runtime use

### Step 6: Implement asset resolution
- Map renderIndex → GLTF model path (from droid_output/models/)
- Map textureIndex → texture file path (assets/textures/)
- Features: reference GLTF model, generate physics shape from bounds

### Step 7: Implement scene_json
JSON serialization using nlohmann/json:
- `shipToJson()` / `jsonToShip()`
- `domainToJson()` / `jsonToDomain()`
- Include collision shape data in JSON

### Step 8: Build scene_tool CLI
```bash
# Convert ship1
./scene_tool --convert-ship ../uber/uberdroid/data/ship1.txt -o output/

# Convert single domain
./scene_tool --convert-domain ../uber/uberdroid/ship1/xmapfile0.txt -o domain_0.json

# View domain with physics debug
./scene_tool --view output/domains/domain_0.json

# View with specific options
./scene_tool --view output/ship.json --domain 0 --show-physics
```

### Step 9: Implement scene_viewer (3D + Physics Debug)
Uses shared SceneRenderer for 3D rendering, Box2D for physics.

**Initialization:**
```cpp
struct SceneViewer {
    SceneRenderer renderer;      // Shared 3D rendering
    b2WorldId worldId;           // Physics world
    Camera3D camera;             // 3D camera (overhead view)

    // Loaded scene data
    Domain currentDomain;
    std::vector<Model> tileModels;
    std::vector<Model> featureModels;
    std::vector<b2BodyId> staticBodies;  // Collision geometry

    // Display options
    bool showPhysics = true;     // Debug physics shapes
    bool showWaypoints = true;
    bool showObjects = true;
};
```

**Rendering Pipeline:**
1. BeginMode3D with overhead camera
2. Draw tile geometry (floor meshes with textures)
3. Draw feature models (GLTF objects)
4. Draw geometry paths (wall meshes)
5. If showPhysics: draw debug wireframes for all static bodies
6. If showWaypoints: draw waypoint graph (nodes + connections)
7. EndMode3D
8. Draw 2D overlay (info, controls)

**Physics Debug Drawing:**
```cpp
void drawPhysicsDebug(SceneViewer* viewer) {
    // Iterate all static bodies
    for (b2BodyId bodyId : viewer->staticBodies) {
        b2Vec2 pos = b2Body_GetPosition(bodyId);
        float angle = b2Body_GetAngle(bodyId);

        // Get shapes
        int shapeCount = b2Body_GetShapeCount(bodyId);
        b2ShapeId shapes[32];
        b2Body_GetShapes(bodyId, shapes, 32);

        for (int i = 0; i < shapeCount; i++) {
            b2ShapeType type = b2Shape_GetType(shapes[i]);

            if (type == b2_polygonShape) {
                b2Polygon poly = b2Shape_GetPolygon(shapes[i]);
                // Draw polygon wireframe in 3D (Y = fixed height)
                drawPolygonWireframe(poly, pos, angle, BLUE);
            }
            else if (type == b2_chainShape) {
                // Draw chain segments
                b2ChainSegment segment;
                // Iterate chain...
                drawLineSegment(segment, pos, GREEN);
            }
        }
    }
}
```

**Camera Controls:**
- WASD: Pan camera (XZ plane)
- QE: Raise/lower camera height
- Mouse wheel: Zoom
- Right-click drag: Rotate view
- 1-9: Jump to domain (when viewing ship)

**Keyboard Shortcuts:**
- F1: Toggle physics debug
- F2: Toggle waypoints
- F3: Toggle objects
- Tab: Next domain
- Shift+Tab: Previous domain

### Step 10: Domain Mesh Generation

Convert tile/geometry data to Raylib meshes at load time:

**Tile Mesh Generation:**
```cpp
Model createTileMesh(const std::vector<Tile>& tiles) {
    // Count total vertices
    int vertexCount = 0;
    for (const auto& tile : tiles) {
        vertexCount += tile.vertices.size();
    }

    // Allocate mesh
    Mesh mesh = {0};
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = vertexCount / 3;
    mesh.vertices = (float*)malloc(vertexCount * 3 * sizeof(float));
    mesh.texcoords = (float*)malloc(vertexCount * 2 * sizeof(float));
    mesh.normals = (float*)malloc(vertexCount * 3 * sizeof(float));

    // Fill vertex data from tiles...

    UploadMesh(&mesh, false);
    return LoadModelFromMesh(mesh);
}
```

**Geometry Path to Collision:**
```cpp
void createCollisionFromGeometry(b2WorldId world, const PathGeometry& geom,
                                  std::vector<b2BodyId>& outBodies) {
    // Create static body for collision shapes
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_staticBody;
    b2BodyId bodyId = b2CreateBody(world, &bodyDef);

    // For each area in geometry, create polygon shape
    for (const auto& area : geom.areas) {
        // Collect vertices from area boundary links
        std::vector<b2Vec2> vertices;
        for (int linkId : area.links) {
            const auto& link = geom.links[linkId];
            vertices.push_back({geom.nodes[link.start].x, geom.nodes[link.start].y});
        }

        // Create hull from vertices
        b2Hull hull = b2ComputeHull(vertices.data(), vertices.size());
        b2Polygon poly = b2MakePolygon(&hull, 0);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        b2CreatePolygonShape(bodyId, &shapeDef, &poly);
    }

    // For wall segments (links that are area boundaries)
    // Create edge chains...

    outBodies.push_back(bodyId);
}
```

## CMake Integration

**tools/scene_tool/CMakeLists.txt:**
```cmake
add_executable(scene_tool
    main.cpp
    scene_viewer.cpp
    transport_parser.cpp
    lifts_parser.cpp
    ${SHARED_SOURCES}
    ${SCENE_CONVERT_SOURCES}
)

target_include_directories(scene_tool PRIVATE
    ${SHARED_INCLUDE_DIR}
)

target_link_libraries(scene_tool
    raylib
    box2d
    tinyxml2
    nlohmann_json::nlohmann_json
)
```

**Dependencies.cmake** - Add tinyxml2:
```cmake
FetchContent_Declare(
    tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 10.0.0
)
FetchContent_MakeAvailable(tinyxml2)
```

**SharedSources.cmake** - Add SCENE_CONVERT_SOURCES:
```cmake
set(SCENE_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_types.h
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/ship_parser.h
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/ship_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.h
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.h
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.h
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.h
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
)
```

## Key Reference Files

| File | Purpose |
|------|---------|
| shared/rendering/scene_renderer.h | Shared 3D rendering infrastructure |
| tools/unit_test/test_scene.cpp | Reference for Box2D + SceneRenderer integration |
| src/physics/physics_world.h | Box2D physics patterns |
| uber/source/uberdroid/paraship.cpp | Ship loading, transporter mapping |
| uber/source/uberdroid/paradomain.cpp | Domain footer parsing |
| uber/source/uberdroid/domain.cpp | Domain loading |
| uber/source/uberdroid/area.cpp | Area/tile/feature parsing |
| uber/uberdroid/data/ship1.txt | Ship definition |
| uber/uberdroid/ship1/xmapfile0.txt | Domain example |
| uber/uberdroid/ship1/lvl0section0.xml | Geometry XML example |
| uber/uberdroid/data/tiles.txt | Archetile definitions |

## Scope Limitations

This implementation focuses on **ship1 only**. Additional ships can be added later by:
1. Parsing additional ship{n}.txt files
2. Using the same domain parser for their xmapfiles
3. No changes to core parsing or rendering code needed

## Format Improvements vs Original

| Aspect | Original | JSON |
|--------|----------|------|
| Structure | Flat keywords | Hierarchical Ship → Domain → Area |
| Asset refs | Numeric index | Named paths (models/block.gltf) |
| Textures | Atlas index | Named file paths |
| Geometry | External XML | Inlined + collision shapes |
| Collision | None stored | Static physics shapes from geometry |
| Defaults | Always written | Omit defaults, schema defines them |
| Metadata | None | Version, source file, conversion date |

## Transformation Limitations

1. **Geometry collision is approximated**: Path XML areas become convex polygons; complex concave shapes are decomposed
2. **Archetile merging is optional**: Implementation can skip merging and use individual quads
3. **Animation not handled**: Door/console animations require separate runtime logic
4. **Sound indices preserved**: No conversion to named audio files
5. **Material effects simplified**: Additive/alpha blend flags preserved, complex effects may need runtime handling
