# tests/CMakeLists.txt
# GoogleTest configuration for unit tests

# Scene conversion sources needed for tests
set(SCENE_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
)

# Level tool sources needed for tests
set(LEVEL_TOOL_SOURCES
    ${CMAKE_SOURCE_DIR}/tools/level_tool/paradroid_parser.cpp
)

add_executable(run_tests
    sanity_test.cpp
    tile_conversion_test.cpp
    paradroid_parser_test.cpp
    combat_state_test.cpp
    weapon_test.cpp
    spawn_test.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_instance.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/combat_state.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/weapon.cpp
    ${CMAKE_SOURCE_DIR}/shared/combat/projectile_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/spawn_config.cpp
    ${SCENE_CONVERT_SOURCES}
    ${LEVEL_TOOL_SOURCES}
)

target_link_libraries(run_tests PRIVATE
    GTest::gtest_main
    raylib
    box2d
    nlohmann_json::nlohmann_json
    tinyxml2
)

target_include_directories(run_tests PRIVATE
    ${SHARED_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/shared
    ${CMAKE_SOURCE_DIR}/tools/level_tool
)

include(GoogleTest)
gtest_discover_tests(run_tests)
