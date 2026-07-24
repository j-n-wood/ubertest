cmake_minimum_required(VERSION 3.20)
project(cpp_version_workspace CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(VENDOR_DEPENDENCIES "Fetch dependencies instead of using system" ON)

# Shared configuration
set(CPP_VERSION_ASSETS_DIR "${CMAKE_SOURCE_DIR}/assets")
include(cmake/SharedSources.cmake)
include(cmake/Dependencies.cmake)

# Platform libraries helper function
function(add_platform_libraries target)
    if(WIN32)
        target_link_libraries(${target} PRIVATE winmm)
    elseif(APPLE)
        target_link_libraries(${target} PRIVATE "-framework IOKit" "-framework Cocoa" "-framework OpenGL")
    elseif(UNIX)
        target_link_libraries(${target} PRIVATE m pthread dl GL X11)
    endif()
endfunction()

#------------------------------------------------------------------------------
# Main Game (topdown_game)
#------------------------------------------------------------------------------
file(GLOB_RECURSE GAME_SOURCES "src/*.cpp")
add_executable(topdown_game ${GAME_SOURCES} ${SHARED_SOURCES} ${LEVEL_SHARED_SOURCES})
target_include_directories(topdown_game PRIVATE ${CMAKE_SOURCE_DIR}/src ${SHARED_INCLUDE_DIR} ${RAYGUI_INCLUDE_DIR})
target_link_libraries(topdown_game PRIVATE raylib box2d nlohmann_json::nlohmann_json tinyxml2)
add_platform_libraries(topdown_game)

# Default the game to the source assets directory (absolute), so it loads AND saves the
# real game-data files regardless of launch cwd. This makes runtime edits (e.g. the
# console droid-library editor's "Save to JSON") persist to source and survive rebuilds.
# Overridable at runtime with --asset-path. (Previously assets were copied next to the
# binary and the game read that throwaway copy — edits went nowhere and were clobbered by
# the copy on the next build.)
target_compile_definitions(topdown_game PRIVATE GAME_SOURCE_ASSETS_DIR="${CPP_VERSION_ASSETS_DIR}")

#------------------------------------------------------------------------------
# Tools
#------------------------------------------------------------------------------
add_subdirectory(tools/model_tool)
add_subdirectory(tools/unit_test)
add_subdirectory(tools/droid_tool)
add_subdirectory(tools/incremental_viewer)
add_subdirectory(tools/level_tool)
add_subdirectory(tools/level_viewer)

#------------------------------------------------------------------------------
# Tests
#------------------------------------------------------------------------------
enable_testing()
add_subdirectory(tests)
