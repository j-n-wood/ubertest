# C++ Workspace - Agent Guidelines

This workspace contains a multi-project C++ codebase with shared code infrastructure. Use this document to understand the project organization, build system, and coding conventions.

## Project Overview

A C++ workspace for a top-down game engine with supporting tools, built with Raylib for graphics and Box2D for physics.

### Workspace Structure

```
cpp-version/
├── CMakeLists.txt              # Workspace orchestrator (builds all projects)
├── cmake/
│   ├── Dependencies.cmake      # Shared dependency fetching (raylib, box2d, googletest)
│   └── SharedSources.cmake     # Defines SHARED_SOURCES, SHARED_INCLUDE_DIR
├── shared/                     # Common code for all projects
│   ├── lighting/
│   │   ├── light.h             # Light struct, MAX_LIGHTS, LIGHT_* constants
│   │   └── light.cpp           # create_light() implementation
│   ├── rendering/
│   │   ├── scene_renderer.h/cpp    # Lighting shader setup + apply to models
│   │   ├── texture_manager.h/cpp   # Central GPU texture owner (enum slots, gTextures())
│   │   ├── sprite_animation.h      # Sprite-sheet animation config (sourceRect per frame)
│   │   ├── geometry_mesh.cpp       # Hand-built mesh construction/upload
│   │   └── texture_loader.h/cpp    # Legacy index-keyed texture cache (tools only)
│   ├── units/
│   │   ├── unit_instance.h/cpp # Unit instance management, BodyUserData fields
│   │   ├── unit_manager.h/cpp  # Unit manager, body user data setup, collision categories
│   │   ├── unit_json.h/cpp     # Unit JSON serialization
│   │   ├── combat_state.h/cpp  # Combat state, damage model, property helpers
│   │   └── weapon.h/cpp        # Weapon definitions, fire/cooldown
│   ├── combat/
│   │   └── projectile_manager.h/cpp  # Projectile Box2D bodies, contact events
│   ├── effects/
│   │   └── effect_manager.h/cpp  # Explosions: animated billboard + area damage over time
│   ├── particles/
│   │   └── particle_manager.h/cpp  # SoA additive-billboard particles (render-only)
│   ├── physics/
│   │   └── body_user_data.h    # BodyTag enum, BodyUserData struct, collision categories
│   ├── model_convert/
│   │   ├── asc_loader.h/cpp    # MilkShape ASCII format loader
│   │   ├── mdl_loader.h/cpp    # Half-Life MDL format loader
│   │   ├── gltf_export.h/cpp   # GLTF 2.0 exporter
│   │   ├── gltf_bounds.h/cpp   # GLTF bounds calculation
│   │   └── gltf_skeletal_export.h/cpp  # Skeletal animation export
│   └── utils/
│       ├── string_utils.h      # to_lower(), has_extension()
│       └── string_utils.cpp
├── src/                        # Main game application
│   ├── main.cpp                # Entry point, window init, game loop
│   ├── game.h/cpp              # Central Game struct
│   ├── physics/                # Box2D wrapper
│   ├── graphics/               # Raylib rendering (uses shared/lighting)
│   ├── entities/               # Entity system
│   ├── generation/             # Procedural generation
│   └── input/                  # Input handling
├── assets/                     # Shared assets for all projects
│   ├── models/                 # GLTF/GLB model files
│   ├── textures/               # Texture files
│   ├── shaders/                # GLSL shaders (lighting.vs/fs)
│   └── units/                  # Unit definition files
├── tests/                      # GoogleTest unit tests
│   ├── CMakeLists.txt          # Test configuration
│   └── sanity_test.cpp         # Basic sanity tests
└── tools/
    ├── model_tool/             # Model viewer and converter tool
    │   ├── CMakeLists.txt
    │   ├── main.cpp
    │   └── AGENTS.md           # Tool-specific documentation
    ├── unit_test/              # Interactive unit testing tool
    │   ├── CMakeLists.txt
    │   ├── main.cpp
    │   └── test_scene.cpp
    └── droid_tool/             # Droid unit generation tool
        ├── CMakeLists.txt
        ├── main.cpp
        ├── droidclass_parser.h/cpp    # Droid class parsing
        ├── renderobject_parser.h/cpp  # Render object parsing
        └── unit_generator.h/cpp       # Unit definition generation
```

