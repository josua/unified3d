find_package(Threads REQUIRED)

add_library(
    unified3d-runtime-lib
    STATIC
        src/json/analysis_codec.cpp
        src/resource.cpp
        src/runtime.cpp
        src/transports/named_pipe.cpp
)

add_library(Unified3D::Runtime ALIAS unified3d-runtime-lib)

target_include_directories(
    unified3d-runtime-lib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    unified3d-runtime-lib
    PUBLIC
        Unified3D::Core
        Unified3D::Operations
        Unified3D::Adapters
        Threads::Threads
    PRIVATE
        nlohmann_json::nlohmann_json
)

target_compile_features(unified3d-runtime-lib PUBLIC cxx_std_20)
unified3d_enable_warnings(unified3d-runtime-lib)

if(WIN32)
    target_link_libraries(unified3d-runtime-lib PRIVATE advapi32)
endif()

add_executable(unified3d-runtime src/main.cpp)
target_link_libraries(unified3d-runtime PRIVATE Unified3D::Runtime)
target_compile_features(unified3d-runtime PRIVATE cxx_std_20)
unified3d_enable_warnings(unified3d-runtime)

if(UNIFIED3D_ENABLE_AUTODESK_FBX)
    add_custom_command(
        TARGET unified3d-runtime
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${UNIFIED3D_AUTODESK_FBX_RUNTIME_DLL}"
            "$<TARGET_FILE_DIR:unified3d-runtime>"
    )
endif()

set_target_properties(
    unified3d-runtime-lib
    PROPERTIES
        OUTPUT_NAME unified3d-runtime
        POSITION_INDEPENDENT_CODE ON
)
