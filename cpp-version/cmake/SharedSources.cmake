# SharedSources.cmake
# Define shared source files for inclusion by projects

set(SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/scene_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/texture_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/rendering/geometry_mesh.cpp
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_instance.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_json.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/combat_state.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/weapon.cpp
    ${CMAKE_SOURCE_DIR}/shared/combat/projectile_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/ai/ai_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/door_manager.cpp
)

# Level loading sources (TMX/TSX parsing, tile rendering, collision, spawning)
set(LEVEL_SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/level/tmx_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/tileset_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/level_renderer.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/tile_properties_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/level/spawn_config.cpp
)

# Model conversion sources (ASC->GLTF, MDL->GLTF) - does not require box2d
set(MODEL_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/model_convert/asc_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/model_convert/gltf_export.cpp
    ${CMAKE_SOURCE_DIR}/shared/model_convert/gltf_bounds.cpp
    ${CMAKE_SOURCE_DIR}/shared/model_convert/mdl_loader.cpp
    ${CMAKE_SOURCE_DIR}/shared/model_convert/gltf_skeletal_export.cpp
)

# Scene conversion sources (ship/domain parsing, JSON serialization)
set(SCENE_CONVERT_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/ship_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/domain_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/archetile_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/geometry_xml_parser.cpp
    ${CMAKE_SOURCE_DIR}/shared/scene_convert/scene_json.cpp
)

set(SHARED_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/shared)
