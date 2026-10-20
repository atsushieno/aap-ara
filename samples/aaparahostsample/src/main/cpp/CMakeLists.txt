cmake_minimum_required(VERSION 3.31.5)
project(aap_ara_host_sample)

include("${CMAKE_CURRENT_LIST_DIR}/../../../../../cmake/ARASDK.cmake")

find_package(androidaudioplugin REQUIRED CONFIG)
find_package(androidaudioplugin-ara REQUIRED CONFIG)

add_library(aap-ara-host-sample SHARED
        aap-ara-host-sample.cpp)

target_link_libraries(aap-ara-host-sample
        ARA_Host_Library
        android
        log
        androidaudioplugin::androidaudioplugin
        androidaudioplugin-ara::androidaudioplugin-ara)