## Build Environment

### Tool Paths (macOS)

| Tool | Path |
|------|------|
| cmake | `/opt/homebrew/bin/cmake` |
| ninja | `/opt/homebrew/bin/ninja` |
| clang++ | `/usr/bin/clang++` |
| git | `/usr/bin/git` |

These are not in the default shell PATH for non-interactive shells. Use full paths when invoking from scripts or agents.

### Project Root Layout

The `cpp-version/` directory sits inside a larger project root:

```
test_project/                    # Project root
├── cpp-version/                 # This workspace (C++ game engine)
├── uber/uberdroid/              # Original game data (legacy format)
│   ├── data/                    # tiles.txt, droidclasses.txt, renderobjects.txt, etc.
│   ├── ship0/ .. ship7/         # Per-ship level data (xmapfile*.txt, level XMLs)
│   └── models/                  # Original model files
├── tiled/                       # Tiled map editor files
│   └── Paradroid.maps           # Parsed map data
└── FreedroidClassic/            # Reference implementation
```

Tests use `TEST_PROJECT_ROOT` (compile definition) to locate files in `uber/` and `tiled/`.

## Build System

### Building All Projects

```bash
cd cpp-version
/opt/homebrew/bin/cmake -B build -DCMAKE_BUILD_TYPE=Release
/opt/homebrew/bin/cmake --build build --parallel
```

This builds:
- `build/topdown_game` - Main game executable
- `build/tools/model_tool/model_tool` - Model viewer and converter
- `build/tools/unit_test/unit_test` - Interactive unit testing tool
- `build/tools/droid_tool/droid_tool` - Droid unit generator
- `build/tests/run_tests` - GoogleTest test runner

### CMake Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `VENDOR_DEPENDENCIES` | Fetch deps via FetchContent | `ON` |
| `ENABLE_BOX2D` | Include Box2D physics library | `ON` |
| `CPP_VERSION_ASSETS_DIR` | Path to shared assets | `${CMAKE_SOURCE_DIR}/assets` |
| `SHARED_SOURCES` | List of shared source files | Set by SharedSources.cmake |
| `SHARED_INCLUDE_DIR` | Include path for shared headers | `${CMAKE_SOURCE_DIR}/shared` |

### Dependencies

- **Raylib 5.5** - Always fetched (all projects need graphics)
- **Box2D v3.0** - Conditionally fetched when `ENABLE_BOX2D=ON` (main game and tests)
- **nlohmann/json v3.11.3** - JSON parsing and serialization
- **tinygltf v2.9.3** - glTF model format support (header-only)
- **GoogleTest v1.15.2** - Unit testing framework

## Code Organization

### Shared Code (`shared/`)

Common code that can be used by any project in the workspace.

**lighting/light.h** - Light definitions for the standard shader:
```cpp
#include "lighting/light.h"
// Provides: Light struct, MAX_LIGHTS, LIGHT_DIRECTIONAL, LIGHT_POINT
// Function: create_light(type, position, target, color, shader, index)
```

**utils/string_utils.h** - String utilities:
```cpp
#include "utils/string_utils.h"
// Provides: to_lower(string), has_extension(path, ".ext")
```

### Adding Shared Code to a Project

In any project's CMakeLists.txt:
```cmake
add_executable(${PROJECT_NAME}
    your_sources.cpp
    ${SHARED_SOURCES}
)
target_include_directories(${PROJECT_NAME} PRIVATE ${SHARED_INCLUDE_DIR})
```

### Asset Paths

- Main game: Assets copied to `build/assets/`
- Tools: Relevant assets copied to tool's build directory (e.g., `build/tools/model_tool/shaders/`)

Use `${CPP_VERSION_ASSETS_DIR}` in CMake for consistent asset path references.

## Adding New Tools

To create a new tool in `tools/`:

1. Create `tools/new_tool/CMakeLists.txt`:
```cmake
project(new_tool CXX)

set(TOOL_SOURCES
    main.cpp
    # ... other sources
)

add_executable(${PROJECT_NAME} ${TOOL_SOURCES} ${SHARED_SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
add_platform_libraries(${PROJECT_NAME})

# Copy required assets
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)
```

