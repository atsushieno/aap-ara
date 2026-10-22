# Import implementation files from the pinned submodule without copying or patching them.
# The desktop top-level CMake requires VST3/AU/CLAP. Android supplies AAP instead.
include_guard(GLOBAL)
include("${ARA_SDK_ROOT}/ARA_API/ARA_Version.cmake")
set(examples "${ARA_SDK_ROOT}/ARA_Examples")
# Validation changes the layout of SDK helper classes. Keep it enabled in the
# libraries and the imported examples for both Debug and Release.
foreach(sdk_target ARA_Host_Library ARA_PlugIn_Library)
    target_compile_definitions(${sdk_target} PUBLIC
        ARA_VALIDATE_API_CALLS=1 ARA_ENABLE_INTERNAL_ASSERTS=1 ARA_ENABLE_DEBUG_OUTPUT=1)
    target_compile_options(${sdk_target} PUBLIC "-include" "${CMAKE_CURRENT_LIST_DIR}/android-log.h")
endforeach()
function(configure_example target)
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_compile_features(${target} PUBLIC cxx_std_17)
    target_include_directories(${target} PUBLIC "${examples}" "${examples}/TestHost")
    target_compile_definitions(${target} PUBLIC
        ARA_MAJOR_VERSION=${ARA_MAJOR_VERSION} ARA_MINOR_VERSION=${ARA_MINOR_VERSION} ARA_PATCH_VERSION=${ARA_PATCH_VERSION}
        ARA_VALIDATE_API_CALLS=1 ARA_ENABLE_INTERNAL_ASSERTS=1 ARA_ENABLE_DEBUG_OUTPUT=1
        ARA_ENABLE_VST3=0 ARA_ENABLE_CLAP=0)
endfunction()
add_library(ara-examples-test-host STATIC
    "${examples}/TestHost/TestHost.cpp"
    "${examples}/TestHost/ModelObjects.cpp"
    "${examples}/TestHost/ARADocumentController.cpp"
    "${examples}/TestHost/CompanionAPIs.cpp"
    "${examples}/TestHost/TestCases.cpp"
    "${examples}/TestHost/ARAHostInterfaces/ARAAudioAccessController.cpp"
    "${examples}/TestHost/ARAHostInterfaces/ARAArchivingController.cpp"
    "${examples}/TestHost/ARAHostInterfaces/ARAContentAccessController.cpp"
    "${examples}/TestHost/ARAHostInterfaces/ARAModelUpdateController.cpp"
    "${examples}/TestHost/ARAHostInterfaces/ARAPlaybackController.cpp"
    "${examples}/ExamplesCommon/Archives/Archives.cpp"
    "${examples}/ExamplesCommon/AudioFiles/AudioFiles.cpp"
    "${examples}/ExamplesCommon/SignalProcessing/PulsedSineSignal.c"
    "${examples}/3rdParty/ICST_AudioFile/AudioFile.cpp"
    "${examples}/3rdParty/cpp-base64/base64.cpp"
    "${examples}/3rdParty/pugixml/src/pugixml.cpp")
configure_example(ara-examples-test-host)
target_link_libraries(ara-examples-test-host PUBLIC ARA_Host_Library)
