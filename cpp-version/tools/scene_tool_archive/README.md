# Scene Tool

Converts ship/domain level files to structured JSON format and provides a 3D viewer with physics debug visualization.

## Overview

The scene_tool processes the original game level data hierarchy:
- **Ship** (ship1.txt) - Top-level container with transporters and deck layout
- **Domain** (xmapfile{n}.txt) - Individual level/deck with areas, tiles, objects
- **Area** - Section of a domain with tile geometry, features, and collision

Output is structured JSON suitable for runtime loading by the game engine.

## Building

Build from the cpp-version directory:

```bash
cmake -B build
cmake --build build --target scene_tool
```

The executable is located at `build/tools/scene_tool/scene_tool`.

## Commands

### Convert Ship

Converts a complete ship with all domains to JSON:

```bash
./build/tools/scene_tool/scene_tool --convert-ship /path/to/uberdroid/data/ship1.txt -o output/

./build/tools/scene_tool/scene_tool --convert-ship ../uber/uberdroid/data/ship1.txt -o output/
```

Output structure:
```
output/
├── ship.json           # Ship metadata, transporters, deck layout
└── domains/
    ├── domain_0.json   # Bridge
    ├── domain_1.json   # Deck 1
    └── ...             # Additional decks
```

### Convert Single Domain

Converts one domain file to JSON:

```bash
./build/tools/scene_tool/scene_tool --convert-domain /path/to/uberdroid/ship1/xmapfile0.txt \
    -t /path/to/uberdroid/data/tiles.txt \
    -o domain_0.json
```

### View Scene

Interactive 3D viewer for converted JSON:

```bash
# View entire ship (navigate between domains)
./build/tools/scene_tool/scene_tool --view output/ship.json

# View single domain
./build/tools/scene_tool/scene_tool --view output/domains/domain_0.json

# View specific domain from ship
./build/tools/scene_tool/scene_tool --view output/ship.json --domain 5
```

## Command Line Options

| Option | Description |
|--------|-------------|
| `--convert-ship <path>` | Convert ship file to JSON |
| `--convert-domain <path>` | Convert domain file to JSON |
| `--view <path>` | Open 3D viewer for JSON file |
| `-o, --output <path>` | Output directory or file |
| `-t, --tiles <path>` | Path to tiles.txt for archetile expansion |
| `-a, --asset-path <path>` | Base path for shaders/textures |
| `-d, --domain <n>` | Domain index to view (default: 0) |
| `--reference-model <path>` | Load GLTF model for rendering comparison |
| `--no-physics` | Disable physics debug display |
| `--help` | Show help message |

## Viewer Controls

### Camera
| Key | Action |
|-----|--------|
| W/S | Move forward/backward |
| A/D | Move left/right |
| Q/E | Move up/down |
| Mouse wheel | Zoom in/out |
| Shift | Move faster |
| R | Reset camera to domain center |
| C | Move camera to test position near origin |

### Display Toggles
| Key | Action |
|-----|--------|
| F1 | Toggle physics collision wireframes |
| F2 | Toggle waypoint graph |
| F3 | Toggle objects (doors, consoles, etc.) |
| F4 | Toggle tile geometry |
| F5 | Toggle debug statistics panel |
| F6 | Toggle shader rendering (for debugging) |
| F7 | Toggle backface culling |
| F8 | Toggle reference model visibility |
| H | Toggle help panel |

### Shader Debug Modes
| Key | Action |
|-----|--------|
| 0 | Normal rendering |
| 1 | Show diffuse only |
| 2 | Show normals |
| 3 | Show specular |
| 4 | Show lighting |
| 5 | Show UV coordinates |

Press F6 to disable the lighting shader entirely for basic debug rendering without any shader effects.

Press F7 to toggle backface culling (OFF by default). With culling off, both sides of triangles are visible which helps debug triangle winding issues.

Press F8 to toggle the reference model visibility when using `--reference-model`. The reference model is drawn at position (100, 0, 100) for comparison with tile geometry.

### Scale Adjustment
| Key | Action |
|-----|--------|
| [ | Decrease geometry scale (0.5x) |
| ] | Increase geometry scale (2x) |

The geometry scale affects how coordinates are transformed during rendering. Original data uses 64-unit tiles. Adjust scale if geometry appears too large or too small.

### Navigation
| Key | Action |
|-----|--------|
| Tab | Next domain |
| Shift+Tab | Previous domain |
| Ctrl+0-9 | Jump to domain by index |

## Debug Output

The viewer outputs detailed logging to the console including:
- Domain loading and parsing status
- Mesh generation statistics (tiles, triangles, vertices)
- Physics body creation counts
- Bounds information (original and scaled)
- Camera position

Press F5 to toggle an on-screen debug panel showing:
- Current scale factors
- Mesh generation stats
- Geometry bounds
- Camera position

## Example Command Lines

```bash
# Full conversion workflow
cd /path/to/cpp-version/build/tools/scene_tool

# Convert ship1 with all domains
./scene_tool --convert-ship /path/to/uber/uberdroid/data/ship1.txt -o output/

# View the converted ship
./scene_tool --view output/ship.json

# Convert a single domain with explicit tiles path
./scene_tool --convert-domain /path/to/uber/uberdroid/ship1/xmapfile5.txt \
    -t /path/to/uber/uberdroid/data/tiles.txt \
    -o engineering.json

# View with physics debug disabled
./scene_tool --view output/domains/domain_0.json --no-physics
```

## Output Format

See [JSON_FORMATS.md](../../docs/JSON_FORMATS.md) for detailed JSON schema documentation.

### Ship JSON Structure

```json
{
  "version": "1.0",
  "name": "Ship Name",
  "crew": 50,
  "capacity": 100,
  "description": ["Line 1", "Line 2"],
  "domainPaths": ["domains/domain_0.json", "domains/domain_1.json"],
  "transporters": [...],
  "decks": {...},
  "metadata": {...}
}
```

### Domain JSON Structure

```json
{
  "version": "1.0",
  "levelNumber": 0,
  "name": "Bridge",
  "ambience": 1,
  "bounds": {...},
  "areas": [{
    "tiles": [...],
    "features": [...],
    "geometry": [...],
    "collision": {...}
  }],
  "waypoints": [...],
  "objects": {...},
  "spawns": [...],
  "metadata": {...}
}
```

## Dependencies

- **raylib** - 3D rendering and window management
- **box2d** - 2D physics for collision debug
- **tinyxml2** - Parsing geometry XML files
- **nlohmann/json** - JSON serialization

## Related Documentation

- [RENDERING_PIPELINE.md](../../docs/RENDERING_PIPELINE.md) - Shader and lighting system
- [ORIGINAL_FORMATS.md](../../docs/ORIGINAL_FORMATS.md) - Original file format documentation
- [JSON_FORMATS.md](../../docs/JSON_FORMATS.md) - JSON output specification
