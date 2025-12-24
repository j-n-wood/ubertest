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
│   │   ├── scene_renderer.h    # Scene rendering utilities
│   │   └── scene_renderer.cpp
│   ├── units/
│   │   ├── unit_instance.h/cpp # Unit instance management
│   │   ├── unit_manager.h/cpp  # Unit manager
│   │   └── unit_json.h/cpp     # Unit JSON serialization
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

## Build System

### Building All Projects

```bash
cd cpp-version
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
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
- **Box2D v3.0** - Conditionally fetched when `ENABLE_BOX2D=ON` (main game only)
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

## Testing

### Strategy

The project uses **GoogleTest** for unit testing. Tests are located in the `tests/` directory and integrated with CMake's CTest.

### Running Tests

```bash
# Build and run all tests
cmake --build build --target run_tests
ctest --test-dir build --output-on-failure

# Run test executable directly (more verbose output)
./build/tests/run_tests

# Run specific test
./build/tests/run_tests --gtest_filter=SanityTest.*
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

### Test Categories

- **Unit tests** (`tests/`): Fast, isolated tests for shared code and utilities
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
    nlohmann_json::nlohmann_json  # Add dependencies as needed
)
```

## Project-Specific Documentation

- **Main Game**: See [README.md](README.md) for game architecture
- **Model Tool**: See [tools/model_tool/AGENTS.md](tools/model_tool/AGENTS.md) for ASC/GLTF conversion details
- **Entity System**: See [docs/entity_system.md](docs/entity_system.md) for entity/physics design
