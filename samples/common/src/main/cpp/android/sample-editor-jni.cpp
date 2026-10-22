#include <jni.h>
#include <exception>
#include "sample-editor.h"
#include "aap/core/host/plugin-host.h"
#include "aap/ara/registry.h"
#include "aap/core/aapxs/ara-aapxs.h"

// Use AAP's JNI Binder bridge for an app-owned service endpoint. The exported
// Android service remains exclusively owned by the external AAP host.
extern "C" JNIEXPORT jobject JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_createPrivateConnection(
        JNIEnv* env, jobject, jint connectionId, jstring packageName, jstring serviceClass) {
    aap::ara::addToRegistry(*aap::xs::AAPXSDefinitionRegistry::getStandardExtensions());
    auto natives = env->FindClass("org/androidaudioplugin/AudioPluginNatives");
    if (!natives) return nullptr;
    auto create = env->GetStaticMethodID(natives, "createBinderForService", "()Landroid/os/IBinder;");
    if (!create) return nullptr;
    auto add = env->GetStaticMethodID(natives, "addBinderForClient", "(ILjava/lang/String;Ljava/lang/String;Landroid/os/IBinder;)V");
    if (!add) return nullptr;
    auto binder = env->CallStaticObjectMethod(natives, create);
    if (env->ExceptionCheck()) return nullptr;
    env->CallStaticVoidMethod(natives, add, connectionId, packageName, serviceClass, binder);
    return binder;
}

extern "C" JNIEXPORT void JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_closePrivateConnection(
        JNIEnv* env, jobject, jint connectionId, jstring packageName, jstring serviceClass, jobject binder) {
    auto natives = env->FindClass("org/androidaudioplugin/AudioPluginNatives");
    if (!natives) return;
    auto remove = env->GetStaticMethodID(natives, "removeBinderForClient", "(ILjava/lang/String;Ljava/lang/String;)V");
    if (!remove) return;
    auto destroy = env->GetStaticMethodID(natives, "destroyBinderForService", "(Landroid/os/IBinder;)V");
    if (!destroy) return;
    env->CallStaticVoidMethod(natives, remove, connectionId, packageName, serviceClass);
    if (!env->ExceptionCheck()) env->CallStaticVoidMethod(natives, destroy, binder);
}

namespace aap {
class PluginClientConnectionList;
class AAPJniFacade {
public:
    static AAPJniFacade* getInstance();
    PluginClientConnectionList* getPluginConnectionListFromJni(jint connectorInstanceId, bool createIfNotExist);
};
}

