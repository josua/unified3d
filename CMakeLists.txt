cmake_minimum_required(VERSION 3.25)

project(
    Unified3D
    VERSION 0.2.0
    DESCRIPTION "Headless 3D asset processing framework"
    LANGUAGES CXX
)

option(UNIFIED3D_BUILD_TESTS "Build Unified3D native tests" ON)
option(UNIFIED3D_BUILD_RUNTIME "Build the Unified3D native Runtime" ON)
option(UNIFIED3D_BUILD_ADAPTERS "Build native FBX and glTF adapters" ON)

if(UNIFIED3D_BUILD_RUNTIME AND NOT UNIFIED3D_BUILD_ADAPTERS)
    message(FATAL_ERROR "The native Runtime requires UNIFIED3D_BUILD_ADAPTERS=ON.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(unified3d_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
    else()
        target_compile_options(
            ${target}
            PRIVATE
                -Wall
                -Wextra
                -Wpedantic
                -Wconversion
                -Wsign-conversion
        )
    endif()
endfunction()

add_subdirectory(core)
add_subdirectory(operations)

if(UNIFIED3D_BUILD_ADAPTERS)
    include(cmake/Dependencies.cmake)
    add_subdirectory(adapters)
endif()

if(UNIFIED3D_BUILD_RUNTIME)
    if(NOT UNIFIED3D_BUILD_ADAPTERS)
        include(cmake/Dependencies.cmake)
    endif()
    add_subdirectory(runtime)
endif()

if(UNIFIED3D_BUILD_TESTS)
    include(CTest)
    add_subdirectory(tests/native)
endif()
