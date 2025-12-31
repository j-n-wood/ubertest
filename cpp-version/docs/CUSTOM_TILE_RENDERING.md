# Custom Tile Rendering

This document describes the custom tile rendering system for the level viewer, which extends the default tilemap rendering with per-tile material properties including bump mapping.

## Overview

The level viewer supports two rendering modes for 2D tile-based levels:

| Mode | Name | Description |
|------|------|-------------|
| 1 | **Tilemap** | Default mode. Uses diffuse atlas texture with flat normal map for all tiles. |
| 2 | **Custom Tiles** | Enhanced mode. Loads `tiles.json` to apply per-tile bump maps and material properties. |

## Rendering Modes

### Tilemap Mode (Default)

Standard tile rendering using the tileset atlas:
- Diffuse color from tileset atlas (`map_blocks.png`)
- Flat normal map for all tiles (`flat_normal.png`)
- Uniform specular properties across all tiles

### Custom Tiles Mode

Enhanced rendering with per-tile material customization:
- Diffuse color from tileset atlas (same as tilemap mode)
- Per-tile bump/normal maps from bump atlas (`bump_atlas.png`)
- Per-tile specular intensity
- Per-tile albedo color multiplier

Requires a `tiles.json` configuration file in the levels folder.

## Configuration File: tiles.json

### Location

Place `tiles.json` in the levels folder alongside TMX files:
```
assets/ships/ship1/levels/
├── level_0_maintenance.tmx
├── level_1_engineering.tmx
├── ...
├── default.tsx
├── map_blocks.png
└── tiles.json          <-- Configuration file
```

### Format

```json
{
  "version": 1,
  "bumpAtlas": {
    "texture": "bump_atlas.png",
    "tileWidth": 128,
    "tileHeight": 128,
    "columns": 8
  },
  "tiles": {
    "8": {
      "bumpTileIndex": 1,
      "specularIntensity": 0.5
    },
    "11": {
      "bumpTileIndex": 2,
      "specularIntensity": 0.8,
      "albedoMultiplier": [1.0, 0.95, 0.9]
    },
    "34": {
      "bumpTileIndex": 0,
      "specularIntensity": 0.3
    }
  },
  "defaults": {
    "bumpTileIndex": 0,
    "specularIntensity": 0.5,
    "albedoMultiplier": [1.0, 1.0, 1.0]
  }
}
```

### Schema

#### Root Object

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `version` | int | Yes | Schema version (currently 1) |
| `bumpAtlas` | object | Yes | Bump atlas texture configuration |
| `tiles` | object | No | Per-tile property overrides (keyed by tile ID string) |
| `defaults` | object | No | Default properties for tiles not specified |

#### bumpAtlas Object

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `texture` | string | Yes | Filename of bump atlas (relative to `assets/textures/`) |
| `tileWidth` | int | Yes | Width of each tile in pixels |
| `tileHeight` | int | Yes | Height of each tile in pixels |
| `columns` | int | Yes | Number of columns in the atlas grid |

#### Tile Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `bumpTileIndex` | int | 0 | Index into bump atlas (0 = flat normal) |
| `specularIntensity` | float | 0.5 | Specular highlight strength (0.0 - 1.0) |
| `albedoMultiplier` | float[3] | [1,1,1] | RGB multiplier for diffuse color |

## Bump Atlas

### Location

The bump atlas texture is stored at:
```
assets/textures/bump_atlas.png
```

### Structure

The bump atlas is a grid of normal map tiles:
- **Tile size**: 128x128 pixels
- **Layout**: 8 columns x 4 rows (1024x512 pixels total)
- **Index 0**: Reserved for flat normal map (RGB: 128, 128, 255)

### Generating the Atlas

Use the provided Python script to generate the atlas from individual bump map images:

```bash
cd cpp-version/tools/scripts

# Create and activate a virtual environment
python3 -m venv venv
source venv/bin/activate

# Install dependencies
pip install Pillow

# Run the generator
python make_bump_atlas.py

# Deactivate when done
deactivate
```

The script reads images from `assets/textures/bump/` and outputs:
- `assets/textures/bump_atlas.png` - The atlas image
- `assets/textures/bump_atlas_manifest.json` - Tile index mapping

### Manifest File

The manifest records which source image corresponds to each atlas index:

```json
{
  "tileSize": 128,
  "columns": 8,
  "rows": 4,
  "atlasWidth": 1024,
  "atlasHeight": 512,
  "tileCount": 24,
  "tiles": [
    {"index": 0, "name": "flat.png"},
    {"index": 1, "name": "N4BUMP.jpg"},
    {"index": 2, "name": "n1bump.jpg"},
    ...
  ]
}
```

## CLI Arguments

```
level_viewer [options]

Options:
  --render-mode <mode>   Set initial rendering mode
                         Values: tilemap, custom (default: tilemap)
```

Example:
```bash
./level_viewer --render-mode custom
```

## Runtime Controls

| Key | Action |
|-----|--------|
| `M` | Cycle render mode (Tilemap -> Custom Tiles -> Tilemap) |

Mode changes trigger:
1. Geometry regeneration (for UV coordinate updates)
2. Texture rebinding (bump atlas vs flat normal)
3. HUD update to show current mode

## Technical Implementation

### Geometry Generation

Custom tiles mode generates separate UV coordinates:
- `texcoords` (UV1): Diffuse atlas coordinates (same as tilemap mode)
- `texcoords2` (UV2): Bump atlas coordinates (based on `bumpTileIndex`)

### Shader Changes

The lighting shader uses the second UV channel for normal map sampling:

```glsl
// Vertex shader
in vec2 vertexTexCoord2;
out vec2 fragTexCoord2;

// Fragment shader
in vec2 fragTexCoord2;
vec3 normalMapSample = texture(texture2, fragTexCoord2).rgb;
```

### Fallback Behavior

If `tiles.json` is missing or invalid:
- Custom tiles mode falls back to tilemap behavior
- Warning logged to console
- All tiles use default properties (flat normal, standard specular)

## File Dependencies

| File | Purpose |
|------|---------|
| `assets/ships/ship1/levels/tiles.json` | Tile property definitions |
| `assets/textures/bump_atlas.png` | Normal map atlas |
| `assets/textures/bump_atlas_manifest.json` | Atlas tile index mapping |
| `assets/shaders/lighting.vs` | Vertex shader (modified for UV2) |
| `assets/shaders/lighting.fs` | Fragment shader (modified for UV2) |

## Example Workflow

1. **Generate bump atlas** (one-time setup):
   ```bash
   cd tools/scripts
   python3 -m venv venv && source venv/bin/activate
   pip install Pillow
   python make_bump_atlas.py
   deactivate
   ```

2. **Create tiles.json** in your levels folder:
   ```json
   {
     "version": 1,
     "bumpAtlas": {
       "texture": "bump_atlas.png",
       "tileWidth": 128,
       "tileHeight": 128,
       "columns": 8
     },
     "tiles": {
       "8": {"bumpTileIndex": 5},
       "34": {"bumpTileIndex": 3, "specularIntensity": 0.8}
     },
     "defaults": {
       "bumpTileIndex": 0,
       "specularIntensity": 0.5
     }
   }
   ```

3. **Run level viewer** in custom mode:
   ```bash
   ./level_viewer --render-mode custom
   ```

4. **Toggle modes** at runtime with `M` key.
