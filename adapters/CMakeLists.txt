option(UNIFIED3D_ENABLE_AUTODESK_FBX "Enable the Autodesk FBX SDK adapter" OFF)
set(
    UNIFIED3D_AUTODESK_FBX_SDK_ROOT
    "C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.10"
    CACHE PATH
    "Autodesk FBX SDK installation root"
)

add_library(unified3d-ufbx-object OBJECT "${ufbx_SOURCE_DIR}/ufbx.c")
set_source_files_properties("${ufbx_SOURCE_DIR}/ufbx.c" PROPERTIES LANGUAGE CXX)
target_include_directories(unified3d-ufbx-object PUBLIC "${ufbx_SOURCE_DIR}")
target_compile_features(unified3d-ufbx-object PRIVATE cxx_std_20)

add_library(unified3d-cgltf-object OBJECT src/gltf/cgltf_implementation.cpp)
target_include_directories(unified3d-cgltf-object PUBLIC "${cgltf_SOURCE_DIR}")
target_compile_features(unified3d-cgltf-object PRIVATE cxx_std_20)

add_library(
    unified3d-adapters
    STATIC
        src/adapter_registry.cpp
        src/fbx/ufbx_adapter.cpp
        src/fbx/glb_to_fbx_converter.cpp
        src/gltf/cgltf_adapter.cpp
        $<TARGET_OBJECTS:unified3d-ufbx-object>
        $<TARGET_OBJECTS:unified3d-cgltf-object>
)
add_library(Unified3D::Adapters ALIAS unified3d-adapters)

target_include_directories(
    unified3d-adapters
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        "${ufbx_SOURCE_DIR}"
        "${cgltf_SOURCE_DIR}"
)
target_link_libraries(unified3d-adapters PUBLIC Unified3D::Core)
target_compile_features(unified3d-adapters PUBLIC cxx_std_20)
unified3d_enable_warnings(unified3d-adapters)

if(UNIFIED3D_ENABLE_AUTODESK_FBX)
    if(NOT MSVC)
        message(FATAL_ERROR "The Autodesk FBX SDK backend requires an MSVC build on Windows.")
    endif()
    set(_fbx_include "${UNIFIED3D_AUTODESK_FBX_SDK_ROOT}/include")
    set(_fbx_lib_root "${UNIFIED3D_AUTODESK_FBX_SDK_ROOT}/lib/x64")
    if(NOT EXISTS "${_fbx_include}/fbxsdk.h")
        message(FATAL_ERROR "Autodesk FBX SDK headers not found under ${_fbx_include}")
    endif()
    target_sources(unified3d-adapters PRIVATE src/fbx/autodesk_fbx_adapter.cpp)
    target_include_directories(unified3d-adapters PRIVATE "${_fbx_include}")
    target_compile_definitions(
        unified3d-adapters
        PUBLIC UNIFIED3D_HAS_AUTODESK_FBX=1
        PRIVATE FBXSDK_SHARED
    )
    target_link_libraries(
        unified3d-adapters
        PRIVATE
            "$<$<CONFIG:Debug>:${_fbx_lib_root}/debug/libfbxsdk.lib>"
            "$<$<NOT:$<CONFIG:Debug>>:${_fbx_lib_root}/release/libfbxsdk.lib>"
    )
    set(
        UNIFIED3D_AUTODESK_FBX_RUNTIME_DLL
        "$<IF:$<CONFIG:Debug>,${_fbx_lib_root}/debug/libfbxsdk.dll,${_fbx_lib_root}/release/libfbxsdk.dll>"
        CACHE INTERNAL "Autodesk FBX SDK runtime DLL"
    )
endif()

set_target_properties(
    unified3d-adapters
    PROPERTIES
        OUTPUT_NAME unified3d-adapters
        POSITION_INDEPENDENT_CODE ON
)
