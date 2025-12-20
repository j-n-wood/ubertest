# Top-Down Game

A minimal C project for a top-down game using 2D physics with 3D graphics.

## Stack

- **Graphics**: Raylib 5.5
- **Physics**: Box2D v3.0 (C API)
- **Build**: CMake 3.20+
- **Language**: C11

## Project Structure

```
test_project/
├── CMakeLists.txt
├── cmake/
│   └── Dependencies.cmake      # FetchContent for raylib and box2d
├── assets/
│   ├── models/                 # GLTF/GLB files
│   ├── textures/
│   └── shaders/
├── src/
│   ├── main.c                  # Entry point, window init, game loop
│   ├── game.h/c                # Central Game struct and lifecycle
│   ├── physics/
│   │   └── physics_world.h/c   # Box2D wrapper (zero gravity top-down)
│   ├── graphics/
│   │   └── renderer.h/c        # Model loading and entity rendering
│   ├── generation/
│   │   └── procgen.h/c         # Procedural level generation
│   ├── entities/
│   │   └── entity.h/c          # Entity struct and physics sync
│   └── input/
│       └── input.h/c           # Input handling
├── docs/
│   └── entity_system.md        # Entity system documentation
└── build/
```

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
