# Dependencies.cmake
# Shared dependency fetching for all projects

include(FetchContent)

option(ENABLE_BOX2D "Enable Box2D physics library" ON)

if(VENDOR_DEPENDENCIES)
    # Raylib - always needed
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG 5.5 GIT_SHALLOW TRUE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(raylib)

    # Box2D - only when enabled (main game needs it, tools may not)
    if(ENABLE_BOX2D)
        FetchContent_Declare(box2d
            GIT_REPOSITORY https://github.com/erincatto/box2d.git
            GIT_TAG v3.0.0 GIT_SHALLOW TRUE)
        set(BOX2D_BUILD_TESTBED OFF CACHE BOOL "" FORCE)
        set(BOX2D_BUILD_UNIT_TESTS OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(box2d)
    endif()

    # nlohmann/json - for unit definition parsing and serialization
    FetchContent_Declare(json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(json)

    # tinygltf - for skeletal GLTF export (header-only, uses nlohmann/json)
    FetchContent_Declare(tinygltf
        GIT_REPOSITORY https://github.com/syoyo/tinygltf.git
        GIT_TAG v2.9.3
        GIT_SHALLOW TRUE)
    # Disable tinygltf's bundled json to use our nlohmann/json
    set(TINYGLTF_HEADER_ONLY ON CACHE BOOL "" FORCE)
    set(TINYGLTF_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tinygltf)

    # tinyxml2 - for parsing geometry XML files
    FetchContent_Declare(tinyxml2
        GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
        GIT_TAG 10.0.0
        GIT_SHALLOW TRUE)
    set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(tinyxml2)

    # GoogleTest - for unit testing
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
else()
    find_package(raylib REQUIRED)
    if(ENABLE_BOX2D)
        find_package(box2d REQUIRED)
    endif()
    find_package(nlohmann_json REQUIRED)
endif()