2. Add to root `CMakeLists.txt`:
```cmake
add_subdirectory(tools/new_tool)
```

## C++ Coding Guidelines

See [tools/model_tool/PROJECT_PROMPT_TEMPLATE.md](tools/model_tool/PROJECT_PROMPT_TEMPLATE.md) for detailed C++23 guidelines.

### Key Conventions

- **Standard**: C++23
- **Strings**: Use `std::string` internally, `std::string_view` for parameters, `.c_str()` at C API boundaries only
- **Paths**: Use `std::filesystem::path` for all file operations
- **Output**: Prefer `std::print`/`std::println` over `printf`
- **Error handling**: Consider `std::expected<T, E>` for fallible operations
- **Initialization**: Use `{}` not `{0}` for aggregates with non-POD members

### Style

- Use `static` or anonymous namespaces for file-local functions
- Use `[[nodiscard]]` where ignoring return value is likely a bug
- Group related functions with comment headers:
  ```cpp
  //------------------------------------------------------------------------------
  // Section Name
  //------------------------------------------------------------------------------
  ```
- Prefer early returns for error conditions

### Avoid

- C string functions: `strcpy`, `strcmp`, `strcat`, `sprintf`, `strrchr`
- Fixed-size char arrays: `char buffer[256]`
- Raw `printf` family (use `std::print` or `std::format`)
- `typedef struct` (use `struct` directly)

## Gameplay Design Patterns

