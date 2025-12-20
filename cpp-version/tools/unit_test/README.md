# Unit Test Tool

A visual testing and inspection tool for the Unit system. Load unit definitions from JSON files, view their 3D models, test physics behavior, and experiment with deconstruction.

## Purpose

This tool allows you to:

- **Visualize units** - See how unit sections are rendered with their GLTF models
- **Test physics** - Apply impulses, observe joint behavior, verify collision filtering
- **Test deconstruction** - Break joints individually or all at once, watch sections simulate independently
- **Debug** - Toggle wireframe physics shapes and joint visualizations

## Building

**Important:** This tool must be built from the parent `cpp-version` directory, not from within the `tools/unit_test` folder.

```bash
# From the cpp-version directory (NOT from tools/unit_test)
cd /path/to/cpp-version

# Configure (first time only)
cmake -B build

# Build the unit_test tool
cmake --build build --target unit_test

# The executable is located at:
# build/tools/unit_test/unit_test
```

If you try to run `cmake -B build` from within the `tools/unit_test` directory, you will get an error directing you to build from the parent project instead.

## Usage

```bash
./unit_test <unit_definition.json>
```

### Examples

```bash
# Test a simple single-section unit
./unit_test assets/units/test_simple.json

# Test a multi-section unit with hierarchy
./unit_test assets/units/test_multi.json

# Test breakable joints
./unit_test assets/units/test_breakable.json
```

## Controls

| Key | Action |
|-----|--------|
| `WASD` | Move camera horizontally |
| `Q/E` | Move camera down/up |
| `Right Mouse` | Look around (hold and drag) |
| `Scroll` | Zoom in/out |
| `Space` | Apply random impulse to root body |
| `B` | Break all joints (total deconstruction) |
| `X` | Explode (break joints + explosive impulse outward) |
| `1-9` | Break joint of specific section (by index) |
| `R` | Reset unit (respawn) |
| `P` | Pause/resume physics simulation |
| `F1` | Toggle debug visualization |
| `I` | Toggle info overlay |
| `ESC` | Exit |

## Info Overlay

Press `I` to toggle the info overlay which displays:

- Current unit name and ID
- Section count and attachment status
- Root body position
- Numbered list of all sections (for use with `1-9` keys)

## Debug Visualization

Press `F1` to toggle debug rendering which shows:

- Physics shape outlines (green = attached, red = detached)
- Joint connections between sections
- Grid and ground plane

## File Structure

```
tools/unit_test/
├── CMakeLists.txt    # Build configuration
├── main.cpp          # Entry point, CLI parsing, main loop
├── test_scene.h      # Scene state and function declarations
├── test_scene.cpp    # Scene logic, input handling, rendering
├── README.md         # This file
└── agents.md         # AI agent guidance
```

## Dependencies

- **raylib** - Window, input, 3D rendering, GLTF loading
- **box2d** - 2D physics simulation
- **nlohmann/json** - JSON parsing for unit definitions
- **shared/units/** - Unit system library (shared with main game)

## Sample Unit Definitions

Located in `assets/units/`:

| File | Description |
|------|-------------|
| `test_simple.json` | Single section with circle physics shape |
| `test_multi.json` | Multi-section hierarchy (hull, turret, barrel, wings) |
| `test_breakable.json` | Sections with joint break thresholds |

## Coordinate System

- **Physics**: 2D simulation on X/Y plane
- **Rendering**: 3D with X/Z as ground plane, Y as height
- Section `height` property controls vertical offset in 3D view
