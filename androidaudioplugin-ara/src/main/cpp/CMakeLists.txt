cmake_minimum_required(VERSION 3.5.1)

project(androidaudioplugin_ara LANGUAGES CXX)

find_package(androidaudioplugin REQUIRED CONFIG)

set(androidaudioplugin_ara_SOURCES
    "android/ara-natives-jni.cpp"
    "core/aapxs/ara-aapxs.cpp"
    "core/aapxs/ara-registry.cpp"
    "host/host-runtime.cpp"
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
    log
    androidaudioplugin::androidaudioplugin
)
