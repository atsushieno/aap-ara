cmake_minimum_required(VERSION 3.31.5)
project(aap_ara_host_sample LANGUAGES C CXX)

add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/../../../../common"
        "${CMAKE_CURRENT_BINARY_DIR}/sample-common")
