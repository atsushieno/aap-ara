# ARA SDK 2.3.0 is pinned by external/ARA_SDK. Applications may supply an
# equivalent checkout; no network access occurs during CMake configuration.
include_guard(GLOBAL)
get_filename_component(_aap_ara_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ARA_SDK_ROOT "${_aap_ara_root}/external/ARA_SDK" CACHE PATH "ARA SDK checkout")
if(NOT EXISTS "${ARA_SDK_ROOT}/ARA_API/ARAInterface.h")
    message(FATAL_ERROR "ARA SDK missing: run git submodule update --init --recursive, or set ARA_SDK_ROOT")
endif()
if(NOT TARGET ARA_PlugIn_Library)
    add_subdirectory("${ARA_SDK_ROOT}/ARA_Library" "${CMAKE_CURRENT_BINARY_DIR}/ARA_SDK" EXCLUDE_FROM_ALL)
endif()
