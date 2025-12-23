project(model_tool CXX)

set(TOOL_SOURCES
    main.cpp
)

# Use shared model conversion sources + other shared utilities
set(MODEL_TOOL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/scene_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
)

add_executable(${PROJECT_NAME} ${TOOL_SOURCES} ${MODEL_TOOL_SHARED_SOURCES} ${MODEL_CONVERT_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/shared/model_convert
    ${tinygltf_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib nlohmann_json::nlohmann_json)
add_platform_libraries(${PROJECT_NAME})

# Copy shaders from shared assets
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)
