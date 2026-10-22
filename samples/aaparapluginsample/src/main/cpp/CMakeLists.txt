cmake_minimum_required(VERSION 3.5.1)

project(aap_ara_plugin_sample LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../../cmake/ARASDK.cmake")

find_package(androidaudioplugin REQUIRED CONFIG)
find_package(androidaudioplugin-ara REQUIRED CONFIG)

add_library(aap-ara-plugin-sample SHARED
    aap-ara-plugin-sample.cpp
    aap-ara-plugin-sample-jni.cpp)

target_compile_options(aap-ara-plugin-sample
    PRIVATE
    -std=c++17 -Wall -Wshadow
)

target_link_libraries(aap-ara-plugin-sample
    ARA_PlugIn_Library
    android
    log
    androidaudioplugin::androidaudioplugin
    androidaudioplugin-ara::androidaudioplugin-ara
)

# The standalone workspace uses the common editor, independently of the host app.
add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../common"
    "${CMAKE_CURRENT_BINARY_DIR}/sample-common")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_sources(aap-ara-plugin-sample PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../androidTest/cpp/plugin-transport-test.cpp)
endif()
