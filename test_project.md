# Raylib + Box2D Top-Down Game Project

A minimal C project for a top-down game using 2D physics with 3D graphics.

## Stack

- **Graphics**: Raylib 5.5
- **Physics**: Box2D v3.0 (C API)
- **Build**: CMake 3.20+
- **Language**: C11

## Directory Structure

```
my-game/
├── CMakeLists.txt
├── cmake/
│   └── Dependencies.cmake
├── assets/
│   ├── models/          # GLTF/GLB files
│   ├── textures/
│   └── shaders/
├── src/
│   ├── main.c
│   ├── game.h
│   ├── game.c
│   ├── physics/
│   │   ├── physics_world.h
│   │   └── physics_world.c
│   ├── graphics/
│   │   ├── renderer.h
│   │   └── renderer.c
│   ├── generation/
│   │   ├── procgen.h
│   │   └── procgen.c
│   └── entities/
│       ├── entity.h
│       └── entity.c
└── build/
```

## CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.20)
project(topdown_game C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VENDOR_DEPENDENCIES "Fetch dependencies instead of using system" ON)

include(cmake/Dependencies.cmake)

file(GLOB_RECURSE SOURCES "src/*.c")
add_executable(${PROJECT_NAME} ${SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(${PROJECT_NAME} PRIVATE raylib box2d)

# Copy assets post-build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets)

# Platform libs
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE winmm)
elseif(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
elseif(UNIX)
    target_link_libraries(${PROJECT_NAME} PRIVATE m pthread dl GL X11)
endif()
```

## cmake/Dependencies.cmake

```cmake
include(FetchContent)

if(VENDOR_DEPENDENCIES)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5 GIT_SHALLOW TRUE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)

    FetchContent_Declare(box2d
        GIT_REPOSITORY https://github.com/erincatto/box2d.git
        GIT_TAG v3.0.0 GIT_SHALLOW TRUE)
    set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
    set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(box2d)
else()
    find_package(raylib REQUIRED)
    find_package(box2d REQUIRED)
endif()
```

## Core Architecture

### main.c
Entry point: initializes window (1280x720), creates Game struct, runs update/render loop at 60 FPS.

### game.h / game.c
Central Game struct containing:
- `PhysicsWorld physics` - Box2D world wrapper
- `Renderer renderer` - graphics state
- `Entity entities[MAX_ENTITIES]` - entity pool (MAX_ENTITIES = 1024)
- `Camera3D camera` - top-down camera

Camera setup for top-down view:
```c
camera.position = (Vector3){0, 50, 0};  // Above looking down
camera.target = (Vector3){0, 0, 0};
camera.up = (Vector3){0, 0, -1};        // Z- is "forward" on screen
```

### physics/physics_world.h
Box2D v3 wrapper with zero gravity (top-down). Key functions:
- `physics_world_init/step/destroy`
- `physics_create_dynamic_circle(world, pos, radius)`
- `physics_create_static_box(world, pos, w, h)`
- `physics_body_apply_force/get_position/get_angle`

Uses `linearDamping = 4.0f` on dynamic bodies to simulate top-down friction.

### entities/entity.h
Entity struct with:
- `EntityType type` - PLAYER, ENEMY, OBSTACLE, PROP
- `Vector3 position` - 3D position (Y is up)
- `float rotation` - Y-axis rotation in radians
- `Model model` - Raylib model (GLTF)
- `PhysicsBody physics` - optional physics attachment

`entity_sync_from_physics()` maps 2D physics (X,Y) to 3D rendering (X,Z).

### graphics/renderer.h
Handles model rendering. Key function:
- `renderer_load_gltf(path)` - wraps `LoadModel()` for GLTF/GLB files
- `renderer_draw_entity()` - draws with position/rotation from entity

### generation/procgen.h
Procedural content generation entry point:
- `procgen_generate_level(game)` - populates entities
- `procgen_spawn_entity(game, type, pos, model_path)` - creates entity with appropriate physics body

## Coordinate Mapping

| 2D Physics (Box2D) | 3D Rendering (Raylib) |
|--------------------|-----------------------|
| X | X |
| Y | Z |
| — | Y (height, usually 0) |

## Build Commands

```bash
# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run
./build/topdown_game

# Debug build
cmake -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
```

## Key Design Decisions

1. **Zero gravity physics** - Top-down games don't need gravity; use linear damping for "friction"
2. **Entity pool** - Fixed array avoids dynamic allocation in game loop
3. **Physics-graphics sync** - Explicit sync step each frame maps 2D physics to 3D transforms
4. **GLTF native** - Raylib's `LoadModel()` handles GLTF/GLB directly
5. **FetchContent deps** - No manual dependency management; CMake fetches on first build

## Extension Points

- **PCG**: Expand `procgen.c` with noise functions, room generation, BSP dungeons
- **ECS**: Replace entity array with sparse set or archetype storage if needed
- **Shaders**: Add custom shaders in `assets/shaders/`, load with `LoadShader()`
- **Audio**: Raylib includes audio; add `InitAudioDevice()` in main