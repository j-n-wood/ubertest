# Top-Down Game (C++ Version)

A C++ workspace for a top-down game using 2D physics with 3D graphics, plus supporting tools for asset conversion and validation.

## Stack

- **Graphics**: Raylib 5.5
- **Physics**: Box2D v3.0 (C API)
- **Build**: CMake 3.20+
- **Language**: C++23

## Movement model

Units move via a Box2D **`b2MotorJoint`** that drives each body toward a desired position and facing (anchored to a static world-origin body, so joint offsets equal world coordinates). Because the drive is a solver constraint bounded by max force/torque, collisions negotiate with it instead of knocking units off course.

Player and AI units are **simulated identically** — the game concept is that the player takes remote control of a unit, so player input simply replaces the AI's target through the same `unit_set_move_target()` entry point and the same tuning ([`shared/units/movement_tuning.h`](shared/units/movement_tuning.h)). See Design Pattern 4 in [docs/gameplay_implementation_plan.md](docs/gameplay_implementation_plan.md).

## Project Structure

```
cpp-version/
├── CMakeLists.txt              # Workspace orchestrator
├── cmake/
│   ├── Dependencies.cmake      # FetchContent for raylib, box2d, googletest
│   └── SharedSources.cmake     # Shared code configuration
├── shared/                     # Common code for all projects
│   ├── lighting/               # Light struct and shader utilities
│   ├── rendering/              # Scene rendering utilities
│   ├── units/                  # Unit instance and manager
│   ├── level/                  # TMX/TSX level loading and rendering
│   ├── model_convert/          # ASC/MDL loaders, GLTF export
│   └── utils/                  # String utilities
├── assets/                     # Shared assets
│   ├── models/                 # GLTF/GLB files
│   ├── textures/               # Textures and bump atlases
│   ├── shaders/                # GLSL shaders
│   ├── units/                  # Unit definition JSON files
│   └── ships/                  # Ship level data
│       └── ship1/levels/       # TMX levels, tileset, tiles.json
├── src/                        # Main game
│   ├── main.cpp                # Entry point, window init, game loop
│   ├── game.h/cpp              # Central Game struct and lifecycle
│   ├── physics/
│   │   └── physics_world.h/cpp # Box2D wrapper (zero gravity top-down)
│   └── input/
│       └── input.h/cpp         # Input handling (WASD/arrows + mouse)
├── tests/                      # GoogleTest unit tests
│   ├── CMakeLists.txt
│   └── sanity_test.cpp
├── tools/
│   ├── model_tool/             # Model viewer and ASC/MDL-to-GLTF converter
│   ├── unit_test/              # Interactive unit testing tool
│   ├── droid_tool/             # Droid unit generation tool
│   ├── level_viewer/           # Level viewer with CustomTiles rendering
│   └── incremental_viewer/     # Incremental model viewer
├── docs/
│   ├── GAME.md                 # Game design documentation
│   └── IMPLEMENTATION_PLAN.md  # Implementation plan
└── build/
```

See [AGENTS.md](AGENTS.md) for workspace organization and coding guidelines.

## Build Commands

```bash
# Configure (from cpp-version directory, fetches dependencies on first run)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build all targets
cmake --build build --parallel

# Build only the game
cmake --build build --target topdown_game

# Debug build
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

## Run Commands

```bash
# Run the game (from cpp-version directory)
./build/topdown_game

# Or from build directory
cd build && ./topdown_game
```

The game loads assets from `./assets` relative to working directory. Run from the `cpp-version` or `build` directory where assets are copied.

## Game Features

- **Levels**: 16 TMX levels from ship1 loaded at startup
- **Rendering**: CustomTiles mode with bump-mapped tiles and specular lighting
- **Physics**: Box2D collision bodies generated from tile collision data
- **Player**: droid_class_0 unit with physics-based movement

## Controls

- **WASD / Arrow keys**: Move player
- **Mouse**: Aim direction
- **0-6**: Debug visualization modes
- **C**: Show collision shapes
- **U**: Show unit physics debug
- **N**: Toggle normal mapping
- **ESC**: Quit

## Architecture

### Coordinate System

2D physics (Box2D) maps to 3D rendering (Raylib):

| Box2D | Raylib |
|-------|--------|
| X     | X      |
| Y     | Z      |
| —     | Y (height) |

### Game Systems

- **SceneRenderer**: Shared lighting and material system with bump mapping
- **UnitManager**: Loads unit definitions from JSON, creates physics instances
- **Level System**: TMX loading, tileset UV calculation, collision generation
- **Input**: Movement forces applied to player physics body, mouse aim via torque

### Key Design Decisions

1. Zero gravity physics with linear damping for top-down friction
2. All levels loaded upfront for fast transitions
3. CustomTiles rendering with per-tile bump maps from tiles.json
4. Collision rectangles merged for optimization (88 bodies from 190 tiles)
5. Shared code between game and level_viewer tool
6. FetchContent for dependency management
7. **Unit definitions are hand-edited JSON.** `assets/units/droid_class_*.json` were originally
   generated by `droid_tool` from the uberdroid data, but that tool is retired — the JSON is now
   the source of truth and is edited directly. (E.g. weapon `fireOffset` is a facing-relative
   `[x=lateral, y=forward, z=height]`; twin weapons mirror the lateral `x` so barrels straddle
   the centreline — class 20 sets `fireOffset: [0.3, 0, 0]`.)

## Testing

Unit tests use **GoogleTest** and are located in the `tests/` directory.

```bash
# Build and run tests
cmake --build build --target run_tests
ctest --test-dir build --output-on-failure

# Or run directly
./build/tests/run_tests
```

See [AGENTS.md](AGENTS.md) for detailed testing guidelines.

## Tools

```bash
# Level viewer - preview levels with debug visualization
./build/tools/level_viewer/level_viewer assets/ships/ship1/levels

# Model tool - view and convert models
./build/tools/model_tool/model_tool

# Droid tool - generate unit definitions
./build/tools/droid_tool/droid_tool
```

> **Note:** `droid_tool` was a one-time converter from the original uberdroid data. Unit
> definitions in `assets/units/*.json` are now the source of truth and are **edited directly**
> — we do not plan to re-run the converter, so fixes/tuning go in the JSON (and don't rely on
> regenerating). See "Unit definitions" under Key Design Decisions.
