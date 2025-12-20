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
add_executable(topdown_game ${GAME_SOURCES} ${SHARED_SOURCES})
target_include_directories(topdown_game PRIVATE ${CMAKE_SOURCE_DIR}/src ${SHARED_INCLUDE_DIR})
target_link_libraries(topdown_game PRIVATE raylib box2d)
add_platform_libraries(topdown_game)

# Copy assets post-build
add_custom_command(TARGET topdown_game POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CPP_VERSION_ASSETS_DIR} $<TARGET_FILE_DIR:topdown_game>/assets)

#------------------------------------------------------------------------------
# Tools
#------------------------------------------------------------------------------
add_subdirectory(tools/model_tool)
