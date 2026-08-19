add_library(
    unified3d-operations
    STATIC
        src/analysis/compare_analysis_records.cpp
)

add_library(Unified3D::Operations ALIAS unified3d-operations)

target_include_directories(
    unified3d-operations
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(unified3d-operations PUBLIC Unified3D::Core)
target_compile_features(unified3d-operations PUBLIC cxx_std_20)
unified3d_enable_warnings(unified3d-operations)

set_target_properties(
    unified3d-operations
    PROPERTIES
        OUTPUT_NAME unified3d-operations
        POSITION_INDEPENDENT_CODE ON
)
