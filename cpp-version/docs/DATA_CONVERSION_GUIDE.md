# Data Conversion Guide - Agent Reference

This document provides guidance for AI agents working on data conversion tools. It captures lessons learned from converting legacy game data formats and recommends approaches that minimize errors and enable validation.

## Problem Statement

Converting data from legacy formats involves multiple potential error sources:
1. **Parsing errors** - Misinterpreting custom text/binary formats
2. **Semantic errors** - Ignoring or mishandling format flags (e.g., tileType)
3. **Coordinate errors** - Wrong axis mapping or scaling
4. **Winding errors** - Incorrect triangle winding for rendering

These errors compound when the conversion pipeline has multiple steps without intermediate validation.

## Recommended Architecture

### Use Original Parsers Directly

**Do not reimplement parsing logic.** Copy the original parsing code into the tool project and use it directly:

```
uber/source/uberdroid/
├── area.h/cpp          → Copy to tool for tile/area parsing
├── geometry.h/cpp      → Copy to tool for PathGeometry XML
├── archetile.h/cpp     → Copy to tool for tile expansion
└── pathgeometry.h/cpp  → Copy to tool for floor mesh generation
```

This eliminates parsing bugs entirely. The original code is battle-tested.

### Conversion Tool Structure

```
tools/legacy_converter/
├── uber_source/           # Copied original source files
│   ├── area.h/cpp
│   ├── geometry.h/cpp
│   └── ...
├── conversion/            # New conversion layer
│   ├── tile_convert.h/cpp
│   ├── geometry_convert.h/cpp
│   └── coordinate_transform.h/cpp
├── validation/            # Reference comparison
│   ├── reference_renderer.h/cpp
│   └── diff_tool.h/cpp
└── main.cpp
```

### Pipeline Design

```
Phase 1: Parse with Original Code
┌─────────────────────────────────────────────────────────┐
│  Source Files → Original Parser → Original Structs     │
│                                                         │
│  - No interpretation required                           │
│  - Proven correct parsing                               │
│  - All format flags preserved (tileType, etc.)          │
└─────────────────────────────────────────────────────────┘
                           ↓
Phase 2: Reference Rendering (Optional but Recommended)
┌─────────────────────────────────────────────────────────┐
│  Original Structs → Original Render Code → Reference    │
│                                                         │
│  - Establishes known-correct visual output              │
│  - Can use original OpenGL calls or software render     │
│  - Captures reference images for comparison             │
└─────────────────────────────────────────────────────────┘
                           ↓
Phase 3: Explicit Conversion
┌─────────────────────────────────────────────────────────┐
│  Original Structs → Conversion Functions → New Structs  │
│                                                         │
│  - Coordinate transform (game space → render space)     │
│  - Scale factor applied (inches → meters)               │
│  - Winding order correction                             │
│  - Explicit field-by-field mapping                      │
└─────────────────────────────────────────────────────────┘
                           ↓
Phase 4: Validation
┌─────────────────────────────────────────────────────────┐
│  New Structs → New Render Code → Test Image             │
│                    ↓                                    │
│              Compare with Reference                     │
│                                                         │
│  - Pixel diff or visual inspection                      │
│  - Catches winding, UV, color errors                    │
│  - Automated regression testing possible                │
└─────────────────────────────────────────────────────────┘
                           ↓
Phase 5: Serialization
┌─────────────────────────────────────────────────────────┐
│  New Structs → JSON/Binary → Output Files               │
│                                                         │
│  - Clean, documented format                             │
│  - Optimizations applied (batching, deduplication)      │
│  - Version field for future compatibility               │
└─────────────────────────────────────────────────────────┘
```

## Coordinate System Transformation

### Game Space (Original)
- X: horizontal (east-west)
- Y: horizontal (north-south / forward)
- Z: vertical (height / up)
- Units: inches

### Render Space (New)
- X: horizontal (east-west) - unchanged
- Y: vertical (up) - was Z
- Z: horizontal (depth) - was Y
- Units: meters

### Transform Function
```cpp
Vector3 gameToRender(float gameX, float gameY, float gameZ, float scale) {
    return {
        gameX * scale,      // X unchanged
        gameZ * scale,      // Game Z → Render Y
        gameY * scale       // Game Y → Render Z
    };
}

constexpr float SCALE_INCHES_TO_METERS = 0.0254f;
```

**Apply coordinate transform AFTER parsing, not during.** This keeps parsing code unchanged and makes the transform explicit and testable.

## Winding Order Correction

The original game uses CW winding when viewed from above. OpenGL/Raylib expects CCW for front-facing triangles.

### Detection
If geometry doesn't render with backface culling ON but appears with culling OFF, the winding is reversed.

### Fix
Reverse index order for all triangles:
```cpp
// Original: (a, b, c)
// Corrected: (a, c, b)
indices[i+0] = a;
indices[i+1] = c;  // swapped
indices[i+2] = b;  // swapped
```

### Tile Type Handling
The original game supports multiple triangle generation modes:

| tileType | Name | Algorithm |
|----------|------|-----------|
| 0 | tt_fan | Triangle fan - vertex 0 shared |
| 1 | tt_strip | Triangle strip - alternating winding |
| 2 | tt_tris | Explicit triangles - 3 verts each |
| ≥3 | (material) | Archetile material index - use fan |

**Critical:** Values ≥3 are material indices from archetile expansion, NOT rendering modes. Always clamp: `if (tileType > 2) tileType = 0;`

## Conversion Layer Design

