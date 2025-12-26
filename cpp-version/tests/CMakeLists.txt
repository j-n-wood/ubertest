# tests/CMakeLists.txt
# GoogleTest configuration for unit tests

# Scene conversion sources needed for tests
set(SCENE_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
)

add_executable(run_tests
    sanity_test.cpp
    tile_conversion_test.cpp
    ${SCENE_CONVERT_SOURCES}
)

target_link_libraries(run_tests PRIVATE
    GTest::gtest_main
    raylib
    nlohmann_json::nlohmann_json
    tinyxml2
)

target_include_directories(run_tests PRIVATE
    ${SHARED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
)

include(GoogleTest)
gtest_discover_tests(run_tests)
