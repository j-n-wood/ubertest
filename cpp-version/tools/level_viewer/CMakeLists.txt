project(level_viewer CXX)

set(TOOL_SOURCES
    main.cpp
    viewer_state.cpp
    camera_controller.cpp
)

set(LEVEL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/level/tmx_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/tileset_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/level_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/tile_properties_loader.cpp
)

set(RENDER_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/rendering/scene_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/texture_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
)

add_executable(${PROJECT_NAME}
    ${TOOL_SOURCES}
    ${LEVEL_SHARED_SOURCES}
    ${RENDER_SHARED_SOURCES}
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    raylib
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
