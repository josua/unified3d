add_library(
    unified3d-core
    STATIC
        src/analysis/analysis_record.cpp
        src/geometry/buffers.cpp
)

add_library(Unified3D::Core ALIAS unified3d-core)

target_include_directories(
    unified3d-core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(unified3d-core PUBLIC cxx_std_20)
unified3d_enable_warnings(unified3d-core)

set_target_properties(
    unified3d-core
    PROPERTIES
        OUTPUT_NAME unified3d-core
        POSITION_INDEPENDENT_CODE ON
)
