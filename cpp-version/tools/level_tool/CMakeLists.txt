project(level_tool CXX)

set(TOOL_SOURCES
    main.cpp
    paradroid_parser.cpp
    tmx_writer.cpp
)

set(LEVEL_TOOL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
)

add_executable(${PROJECT_NAME} ${TOOL_SOURCES} ${LEVEL_TOOL_SHARED_SOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SHARED_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} PRIVATE tinyxml2)
add_platform_libraries(${PROJECT_NAME})
