project(incremental_viewer CXX)

set(TOOL_SOURCES
    main.cpp
    viewer.cpp
)

# Shared sources needed for rendering
set(VIEWER_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/scene_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/tile_mesh.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/texture_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
)

# Scene conversion sources (parsing and JSON)
set(SCENE_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
)

add_executable(${PROJECT_NAME}
    ${TOOL_SOURCES}
    ${VIEWER_SHARED_SOURCES}
    ${SCENE_CONVERT_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib nlohmann_json::nlohmann_json tinyxml2)
add_platform_libraries(${PROJECT_NAME})

# Copy shaders, textures, and models from shared assets
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/textures
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/textures
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/models
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/assets/models)
