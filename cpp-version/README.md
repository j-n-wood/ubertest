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
│   ├── Dependencies.cmake      # FetchContent for raylib, box2d, googletest
│   └── SharedSources.cmake     # Shared code configuration
├── shared/                     # Common code for all projects
│   ├── lighting/               # Light struct and shader utilities
│   ├── rendering/              # Scene rendering utilities
│   ├── units/                  # Unit instance and manager
│   ├── model_convert/          # ASC/MDL loaders, GLTF export
│   └── utils/                  # String utilities
├── assets/                     # Shared assets
│   ├── models/                 # GLTF/GLB files
│   ├── textures/
│   ├── shaders/
│   └── units/                  # Unit definition files
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
├── tests/                      # GoogleTest unit tests
│   ├── CMakeLists.txt
│   └── sanity_test.cpp
├── tools/
│   ├── model_tool/             # Model viewer and ASC/MDL-to-GLTF converter
│   ├── unit_test/              # Interactive unit testing tool
│   └── droid_tool/             # Droid unit generation tool
├── docs/
│   └── entity_system.md        # Entity system documentation
└── build/
```

See [AGENTS.md](AGENTS.md) for workspace organization and coding guidelines.

## Asset organisation

It is generally expected that build will be invoked from the root folder (cpp-version). Tools and executables
shall have an asset-path argument to override the assets folder path. Otherwise ./assets is assumed.
If the provided or optional asset location does not exist, raise an error and exit.

This is to allow both tools and game executables to share input assets.

Output asset path will default to the same but can be overridden to an existing or new folder for tools
that produce output.

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
