#include <jni.h>
#include <aap/core/host/plugin-host.h>
#include "aap-ara-plugin-sample.h"

extern "C" JNIEXPORT jlong JNICALL
Java_org_androidaudioplugin_aaparapluginsample_AraPluginSampleNative_resolveSnapshot(
        JNIEnv*, jobject, jlong servicePointer, jint instanceId) {
    auto* service = reinterpret_cast<aap::PluginService*>(servicePointer);
    auto* instance = service ? service->getLocalInstance(instanceId) : nullptr;
    return instance ? sample_plugin::resolveSnapshot(instance->getPlugin()) : 0;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparapluginsample_AraPluginSampleNative_snapshot(
        JNIEnv* env, jobject, jlong token) {
    return env->NewStringUTF(sample_plugin::snapshot(token).c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparapluginsample_AraPluginSampleNative_setModificationGain(
        JNIEnv* env, jobject, jlong token, jlong id, jdouble gain) {
    return env->NewStringUTF(sample_plugin::setModificationGain(token, id, gain).c_str());
}
