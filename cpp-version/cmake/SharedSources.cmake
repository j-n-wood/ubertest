# SharedSources.cmake
# Define shared source files for inclusion by projects

set(SHARED_SOURCES
    ${CMAKE_SOURCE_DIR}/shared/lighting/light.cpp
    ${CMAKE_SOURCE_DIR}/shared/utils/string_utils.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_instance.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_manager.cpp
    ${CMAKE_SOURCE_DIR}/shared/units/unit_json.cpp
)

set(SHARED_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/shared)
