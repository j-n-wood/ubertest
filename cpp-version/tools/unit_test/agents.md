# Unit Test Tool - Agent Guide

This document provides context for AI agents working on the unit_test tool.

> **See also:** [docs/agents.md](../../docs/agents.md) for project-wide patterns and [docs/DATA_CONVERSION_GUIDE.md](../../docs/DATA_CONVERSION_GUIDE.md) for data conversion best practices.

## Overview

The unit_test tool is a standalone application for testing the Unit system. It loads unit definitions from JSON, creates physics-enabled instances, and provides interactive controls for testing behavior.

## Architecture

### Main Components

1. **main.cpp** - Entry point
   - Parses command line arguments
   - Initializes Raylib window
   - Creates TestScene
   - Runs main loop (update + render)
   - Handles cleanup

2. **test_scene.h/cpp** - Core scene logic
   - `TestScene` struct holds all runtime state
   - `testSceneInit()` - Creates physics world, initializes UnitManager
   - `testSceneLoadUnit()` - Loads definition and creates instance
   - `testSceneUpdate()` - Handles input, steps physics, updates camera
   - `testSceneRender()` - Draws 3D scene and 2D overlay
   - `testSceneHandleInput()` - Processes keyboard input for testing
   - `testSceneRenderInfo()` - Draws info overlay with unit details

### Key Dependencies

The tool uses the shared unit system library:

- `shared/units/unit_types.h` - Definition structs (SectionDefinition, UnitDefinition)
- `shared/units/unit_instance.h` - Runtime structs (SectionInstance, UnitInstance)
- `shared/units/unit_manager.h` - UnitManager class for loading and instantiation
- `shared/units/unit_json.h` - JSON parsing functions

### Physics Integration

- Box2D world created with zero gravity (top-down view)
- Each unit section can have a physics body
- Sections are connected via weld joints
- Collision filtering uses negative group indices to prevent self-collision

## Common Modifications

### Adding New Input Controls

In `testSceneHandleInput()` in test_scene.cpp:

```cpp
if (IsKeyPressed(KEY_X)) {
    // Your action here
    std::cout << "Action performed" << std::endl;
}
```

Update the info overlay in `testSceneRenderInfo()` to document the new control.

### Adding Debug Visualization

In `UnitManager::renderSectionDebug()` in unit_manager.cpp, add drawing code within the section loop. Use Raylib 3D drawing functions like `DrawLine3D`, `DrawCircle3D`, etc.

### Modifying Scene Defaults

Constants at the top of test_scene.cpp:

```cpp
constexpr float CAMERA_MOVE_SPEED = 10.0f;
constexpr float CAMERA_ROTATE_SPEED = 0.003f;
constexpr float IMPULSE_STRENGTH = 50.0f;
```

Window settings in main.cpp:

```cpp
constexpr int WINDOW_WIDTH = 1280;
constexpr int WINDOW_HEIGHT = 720;
```

## Data Flow

```
JSON File
    ↓
loadUnitDefinitionFromFile() [unit_json.cpp]
    ↓
UnitDefinition (cached in UnitManager)
    ↓
UnitManager::createInstance()
    ↓
UnitInstance with SectionInstances
    - Physics bodies created
    - Weld joints created
    - Models loaded
    ↓
testSceneUpdate() each frame
    - b2World_Step() advances physics
    - UnitManager::update() syncs transforms
    ↓
testSceneRender() each frame
    - UnitManager::renderAll() draws models
    - UnitManager::renderDebug() draws shapes/joints
```

## Testing Scenarios

1. **Basic loading** - Load test_simple.json, verify model renders
2. **Physics** - Press Space to apply impulse, observe movement
3. **Joints** - Load test_multi.json, verify sections stay attached
4. **Deconstruction** - Press B to break all joints, verify sections separate
5. **Individual break** - Press 1-9 to break specific sections
6. **Reset** - Press R to respawn unit after deconstruction
7. **Collision filtering** - Verify overlapping sections don't explode apart on spawn

## Build Integration

The tool is added to the build via root CMakeLists.txt:

```cmake
add_subdirectory(tools/unit_test)
```

Its own CMakeLists.txt links against:
- raylib
- box2d
- nlohmann_json::nlohmann_json
- Shared sources from cmake/SharedSources.cmake

## Related Files

- `docs/unit_system.md` - Full unit system design documentation
- `shared/units/` - Shared unit system library
- `assets/units/` - Sample unit definition JSON files
- `assets/models/` - GLTF models referenced by unit definitions
