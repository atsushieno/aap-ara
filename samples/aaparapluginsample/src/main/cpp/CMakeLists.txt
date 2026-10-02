cmake_minimum_required(VERSION 3.5.1)

project(aap_ara_plugin_sample LANGUAGES CXX)

find_package(androidaudioplugin REQUIRED CONFIG)
find_package(androidaudioplugin-ara REQUIRED CONFIG)

add_library(aap-ara-plugin-sample SHARED aap-ara-plugin-sample.cpp)

target_compile_options(aap-ara-plugin-sample
    PRIVATE
    -std=c++17 -Wall -Wshadow
)

target_link_libraries(aap-ara-plugin-sample
    android
    log
    androidaudioplugin::androidaudioplugin
    androidaudioplugin-ara::androidaudioplugin-ara
)

# The plugin app is also a document editor; reuse the standalone host's JNI bridge.
add_library(aap-ara-host-sample SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../aaparahostsample/src/main/cpp/aap-ara-host-sample.cpp)
target_compile_features(aap-ara-host-sample PRIVATE cxx_std_17)
target_link_libraries(aap-ara-host-sample
    android log
    androidaudioplugin::androidaudioplugin
    androidaudioplugin-ara::androidaudioplugin-ara)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_sources(aap-ara-plugin-sample PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../androidTest/cpp/plugin-transport-test.cpp)
endif()
