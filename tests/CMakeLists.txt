cmake_minimum_required(VERSION 3.19)
project(ARAAAPTests LANGUAGES C CXX)
include(../cmake/ARASDK.cmake)
set(AAP_CORE_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../aap-core/include" CACHE PATH "aap-core headers")
add_executable(araaap-tests araaap-tests.cpp ../androidaudioplugin-ara/src/main/cpp/sdk/araaap.cpp)
target_compile_features(araaap-tests PRIVATE cxx_std_17)
target_include_directories(araaap-tests PRIVATE ../include "${AAP_CORE_INCLUDE_DIR}")
target_link_libraries(araaap-tests PRIVATE ARA_PlugIn_Library)
if(UNIX)
    target_link_libraries(araaap-tests PRIVATE pthread)
endif()
enable_testing()
add_test(NAME araaap-sdk-roundtrip COMMAND araaap-tests)

# Verify reuse of the SDK's existing example implementation, rather than only
# controllers written specifically for this integration.
include("${ARA_SDK_ROOT}/ARA_API/ARA_Version.cmake")
set(ARA_UPSTREAM_EXAMPLES "${ARA_SDK_ROOT}/ARA_Examples")
add_executable(araaap-upstream-tests
    araaap-upstream-tests.cpp
    existing-ara-host.cpp
    ../androidaudioplugin-ara/src/main/cpp/sdk/araaap.cpp
    "${ARA_UPSTREAM_EXAMPLES}/TestPlugIn/ARATestDocumentController.cpp"
    "${ARA_UPSTREAM_EXAMPLES}/TestPlugIn/ARATestAudioSource.cpp"
    "${ARA_UPSTREAM_EXAMPLES}/TestPlugIn/ARATestPlaybackRenderer.cpp"
    "${ARA_UPSTREAM_EXAMPLES}/TestPlugIn/TestAnalysis.cpp"
    "${ARA_UPSTREAM_EXAMPLES}/TestPlugIn/TestPersistency.cpp")
target_compile_features(araaap-upstream-tests PRIVATE cxx_std_17)
target_include_directories(araaap-upstream-tests PRIVATE ../include "${AAP_CORE_INCLUDE_DIR}" "${ARA_UPSTREAM_EXAMPLES}")
# Match the SDK examples' required validation and version configuration.
target_compile_definitions(araaap-upstream-tests PRIVATE
    ARA_MAJOR_VERSION=${ARA_MAJOR_VERSION} ARA_MINOR_VERSION=${ARA_MINOR_VERSION}
    ARA_PATCH_VERSION=${ARA_PATCH_VERSION}
    ARA_VALIDATE_API_CALLS=1 ARA_ENABLE_INTERNAL_ASSERTS=1 ARA_ENABLE_DEBUG_OUTPUT=1)
target_link_libraries(araaap-upstream-tests PRIVATE ARA_PlugIn_Library ARA_Host_Library)
if(UNIX)
    target_link_libraries(araaap-upstream-tests PRIVATE pthread)
endif()
add_test(NAME araaap-existing-sdk-code COMMAND araaap-upstream-tests)

# Exercise the actual sample plugin without JNI or an Android frontend.
add_executable(araaap-sample-model-tests
    araaap-sample-model-tests.cpp
    ../samples/aaparapluginsample/src/main/cpp/aap-ara-plugin-sample.cpp
    ../androidaudioplugin-ara/src/main/cpp/sdk/araaap.cpp)
target_compile_features(araaap-sample-model-tests PRIVATE cxx_std_17)
target_include_directories(araaap-sample-model-tests PRIVATE ../include "${AAP_CORE_INCLUDE_DIR}"
    ../samples/aaparapluginsample/src/main/cpp ../samples/common/src/main/cpp)
target_link_libraries(araaap-sample-model-tests PRIVATE ARA_PlugIn_Library ARA_Host_Library)
if(UNIX)
    target_link_libraries(araaap-sample-model-tests PRIVATE pthread)
endif()
add_test(NAME araaap-sample-sdk-model COMMAND araaap-sample-model-tests)
