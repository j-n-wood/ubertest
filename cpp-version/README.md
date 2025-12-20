# Top-Down Game (C++ Version)

A C++ workspace for a top-down game using 2D physics with 3D graphics, plus supporting tools for asset conversion and validation.

## Stack

- **Graphics**: Raylib 5.5
- **Physics**: Box2D v3.0 (C API)
- **Build**: CMake 3.20+
- **Language**: C++23

## Project Structure

```
cpp-version/
├── CMakeLists.txt              # Workspace orchestrator
├── cmake/
│   ├── Dependencies.cmake      # FetchContent for raylib and box2d
│   └── SharedSources.cmake     # Shared code configuration
├── shared/                     # Common code for all projects
│   ├── lighting/               # Light struct and shader utilities
│   └── utils/                  # String utilities
├── assets/                     # Shared assets
│   ├── models/                 # GLTF/GLB files
│   ├── textures/
│   └── shaders/
├── src/                        # Main game
│   ├── main.cpp                # Entry point, window init, game loop
│   ├── game.h/cpp              # Central Game struct and lifecycle
│   ├── physics/
│   │   └── physics_world.h/cpp # Box2D wrapper (zero gravity top-down)
│   ├── graphics/
│   │   └── renderer.h/cpp      # Model loading and entity rendering
│   ├── generation/
│   │   └── procgen.h/cpp       # Procedural level generation
│   ├── entities/
│   │   └── entity.h/cpp        # Entity struct and physics sync
│   └── input/
│       └── input.h/cpp         # Input handling
├── tools/
│   └── model_tool/             # Model viewer and ASC-to-GLTF converter
├── docs/
│   └── entity_system.md        # Entity system documentation
└── build/
```

See [AGENTS.md](AGENTS.md) for workspace organization and coding guidelines.

## Build Commands

```bash
# Configure (fetches dependencies on first run)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --parallel

# Run
./build/topdown_game

# Debug build
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

## Controls

- **Arrow keys**: Move camera
- **ESC**: Quit

## Architecture

### Coordinate System

2D physics (Box2D) maps to 3D rendering (Raylib):

| Box2D | Raylib |
|-------|--------|
| X     | X      |
| Y     | Z      |
| —     | Y (height) |

### Entity Types

- **PLAYER**: Dynamic circle body, player-controlled
- **ENEMY**: Dynamic circle body, AI-controlled
- **OBSTACLE**: Static box body, collision geometry
- **PROP**: No physics, decorative objects

### Key Design Decisions

1. Zero gravity physics with linear damping for top-down friction
2. Fixed entity pool (1024 max) to avoid runtime allocation
3. Explicit physics-to-graphics sync each frame
4. GLTF native model loading via Raylib
5. FetchContent for dependency management
