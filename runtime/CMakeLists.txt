add_library(
    unified3d-runtime-lib
    STATIC
        src/json/analysis_codec.cpp
        src/runtime.cpp
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
    PRIVATE
        nlohmann_json::nlohmann_json
)

target_compile_features(unified3d-runtime-lib PUBLIC cxx_std_20)
unified3d_enable_warnings(unified3d-runtime-lib)

add_executable(unified3d-runtime src/main.cpp)
target_link_libraries(unified3d-runtime PRIVATE Unified3D::Runtime)
target_compile_features(unified3d-runtime PRIVATE cxx_std_20)
unified3d_enable_warnings(unified3d-runtime)

set_target_properties(
    unified3d-runtime-lib
    PROPERTIES
        OUTPUT_NAME unified3d-runtime
        POSITION_INDEPENDENT_CODE ON
)
