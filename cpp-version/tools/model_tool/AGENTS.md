# Model Tool - Debug Viewer & Converter

A C++ tool using Raylib for viewing and converting 3D models, including MilkShape 3D ASCII (.asc) and Half-Life MDL (.mdl) files to GLTF 2.0 format.

## Project Structure

```
cpp-version/tools/model_tool/
├── CMakeLists.txt       # Build configuration (fetches Raylib 5.5)
├── main.cpp             # CLI parsing, viewer mode, conversion mode
├── asc_loader.h/cpp     # MilkShape ASCII parser
├── gltf_export.h/cpp    # GLTF 2.0 exporter with texture handling
└── build/               # Build output directory
```

## Building

```bash
cd cpp-version/tools/model_tool
cmake -B build
cmake --build build
```

Raylib 5.5 is fetched automatically via CMake FetchContent.

## Usage

### Viewer Mode (Interactive)

```bash
./build/model_tool --model-a path/to/model.asc --model-b path/to/model.gltf
```

Four-panel display:
- Top-left: Procedural box (reference)
- Top-right: Procedural sphere (reference)
- Bottom-left: Model A
- Bottom-right: Model B

Controls:
- `0-5`: Debug visualization modes (normals, light direction, specular, etc.)
- `Space`: Toggle auto-rotate
- `Arrow keys`: Manual rotation
- `+/-`: Zoom
- `R`: Reset rotation

### Conversion Mode (Headless)

```bash
./build/model_tool --convert input.asc -o output/path/model.gltf --texture-path /path/to/textures
```

Options:
- `--convert <file>`: Input ASC file
- `-o, --output <file>`: Output GLTF path
- `--texture-path <dir>`: Fallback directory for texture files
- `--scale <factor>`: Scale factor (default: 0.0254 for inches to meters)
- `--swap-yz`: Enable Y/Z axis swap
- `--flip-winding`: Flip triangle winding order

## Coordinate Systems

### MilkShape 3D ASCII (.asc)
- **Axis convention**: Z-up (right-handed)
- **Units**: Typically inches
- **Triangle winding**: Clockwise (CW)

### Half-Life MDL (.mdl)
- **Axis convention**: Z-up, X-forward (right-handed)
- **Units**: Game units (typically treated as inches)
- **Triangle winding**: Clockwise (CW)
- **Vertex space**: Bone-local (vertices stored relative to their assigned bone)
- **Animation**: Absolute bone transforms per frame (not relative to rest pose)

### Raylib / GLTF
- **Axis convention**: Y-up, +Z-forward (right-handed)
- **Units**: Meters
- **Triangle winding**: Counter-clockwise (CCW)

### Transformations Applied

When loading ASC files, the following transforms can be applied via CLI options:

1. **Scale** (`--scale`): Default 0.0254 converts inches to meters
2. **Y/Z Swap** (`--swap-yz`): Converts Z-up to Y-up
   ```
   File: (x, y, z) → Memory: (x, z, y)
   ```
3. **Winding Flip** (`--flip-winding`): Converts CW to CCW triangles
   ```
   Indices: (v0, v1, v2) → (v0, v2, v1)
   ```

Note: By default these transforms are disabled. Enable them based on your source content.

### MDL Coordinate Axis Correction

When loading MDL files with `--swap-yz`, a multi-stage coordinate transformation is applied to convert from MDL's Z-up/X-forward system to glTF's Y-up/+Z-forward system:

**Stage 1: Basic Axis Swap**

Converts Z-up to Y-up for positions and quaternion rotations:
```
Position: (x, y, z) → (-y, z, -x)
Quaternion: (qx, qy, qz, qw) → (-qy, qz, -qx, qw)
```

**Stage 2: Vertex Space Conversion**

MDL stores vertices in bone-local space. Before export, vertices are transformed to model space using the bone hierarchy world matrices. This is required because glTF skinning expects vertices in model space with inverse bind matrices.

**Stage 3: Bind Pose Correction**

MDL animation frame 0 often differs from the bone rest pose. To ensure animations play correctly, the bind pose is updated from animation frame 0 before calculating inverse bind matrices.

**Stage 4: Forward Direction Correction (180° Y Rotation)**

After the axis swap, the model faces -Z instead of glTF's +Z forward convention. A 180° rotation around the Y axis is applied to correct this:

```
Vertices: (x, y, z) → (-x, y, -z)
Normals: (nx, ny, nz) → (-nx, ny, -nz)
Root bones: position (x, y, z) → (-x, y, -z), rotation *= Quaternion(0, 1, 0, 0)
Animation keyframes (root bones only): same transforms as root bones
```

This rotation is baked directly into the vertex data and root bone transforms because glTF skinned meshes ignore parent node transforms during animation.

## Texture Path Handling

### Source Path Resolution

ASC files contain Windows-style relative texture paths:
```
"..\..\textures\materials\mtl_orange.jpg"
```

The exporter resolves these using a two-stage fallback:

1. **Primary**: Resolve relative to ASC file directory
   - `source_dir + texture_path` (after converting `\` to `/`)

2. **Fallback** (`--texture-path`): Try progressively shorter paths
   - Given fallback `/project/textures` and path `../../textures/materials/file.jpg`
   - Tries: `textures/materials/file.jpg`, `materials/file.jpg`, `file.jpg`
   - Returns first match found

### Output Structure

Textures are placed in a `textures/` subfolder under the output directory:

```
output/
├── model.gltf           # References "textures/filename.jpg"
└── textures/
    ├── texture1.jpg
    └── texture2.jpg
```

GLTF image URIs use relative paths: `"textures/filename.jpg"`

### BMP to JPG Conversion

GLTF 2.0 does not support BMP format. BMP textures are automatically converted to JPG during export using Raylib's image functions.

- Input: `chrome1.bmp`
- Output: `textures/chrome1.jpg`
- GLTF URI: `"textures/chrome1.jpg"`

### Duplicate Handling

Before copying/converting textures, the exporter checks if the destination file already exists. This allows batch conversion of multiple models that share textures without redundant file operations.

## Materials

### ASC Material Properties

```
ambient[4]      // RGBA ambient color
diffuse[4]      // RGBA diffuse color
specular[4]     // RGBA specular color
emissive[4]     // RGBA emissive color
shininess       // Specular exponent (0-128)
transparency    // Opacity (1.0 = opaque)
"texture_path"  // Diffuse texture
```

### GLTF Export

Materials are exported with PBR metallic-roughness approximation plus `extras` preserving original Blinn-Phong data:

```json
{
  "pbrMetallicRoughness": {
    "baseColorFactor": [r, g, b, a],
    "baseColorTexture": { "index": 0 },
    "metallicFactor": 0.0,
    "roughnessFactor": 0.8
  },
  "extras": {
    "shaderHint": "blinn-phong",
    "originalFormat": "asc",
    "blinnPhong": {
      "diffuse": [r, g, b, a],
      "specular": [r, g, b, a],
      "shininess": 32.0
    }
  }
}
```

## Example Workflow

Convert all models from a legacy project:

```bash
# Single model
./build/model_tool --convert ../../uber/uberdroid/models/302.asc \
    -o ./converted/models/302.gltf \
    --texture-path ../../uber/uberdroid/textures \
    --swap-yz

# Result:
# ./converted/models/302.gltf
# ./converted/models/textures/mtl_orange.jpg
# ./converted/models/textures/chrome1.jpg
# ./converted/models/textures/grille1.jpg
```

View converted model alongside original:

```bash
./build/model_tool \
    --model-a ../../uber/uberdroid/models/302.asc \
    --model-b ./converted/models/302.gltf \
    --swap-yz
```
