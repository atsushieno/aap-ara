cmake_minimum_required(VERSION 3.5.1)

project(androidaudioplugin_ara LANGUAGES C CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../cmake/ARASDK.cmake")

find_package(androidaudioplugin REQUIRED CONFIG)

set(androidaudioplugin_ara_SOURCES
    "android/ara-natives-jni.cpp"
    "core/aapxs/ara-aapxs.cpp"
    "core/aapxs/ara-registry.cpp"
    "sdk/remote-host.cpp"
    "sdk/araaap.cpp"
)

add_library(androidaudioplugin-ara SHARED ${androidaudioplugin_ara_SOURCES})

target_compile_options(androidaudioplugin-ara
    PRIVATE
    -std=c++17 -Wall -Wshadow
)

target_include_directories(androidaudioplugin-ara
    PRIVATE
    "../../../../include"
)

target_link_libraries(androidaudioplugin-ara
    ARA_Host_Library
    log
    androidaudioplugin::androidaudioplugin
)
