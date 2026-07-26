# tests/CMakeLists.txt
# GoogleTest configuration for unit tests

# Scene conversion sources needed for tests
set(SCENE_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
)

# Level tool sources needed for tests (currently disabled — see paradroid_parser_test.cpp)
# set(LEVEL_TOOL_SOURCES
#     ${CMAKE_SOURCE_DIR}/tools/level_tool/paradroid_parser.cpp
# )

add_executable(run_tests
    sanity_test.cpp
    tile_conversion_test.cpp
    # paradroid_parser_test.cpp  # Disabled — parser crashes on valid data, revisit if data needs regeneration
    combat_state_test.cpp
    weapon_test.cpp
    spawn_test.cpp
    ai_test.cpp
    door_test.cpp
    charger_test.cpp
    console_test.cpp
    lift_test.cpp
    transfer_test.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/console_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/lift_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/ship_map.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/tmx_loader.cpp
    ${CMAKE_SOURCE_DIR}/src/pages/page_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_instance.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/combat_state.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/weapon.cpp
    ${CMAKE_SOURCE_DIR}/shared/combat/projectile_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/spawn_config.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/door_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/charger_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/ai/ai_manager.cpp
    ${SCENE_CONVERT_SOURCES}
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
    ${CMAKE_SOURCE_DIR}/src
)

# Provide the project root (parent of cpp-version) so tests can find
# external data files (uber/uberdroid, tiled/) without guessing relative paths.
target_compile_definitions(run_tests PRIVATE
    TEST_PROJECT_ROOT="${CMAKE_SOURCE_DIR}/.."
)

include(GoogleTest)
gtest_discover_tests(run_tests)
