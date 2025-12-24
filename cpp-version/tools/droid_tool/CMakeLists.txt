project(droid_tool CXX)

# Only include shared sources that don't require box2d
set(DROID_TOOL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
)

set(TOOL_SOURCES
    main.cpp
    droidclass_parser.cpp
    renderobject_parser.cpp
    unit_generator.cpp
)

add_executable(${PROJECT_NAME} ${TOOL_SOURCES} ${DROID_TOOL_SHARED_SOURCES} ${MODEL_CONVERT_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/shared/model_convert
    ${tinygltf_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE raylib nlohmann_json::nlohmann_json)
add_platform_libraries(${PROJECT_NAME})
