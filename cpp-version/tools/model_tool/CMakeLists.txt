project(model_tool CXX)

set(TOOL_SOURCES
    main.cpp
    asc_loader.cpp
    gltf_export.cpp
)

add_executable(${PROJECT_NAME} ${TOOL_SOURCES} ${SHARED_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib)
add_platform_libraries(${PROJECT_NAME})

# Copy shaders from shared assets
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR}/shaders
    $<TARGET_FILE_DIR:${PROJECT_NAME}>/shaders)
