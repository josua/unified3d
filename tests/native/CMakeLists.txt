add_executable(
    unified3d-native-tests
    analysis_record_tests.cpp
)

target_link_libraries(
    unified3d-native-tests
    PRIVATE
        Unified3D::Core
        Unified3D::Operations
)

target_compile_features(unified3d-native-tests PRIVATE cxx_std_20)
unified3d_enable_warnings(unified3d-native-tests)

add_test(NAME unified3d-native-tests COMMAND unified3d-native-tests)

if(UNIFIED3D_BUILD_RUNTIME)
    add_executable(
        unified3d-runtime-tests
        runtime_tests.cpp
    )

    target_link_libraries(
        unified3d-runtime-tests
        PRIVATE
            Unified3D::Runtime
            nlohmann_json::nlohmann_json
    )

    target_compile_features(unified3d-runtime-tests PRIVATE cxx_std_20)
    target_compile_definitions(
        unified3d-runtime-tests
        PRIVATE
            UNIFIED3D_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
    )
    unified3d_enable_warnings(unified3d-runtime-tests)

    if(UNIFIED3D_ENABLE_AUTODESK_FBX)
        add_custom_command(
            TARGET unified3d-runtime-tests
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${UNIFIED3D_AUTODESK_FBX_RUNTIME_DLL}"
                "$<TARGET_FILE_DIR:unified3d-runtime-tests>"
        )
    endif()

    add_test(NAME unified3d-runtime-tests COMMAND unified3d-runtime-tests)
endif()