These three rules apply to **all gameplay code** — combat, AI, spawning, projectiles, etc. See [docs/gameplay_implementation_plan.md](docs/gameplay_implementation_plan.md#design-patterns) for the full rationale.

1. **Use raylib types** — `Vector2`, `Vector3`, `Color` for members and function parameters. Use raymath functions (`Vector2Normalize`, `Vector2Scale`, `Vector2Distance`, etc.) instead of manual float arithmetic.

2. **Use Box2D directly** — systems that need physics use Box2D API calls directly. Tests create a `b2World` and step the simulation. Don't write manual physics simulation or abstract Box2D behind wrapper interfaces.

3. **Single source of truth** — the unit instance collection is authoritative for all positions, orientations, and combat state. Access units via `BodyUserData` pointers on Box2D bodies during contact events. Don't build intermediate data structures that duplicate data from authoritative sources.

All Box2D bodies carry a `BodyUserData` struct (defined in `shared/physics/body_user_data.h`) that identifies what they are. See [docs/unit_system.md](docs/unit_system.md#body-user-data) for tag definitions and [docs/unit_system.md](docs/unit_system.md#collision-filtering) for the category bit table.

### Droid Property Access

Unit definitions store gameplay data in a typed `DroidProperties` struct (defined in `unit_types.h`). Access fields directly — no map lookups or variant casts:

```cpp
float armour = unit->definition->properties.armour;
int weaponId = unit->definition->properties.weapon;  // -1 = unarmed
```

## Testing

### Strategy

The project uses **GoogleTest** for unit testing. Tests are located in the `tests/` directory and integrated with CMake's CTest. Tests link `box2d` and `raylib` so gameplay systems can be tested with real physics.

### Running Tests

```bash
# Build and run all tests
/opt/homebrew/bin/cmake --build build --target run_tests
./build/tests/run_tests

# Run specific test
./build/tests/run_tests --gtest_filter=SanityTest.*

# Run weapon/projectile tests only
./build/tests/run_tests --gtest_filter="Projectile*:Weapon*"
```

### Writing Tests

Add new test files to `tests/CMakeLists.txt`:

```cmake
add_executable(run_tests
    sanity_test.cpp
    your_new_test.cpp  # Add new test files here
)
```

Test file structure:

```cpp
#include <gtest/gtest.h>

TEST(TestSuiteName, TestName) {
    EXPECT_EQ(expected, actual);
    ASSERT_TRUE(condition);  // Fails immediately if false
}
```

### Gameplay Tests with Physics

Tests that need physics create a lightweight `b2World` (zero gravity) and step it with controlled dt values — no real-time clock needed:

```cpp
class MyTestFixture : public ::testing::Test {
protected:
    b2WorldId worldId = b2_nullWorldId;
    void SetUp() override {
        b2WorldDef worldDef = b2DefaultWorldDef();
        worldDef.gravity = {0.0f, 0.0f};
        worldId = b2CreateWorld(&worldDef);
    }
    void TearDown() override { b2DestroyWorld(worldId); }
    void step(float dt) { b2World_Step(worldId, dt, 4); }
};
```

Contact events must be processed per-step — they are only valid for the step in which they occur.

### Test Categories

- **Unit tests** (`tests/`): Fast, isolated tests for shared code and utilities
- **Gameplay tests** (`tests/`): Physics-enabled tests with Box2D worlds (combat, projectiles, weapons)
- **Integration tests**: Add to `tests/` with dependencies on shared sources
- **Interactive tools** (`tools/unit_test/`): Visual/interactive testing with Raylib window

### Adding Tests for Shared Code

To test code from `shared/`, link the sources in `tests/CMakeLists.txt`:

```cmake
add_executable(run_tests
    sanity_test.cpp
    ${SHARED_SOURCES}  # Include shared sources for testing
)

target_link_libraries(run_tests PRIVATE
    GTest::gtest_main
    raylib
    box2d
    nlohmann_json::nlohmann_json  # Add dependencies as needed
)
```

## Main Game Architecture

### Game Loop (`src/game.cpp` — `game_update_gameplay` / `game_render_gameplay`)

`main.cpp` owns the window + a scoped `TextureManager` (`unique_ptr`, `reset()` before
`CloseWindow`), and drives the active page (`GamePage`) each frame: `update(dt)` then
`render()`.

**Update** — input (`input_update`, mouse aim via `transfer_update`), then debug-key toggles,
then the **simulation block** guarded by `if (!paused)` with `simDt = slowMotion ? dt*0.1 : dt`,
in this order: AI update → player fire → `b2World_Step` → AI collision response →
doors → chargers → unit visibility → projectiles (update/sync/contacts/cleanup) → **effects**
(area damage) → **particles** → reap dead → `UnitManager::update` (syncs transforms + flushes
realtime damage) → game clock → score/alert. Outside the block: console/lift proximity, camera
follow.

**Render** (`game_render_gameplay`) is a separate, read-only pass: `BeginMode3D` → tiles →
doors → chargers → units → additive billboard blocks (projectiles, explosions, particles) →
`EndMode3D` → 2D HUD/debug. It issues **no** simulation side effects.

### Simulation vs rendering layers

State-owning **managers** step in the sim block; **rendering reads their state read-only** in the
render pass. `shared/` managers never call raylib draw APIs — they expose data (`getEffects()`,
`getProjectiles()`, `renderData()` spans) and the game draws it. Don't mutate game state in the
render pass. Transient visuals (projectiles, explosions, particles) all render as additive
`DrawBillboardPro` billboards; rlgl auto-batches same-texture quads into ~1 draw call, so "many
billboards" is cheap as long as a system shares one texture (`docs/effects.md`, `docs/textures.md`).

### Per-level physics worlds

Each level owns a retained `b2WorldId` in `game->levelWorlds[]`; only the active level's world is
stepped and `game->physics.world_id` aliases it. Level switch repoints `physics.world_id` and
re-`init`s the world-bound managers (doors/chargers/effects). **Teardown order is load-bearing:**
body-owning managers are destroyed **before** `b2DestroyWorld`, and all GPU textures freed before
`CloseWindow` (the `TextureManager` reset in `main`). See `docs/levels.md`.

### Textures, sprites, effects, particles

- **`TextureManager`** (`shared/rendering/texture_manager.h`) is the single GPU-texture owner —
  enum-indexed slots via `gTextures()`, loaded once, freed on teardown. `docs/textures.md`.
- **`SpriteAnimation`** animates a sprite **sheet** (`sourceRect(age,…)`, one texture bind,
  per-instance `age` cursor). Used by weapon-3 blasts and explosions.
- **`EffectManager`** (`shared/effects/`) — world effects (explosions: animated billboard + 1/r
  area damage over time). **`ParticleManager`** (`shared/particles/`) — render-only, SoA,
  additive billboards; `game_spawn_explosion()` fires both on death. `docs/effects.md`.
- **Damage:** projectiles apply immediately; continuous sources accumulate and flush on a 0.1 s
  tick (`combat_state` realtime-damage, driven from `UnitManager::update`).

### Input Gating by Test Mode

Input processing and player movement are guarded by `!game->testConfig.enabled`. Debug keys (0–6) and rotation torque application are **not** guarded. When debugging input issues, check whether `testConfig.enabled` is unexpectedly `true`.

### Unit Physics — Two Levels

Units have physics at **two separate levels**:

- **Unit-level `collisionRadius`** (in unit JSON root) — used by `UnitManager::createInstance` to create the Box2D body's circle shape. This determines mass, inertia, and collision detection for the live unit. **Must be > 0** or the body will have zero mass/inertia and won't respond to forces or torques.
- **Section-level `physics`** (in each section's JSON) — used only for **debris** when a unit is dismantled. Not used for the live unit body.

A `collisionRadius` of 0 silently creates a non-functional physics body. The code now logs a warning and applies a default minimum of 0.2.

### Coordinate Mapping

- **Physics (Box2D 2D)**: X = right, Y = forward/into screen
- **World (Raylib 3D)**: X = right, Y = up (height), Z = into screen
- Mapping: Physics X → World X, Physics Y → World Z
- Camera: top-down, `camera.up = {0, 0, -1}`, positioned at `{playerX, cameraHeight, playerZ}`
- Rotation: physics CCW angle is negated for `DrawModelEx` (`-rotation * RAD2DEG`)

### Key Constants (`src/game.cpp`)

| Constant | Value | Purpose |
|----------|-------|---------|
| `MOVEMENT_FORCE` | 7.0 | Force applied per frame from WASD input |
| `MAX_TORQUE` | 100.0 | Clamp for rotation PD controller |
| Default `linearDamping` | 4.0 | Top-down friction simulation |
| Default `angularDamping` | 8.0 | Rotation resistance |

### Debugging Checklist

When player input doesn't work:

1. **Check `testConfig.enabled`** — gates all input processing
2. **Check `collisionRadius`** in the unit JSON — zero means zero mass/inertia
3. **Check `playerUnit` is non-null** — `game_spawn_player` logs errors on failure
4. **Check `b2Body_IsValid`** — body creation can fail silently
5. **Check spawn position** — player may be trapped in level collision geometry

## Project-Specific Documentation

- **Main Game**: See [README.md](README.md) for game architecture
- **Model Tool**: See [tools/model_tool/AGENTS.md](tools/model_tool/AGENTS.md) for ASC/GLTF conversion details
- **Entity System**: See [docs/entity_system.md](docs/entity_system.md) for entity/physics design
- **Unit System**: See [docs/unit_system.md](docs/unit_system.md) for unit definitions, combat state, body user data, collision filtering, projectile lifecycle
- **Projectile System**: See [docs/projectile_system.md](docs/projectile_system.md) for Box2D projectile refactor design, contact events, API reference
- **Weapons**: See [docs/weapons.md](docs/weapons.md) for the weapon table, firing (player/AI), projectile rendering, and per-weapon physics radius
- **Effects & particles**: See [docs/effects.md](docs/effects.md) for explosions, the realtime-damage model, and the particle system
- **Textures & sprites**: See [docs/textures.md](docs/textures.md) for the central TextureManager and sprite-sheet animation
- **Levels**: See [docs/levels.md](docs/levels.md) for per-level Box2D worlds and level switching
- **Pages (presentation layer)**: See [docs/pages.md](docs/pages.md) for the `PageManager` stack and `Page` interface (deferred push/pop, activate/deactivate, current screens)
- **Gameplay Plan**: See [docs/gameplay_implementation_plan.md](docs/gameplay_implementation_plan.md) for staged implementation — **read [Design Patterns](docs/gameplay_implementation_plan.md#design-patterns) section first**
- **Data Conversion**: See [docs/agents.md](docs/agents.md) for conversion tool guidance, coordinate systems, winding order
