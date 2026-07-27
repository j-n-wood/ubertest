cmake_minimum_required(VERSION 3.20)

# This CMakeLists.txt is designed to be included from the parent project.
# To build, run from cpp-version directory:
#   cmake -B build
#   cmake --build build --target unit_test

# Guard against standalone build
if(NOT DEFINED CPP_VERSION_ASSETS_DIR)
    message(FATAL_ERROR
        "unit_test must be built from the parent project.\n"
        "Run cmake from the cpp-version directory:\n"
        "  cd /path/to/cpp-version\n"
        "  cmake -B build\n"
        "  cmake --build build --target unit_test"
    )
endif()

project(unit_test CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(TOOL_SOURCES
    main.cpp
    test_scene.cpp
)

add_executable(${PROJECT_NAME}
    ${TOOL_SOURCES}
    ${SHARED_SOURCES}
    ${LEVEL_SHARED_SOURCES}   # level_renderer.cpp etc. — door/charger renderers in
                              # SHARED_SOURCES call freeLevelRenderData (defined there)
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
    box2d
    nlohmann_json::nlohmann_json
    tinyxml2   # tmx_loader.cpp (LEVEL_SHARED_SOURCES) includes <tinyxml2.h>
)

add_platform_libraries(${PROJECT_NAME})

# Copy shaders and textures to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/textures
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/textures
)

# Copy models to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/models
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/models
)

# Copy unit definitions to build directory
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/units
)

# If units directory exists, copy it
if(EXISTS ${CPP_VERSION_ASSETS_DIR}/units)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CPP_VERSION_ASSETS_DIR}/units
        $<TARGET_FILE_DIR:${PROJECT_NAME}>/units
    )
endif()
