# Common editor runtime and Android adapter, used independently by both apps.
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/ARASDK.cmake")
find_package(androidaudioplugin REQUIRED CONFIG)
find_package(androidaudioplugin-ara REQUIRED CONFIG)

include("${CMAKE_CURRENT_LIST_DIR}/../scenarios/upstream.cmake")
add_library(aap-ara-sample-editor STATIC src/main/cpp/sample-editor.cpp
    ../scenarios/upstream-scenarios.cpp)
target_include_directories(aap-ara-sample-editor PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../scenarios")
set_target_properties(aap-ara-sample-editor PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(aap-ara-sample-editor PUBLIC cxx_std_17)
target_include_directories(aap-ara-sample-editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/main/cpp")
target_link_libraries(aap-ara-sample-editor PUBLIC
    ARA_Host_Library
    ara-examples-test-host
    androidaudioplugin::androidaudioplugin
    androidaudioplugin-ara::androidaudioplugin-ara)

# Other native frontends can link the editor without this JNI adapter.
add_library(aap-ara-sample-editor-jni SHARED src/main/cpp/android/sample-editor-jni.cpp)
target_link_libraries(aap-ara-sample-editor-jni PRIVATE aap-ara-sample-editor android log)
