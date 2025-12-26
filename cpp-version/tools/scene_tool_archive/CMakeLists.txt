cmake_minimum_required(VERSION 3.20)

# Guard: Must be built from parent project
if(NOT DEFINED CPP_VERSION_ASSETS_DIR)
    message(FATAL_ERROR "scene_tool must be built from the parent project. Run cmake from the cpp-version directory.")
endif()

project(scene_tool CXX)

# Tool-specific sources
set(TOOL_SOURCES
    main.cpp
    scene_viewer.cpp
)

# Scene conversion sources (shared code)
set(SCENE_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/ship_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
)

# Rendering and utility sources
set(SCENE_TOOL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/rendering/scene_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
)

add_executable(${PROJECT_NAME}
    ${TOOL_SOURCES}
    ${SCENE_CONVERT_SOURCES}
    ${SCENE_TOOL_SHARED_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/shared/scene_convert
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
    box2d
    tinyxml2
    nlohmann_json::nlohmann_json
)

add_platform_libraries(${PROJECT_NAME})

# Copy shaders and textures post-build
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/textures
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/textures
)

# Create output directories and copy assets needed for viewing
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/output/domains
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/output/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/textures
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/output/textures
)