namespace {
std::string jstringToStdString(JNIEnv* env, jstring s) {
    jboolean copy = false;
    const char* utf = env->GetStringUTFChars(s, &copy);
    std::string result = utf ? utf : "";
    if (utf)
        env->ReleaseStringUTFChars(s, utf);
    return result;
}

std::vector<int32_t> intArray(JNIEnv* env, jintArray array) {
    std::vector<int32_t> values(env->GetArrayLength(array));
    env->GetIntArrayRegion(array, 0, values.size(), values.data());
    return values;
}
std::string stringAt(JNIEnv* env, jobjectArray array, size_t index) {
    auto item = static_cast<jstring>(env->GetObjectArrayElement(array, index));
    auto value = jstringToStdString(env, item);
    env->DeleteLocalRef(item);
    return value;
}
aap::PluginClientConnectionList* connections(int id) {
    return aap::AAPJniFacade::getInstance()->getPluginConnectionListFromJni(id, true);
}
aap::PluginListSnapshot& plugins() {
    static auto list = aap::PluginListSnapshot::queryServices();
    return list;
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_runScenario(
        JNIEnv* env, jobject, jint connection, jstring pluginId, jint rate) {
    try {
        auto* list = connections(connection);
        if (!list) return env->NewStringUTF("Failed to resolve service connection list.");
        static aap::PluginListSnapshot pluginList{};
        if (pluginList.getNumPluginInformation() == 0)
            pluginList = aap::PluginListSnapshot::queryServices();
        return env->NewStringUTF(sample_editor::runScenario(list, pluginList,
                jstringToStdString(env, pluginId), rate).c_str());
    } catch (const std::exception& e) {
        auto message = std::string("Scenario failed with native exception: ") + e.what();
        return env->NewStringUTF(message.c_str());
    } catch (...) {
        return env->NewStringUTF("Scenario failed with an unknown native exception.");
    }
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_openEditor(
        JNIEnv* env, jobject, jint connection, jstring pluginId, jint rate) {
    try {
        sample_editor::closeEditor();
        auto* list = connections(connection);
        if (!list) return env->NewStringUTF("No service connection");
        return env->NewStringUTF(sample_editor::openEditor(list, plugins(),
                jstringToStdString(env, pluginId), rate).c_str());
    } catch (const std::exception& e) {
        return env->NewStringUTF(e.what());
    }
}

extern "C" JNIEXPORT jint JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_editorInstanceId(
        JNIEnv*, jobject) {
    return sample_editor::editorInstanceId();
}

extern "C" JNIEXPORT void JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_closeEditor(
        JNIEnv*, jobject) {
    sample_editor::closeEditor();
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_notifyEditorSelection(
        JNIEnv* env, jobject, jintArray clipIds, jintArray trackIds, jboolean hasRange, jdouble start, jdouble duration) {
    // Reject oversized Java arrays before allocating their native copies.
    if (uint64_t(env->GetArrayLength(clipIds)) + env->GetArrayLength(trackIds) >
        (ARA_SHARED_MEMORY_SIZE - sizeof(aap_ara_view_selection_wire_t)) / sizeof(int64_t))
        return env->NewStringUTF("Selection exceeds transport capacity");
    return env->NewStringUTF(sample_editor::notifyEditorSelection(intArray(env, clipIds),
            intArray(env, trackIds), hasRange, start, duration).c_str());
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_drainPluginUpdates(
        JNIEnv* env, jobject) {
    auto result = sample_editor::drainPluginUpdates();
    auto strings = env->FindClass("java/lang/String");
    auto array = env->NewObjectArray(result.size(), strings, nullptr);
    for (size_t i = 0; i < result.size(); ++i) {
        auto value = env->NewStringUTF(result[i].c_str());
        env->SetObjectArrayElement(array, i, value);
        env->DeleteLocalRef(value);
    }
    env->DeleteLocalRef(strings);
    return array;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_syncEditor(
        JNIEnv* env, jobject, jstring title, jintArray trackIds, jobjectArray trackNames,
        jintArray clipIds, jintArray clipTracks, jobjectArray clipNames,
        jdoubleArray values, jobjectArray pluginStates) {
    const auto nt = env->GetArrayLength(trackIds), nc = env->GetArrayLength(clipIds);
    if (env->GetArrayLength(trackNames) != nt || env->GetArrayLength(clipTracks) != nc ||
        env->GetArrayLength(clipNames) != nc || env->GetArrayLength(values) != int64_t(nc) * 5 ||
        env->GetArrayLength(pluginStates) != nc)
        return env->NewStringUTF("Invalid model arrays");
    auto tids = intArray(env, trackIds), ids = intArray(env, clipIds), parents = intArray(env, clipTracks);
    std::vector<double> v(size_t(nc) * 5);
    env->GetDoubleArrayRegion(values, 0, v.size(), v.data());
    std::vector<sample_editor::Track> tracks;
    std::vector<sample_editor::Clip> clips;
    for (int i = 0; i < nt; ++i) tracks.push_back({tids[i], stringAt(env, trackNames, i)});
    for (int i = 0; i < nc; ++i)
        clips.push_back({ids[i], parents[i], stringAt(env, clipNames, i),
                v[i*5], v[i*5+1], v[i*5+2], v[i*5+3], v[i*5+4], stringAt(env, pluginStates, i)});
    return env->NewStringUTF(sample_editor::syncEditor(jstringToStdString(env, title), tracks, clips).c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_verifyEditorAudio(
        JNIEnv* env, jobject, jint clipId, jboolean postChange, jdouble modificationGain) {
    return env->NewStringUTF(sample_editor::verifyEditorAudio(clipId, postChange, modificationGain).c_str());
}