### Explicit Field Mapping
```cpp
// conversion/tile_convert.h

// Document every semantic detail
struct TileConversionNotes {
    // tileType values:
    // 0 = tt_fan: Triangle fan, vertex 0 shared
    // 1 = tt_strip: Triangle strip, alternating winding
    // 2 = tt_tris: Explicit triangles
    // >= 3: Archetile material index, render as fan
    //
    // Winding: Original is CW from above, must reverse for OpenGL CCW
};

NewTile convertTile(const OldTile& old, float scale) {
    NewTile t;

    // Explicit vertex conversion with coordinate transform
    for (const auto& v : old.vertices) {
        t.vertices.push_back(convertVertex(v, scale));
    }

    // Preserve semantic flags
    t.tileType = old.tile_type;  // Keep original value

    // Convert textures
    t.textureIndex1 = old.texture_index_1;
    t.textureIndex2 = old.texture_index_2;

    // Convert properties with clear mapping
    t.properties.diffuseColor = convertColor(old.diffuse_colour);

    return t;
}
```

### Unit Tests for Conversion
```cpp
TEST(TileConversion, PreservesTileType) {
    OldTile old;
    old.tile_type = 1;  // tt_strip

    auto result = convertTile(old, 0.0254f);

    EXPECT_EQ(result.tileType, 1);
}

TEST(TileConversion, TransformsCoordinates) {
    OldTile old;
    old.vertices[0] = {100, 200, 50};  // game space

    auto result = convertTile(old, 0.0254f);

    // Game (100, 200, 50) → Render (2.54, 1.27, 5.08)
    EXPECT_FLOAT_EQ(result.vertices[0].x, 2.54f);
    EXPECT_FLOAT_EQ(result.vertices[0].y, 1.27f);  // was Z
    EXPECT_FLOAT_EQ(result.vertices[0].z, 5.08f);  // was Y
}

TEST(TileConversion, HandlesStripWinding) {
    // Create a 4-vertex strip tile
    OldTile old;
    old.tile_type = 1;  // tt_strip
    old.vertices = {{0,0,0}, {1,0,0}, {0,1,0}, {1,1,0}};

    auto mesh = generateMesh(convertTile(old, 1.0f));

    // Verify CCW winding when viewed from +Y
    // Triangle 0: should be CCW
    // Triangle 1: should be CCW (with alternation applied)
}
```

## Geometry Generation

For PathGeometry floor areas, the original code includes triangulation. Options:

### Option A: Use Original Triangulation
```cpp
// Copy original pathgeometry triangulation code
#include "uber_source/pathgeometry.h"

PathGeometry geom;
geom.loadFromXML("section.xml");
auto mesh = geom.triangulate();  // Original algorithm

// Then convert the resulting mesh
NewMesh converted = convertMesh(mesh, SCALE_INCHES_TO_METERS);
```

### Option B: Re-triangulate After Conversion
```cpp
// Convert boundary points only
auto boundary = convertBoundary(geom.getBoundary(), scale);

// Use new triangulation (ear-clipping, etc.)
auto mesh = triangulatePolygon(boundary);
```

Option A is safer for initial conversion. Option B allows optimization but requires validation against Option A output.

## Optimization Phase

After validating that converted data renders correctly, optimizations can be applied:

1. **Texture batching** - Group geometry by texture to reduce bind calls
2. **Mesh merging** - Combine small meshes into larger batches
3. **Level-of-detail** - Generate simplified versions for distance
4. **Spatial partitioning** - Organize for frustum culling

Each optimization should be validated:
```cpp
// Before optimization
auto referenceImage = renderScene(convertedData);

// After optimization
auto optimizedData = optimize(convertedData);
auto testImage = renderScene(optimizedData);

// Validate
EXPECT_IMAGE_MATCH(referenceImage, testImage);
```

## Debugging Aids

### Visual Toggles
Add keyboard controls for debugging:
- **F6**: Show tile/polygon indices at centroids
- **F7**: Toggle backface culling (reveals winding issues)
- **F5**: Wireframe overlay (shows triangle structure)

### Logging
Log first few items during conversion:
```cpp
if (tileIndex < 3) {
    TraceLog(LOG_INFO, "Tile %d: %d verts, tileType=%d",
             tileIndex, tile.vertices.size(), tile.tileType);
}
```

### Validation Assertions
```cpp
assert(tile.vertices.size() >= 3 && "Degenerate tile");
assert(tileType >= 0 && tileType <= 2 || tileType >= 3 && "Invalid tileType");
```

## Common Pitfalls

1. **Reimplementing parsers** - Always copy original code instead
2. **Assuming fixed vertex counts** - Tiles can have variable vertices
3. **Ignoring format flags** - tileType, texture flags, etc. have meaning
4. **Converting coordinates during parsing** - Keep parsing pure, transform after
5. **No reference comparison** - Always validate against known-correct output
6. **Silent failures** - Log everything, fail loudly on unexpected data

## File Organization

```
cpp-version/
├── docs/
│   ├── DATA_CONVERSION_GUIDE.md  (this file)
│   ├── RENDERING_PIPELINE.md     (tile geometry details)
│   └── ORIGINAL_FORMATS.md       (source format documentation)
├── tools/
│   ├── legacy_converter/         (main conversion tool)
│   ├── incremental_viewer/       (visual validation)
│   └── reference_tool/           (original code rendering)
└── shared/
    ├── scene_convert/            (conversion layer)
    └── rendering/                (new rendering code)
```

## Related Documentation

- [RENDERING_PIPELINE.md](RENDERING_PIPELINE.md) - Tile geometry, winding order details
- [ORIGINAL_FORMATS.md](ORIGINAL_FORMATS.md) - Source data format specifications
- [JSON_FORMATS.md](JSON_FORMATS.md) - Output format specifications
