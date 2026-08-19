include(FetchContent)

set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
set(JSON_ImplicitConversions OFF CACHE INTERNAL "")

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
    ufbx
    GIT_REPOSITORY https://github.com/ufbx/ufbx.git
    GIT_TAG fcc5d6ba444cfd3eb80677dba5e37e493941abe5
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR unified3d-no-cmake
)

FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf.git
    GIT_TAG 360db1a95480fe102ae9c69b27c5d101167ff5ba
    GIT_SHALLOW TRUE
    GIT_SUBMODULES ""
    SOURCE_SUBDIR unified3d-no-cmake
)

FetchContent_MakeAvailable(nlohmann_json ufbx cgltf)
