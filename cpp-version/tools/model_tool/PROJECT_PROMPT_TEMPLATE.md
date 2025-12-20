# C++ CLI Tool Project Template

Use this prompt as a starting point when building command-line tools in modern C++.

---

## Project Requirements

Build a C++ command-line tool that [describe primary function].

### Language and Standards

- Use **C++23**
- Prefer modern C++ idioms over C-style code:
  - Use `std::string` and `std::string_view` instead of `char[]` arrays and C string functions (`strcpy`, `strncpy`, `strcmp`, `strcat`, `sprintf`, etc.)
  - Use `std::filesystem` (aliased as `fs`) for all path manipulation instead of manual string parsing
  - Use `std::vector`, `std::array`, and other STL containers instead of raw arrays where appropriate
  - Use `struct` without `typedef` (C++ style)
  - Use `{}` initialization instead of `{0}` for aggregates containing non-POD types
  - Use `std::format` or `std::print` instead of `printf`/`sprintf` where possible
  - Use range-based algorithms with projections where cleaner

### String Handling

- All internal string storage should use `std::string`
- Use `std::string_view` for function parameters that only read strings
- Convert to C strings (`.c_str()`) only at API boundaries when calling C libraries
- Use `std::ranges::to<std::string>()` for range-to-string conversions
- For case-insensitive comparisons, use range views:
  ```cpp
  auto to_lower(std::string_view s) -> std::string {
      return s | std::views::transform([](char c) {
          return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }) | std::ranges::to<std::string>();
  }
  ```
- Extract reusable string utilities into separate `string_utils.h` / `string_utils.cpp` files

### Path and File Operations

- Use `std::filesystem::path` for all file path operations
- Leverage built-in methods instead of manual parsing:
  - `path.filename()` - extract filename from path
  - `path.stem()` - filename without extension
  - `path.extension()` - file extension (includes leading dot)
  - `path.parent_path()` - directory containing the file
  - `path.replace_extension()` - change file extension
  - `fs::exists()`, `fs::is_directory()`, `fs::is_regular_file()` - file type checks
  - `fs::create_directories()` - recursive directory creation
  - `fs::directory_iterator` - iterate directory contents
- Always use `std::error_code` overloads to avoid exceptions for expected failures
- When a function accepts paths, prefer `const fs::path&` parameters

### Command-Line Argument Parsing

- Parse arguments in a dedicated `parse_args()` function
- Store parsed values in an application state struct with default member initializers
- Support both short (`-o`) and long (`--output`) option forms where appropriate
- Implement `--help` / `-h` to display usage information
- Use `std::string_view` for argument comparisons
- Use `std::from_chars` for numeric parsing (faster, no exceptions)
- Pattern for argument parsing:
  ```cpp
  void parse_args(std::span<char*> args, AppConfig& config) {
      for (size_t i = 1; i < args.size(); i++) {
          std::string_view arg = args[i];
          if (arg == "--option" && i + 1 < args.size()) {
              config.option_value = args[++i];
          } else if (arg == "--flag") {
              config.flag_enabled = true;
          } else if (arg == "--help" || arg == "-h") {
              print_usage();
              std::exit(0);
          }
      }
  }
  ```

### Output and Error Handling

- Use `std::print` / `std::println` (C++23) for output:
  ```cpp
  std::println("Processing: {}", filename);
  std::println(stderr, "Error: Cannot open '{}'", path.string());
  ```
- Return meaningful exit codes: `0` for success, non-zero for errors
- Provide clear, actionable error messages that include:
  - What operation failed
  - The relevant file path or input value
  - The underlying error (e.g., `ec.message()` from `std::error_code`)
- Validate inputs early and fail fast with descriptive messages
- Consider using `std::expected<T, E>` (C++23) for functions that can fail:
  ```cpp
  auto load_file(const fs::path& path) -> std::expected<FileData, std::string> {
      if (!fs::exists(path)) {
          return std::unexpected(std::format("File not found: {}", path.string()));
      }
      // ...
  }
  ```

### Project Structure

```
project/
├── CMakeLists.txt
├── main.cpp           # Entry point, argument parsing, main flow
├── string_utils.h     # String utility declarations
├── string_utils.cpp   # String utility implementations
├── [feature].h        # Feature-specific declarations
└── [feature].cpp      # Feature-specific implementations
```

### CMake Configuration

```cmake
cmake_minimum_required(VERSION 3.25)
project(tool_name CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(${PROJECT_NAME}
    main.cpp
    string_utils.cpp
    # additional source files...
)

# Link libraries as needed
# target_link_libraries(${PROJECT_NAME} PRIVATE some_library)
```

### Code Style Guidelines

- Use `static` for file-local functions, or prefer anonymous namespaces
- Use `[[nodiscard]]` on functions where ignoring the return value is likely a bug
- Use `constexpr` where possible
- Group related functions together with comment headers:
  ```cpp
  //------------------------------------------------------------------------------
  // Section Name
  //------------------------------------------------------------------------------
  ```
- Prefer early returns for error conditions
- Keep functions focused and reasonably sized
- Use descriptive variable names
- Use `auto` for complex types, explicit types for simple ones

---

## Example Application State Structure

```cpp
struct AppConfig {
    // Input/output paths
    fs::path input_path;
    fs::path output_path;

    // Operation modes
    bool verbose = false;
    bool dry_run = false;

    // Processing options
    float scale_factor = 1.0f;
    bool option_flag = false;
};
```

---

## C++23 Features to Prefer

| Instead of | Use |
|------------|-----|
| `printf` / `fprintf` | `std::print` / `std::println` |
| `sprintf` / `snprintf` | `std::format` |
| Raw loops for transforms | `std::views` + `std::ranges::to` |
| `std::optional` + error codes | `std::expected<T, E>` |
| `const char*` params | `std::string_view` |
| `int argc, char** argv` | `std::span<char*>` |
| Iterator pairs | Ranges |

---

## Checklist

- [ ] All string storage uses `std::string`
- [ ] All path operations use `std::filesystem`
- [ ] No C string functions (`strcpy`, `strcmp`, `strcat`, `sprintf`, `strrchr`, etc.)
- [ ] No fixed-size `char[]` buffers for strings
- [ ] Argument parsing uses `std::string_view` comparisons
- [ ] Numeric conversions use `std::from_chars` or `std::stof()` / `std::stoi()`
- [ ] Output uses `std::print` / `std::format` where possible
- [ ] Errors go to stderr with clear messages
- [ ] Exit codes indicate success/failure
- [ ] `--help` option implemented
- [ ] String utilities in separate files
- [ ] CMake configured for C++23
- [ ] Consider `std::expected` for fallible operations
- [ ] Use `[[nodiscard]]` appropriately
