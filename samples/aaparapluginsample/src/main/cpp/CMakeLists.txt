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
