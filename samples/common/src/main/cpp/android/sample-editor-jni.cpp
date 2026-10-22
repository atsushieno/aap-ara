#include <jni.h>
#include <exception>
#include "sample-editor.h"
#include "sample-project.h"
#include "aap/ara/araaap.hpp"
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
    std::vector<ARA::ARAPlaybackRegionRef> regions;
    std::vector<ARA::ARARegionSequenceRef> sequences;
    for (auto id : intArray(env, clipIds)) {
        auto ref = sample_editor::playbackRegionRef(aap::ara::hostRef<ARA::ARAPlaybackRegionHostRef>(id));
        if (!ref) return env->NewStringUTF("Unknown selected clip");
        regions.push_back(ref);
    }
    for (auto id : intArray(env, trackIds)) {
        auto ref = sample_editor::regionSequenceRef(aap::ara::hostRef<ARA::ARARegionSequenceHostRef>(id));
        if (!ref) return env->NewStringUTF("Unknown selected track");
        sequences.push_back(ref);
    }
    ARA::ARAContentTimeRange range{start, duration};
    ARA::ARAViewSelection selection{sizeof(selection), regions.size(), regions.data(),
            sequences.size(), sequences.data(), hasRange ? &range : nullptr};
    return env->NewStringUTF(sample_editor::notifyEditorSelection(selection).c_str());
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
    // Own UTF-8 strings until all borrowed SDK property pointers have been used.
    std::vector<std::string> trackNamesData, clipNamesData, persistentIDs;
    trackNamesData.reserve(nt);
    clipNamesData.reserve(nc);
    persistentIDs.reserve(nc);
    for (int i = 0; i < nt; ++i) trackNamesData.push_back(stringAt(env, trackNames, i));
    for (int i = 0; i < nc; ++i) {
        clipNamesData.push_back(stringAt(env, clipNames, i));
        persistentIDs.push_back("clip-" + std::to_string(ids[i]));
    }
    using namespace ARA;
    using aap::ara::hostRef;
    std::vector<sample_project::RegionSequence> sequences;
    std::vector<sample_project::PlaybackRegion> regions;
    for (int i = 0; i < nt; ++i)
        sequences.push_back({hostRef<ARARegionSequenceHostRef>(tids[i]),
                {sizeof(ARARegionSequenceProperties), trackNamesData[i].c_str(), i, nullptr, nullptr}});
    auto rate = sample_editor::sampleRate();
    for (int i = 0; i < nc; ++i) {
        const auto* name = clipNamesData[i].c_str();
        const auto* persistentID = persistentIDs[i].c_str();
        ARAAudioSourceProperties source{sizeof(source), name, persistentID, ARASampleCount(rate * 60), rate,
                2, kARAFalse, kARAChannelArrangementUndefined, nullptr};
        ARAAudioModificationProperties modification{sizeof(modification), name, persistentID};
        ARAPlaybackRegionProperties region{sizeof(region), kARAPlaybackTransformationNoChanges,
                v[i*5+1], v[i*5+2], v[i*5], v[i*5+2], nullptr, nullptr, name, nullptr};
        regions.push_back({hostRef<ARAPlaybackRegionHostRef>(ids[i]), hostRef<ARARegionSequenceHostRef>(parents[i]),
                source, modification, region, v[i*5+3], v[i*5+4], stringAt(env, pluginStates, i)});
    }
    auto name = jstringToStdString(env, title);
    ARADocumentProperties document{sizeof(document), name.c_str()};
    return env->NewStringUTF(sample_project::sync(document, sequences, regions).c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_arasample_SampleEditorNative_verifyEditorAudio(
        JNIEnv* env, jobject, jint clipId, jboolean postChange, jdouble modificationGain) {
    return env->NewStringUTF(sample_editor::verifyEditorAudio(clipId, postChange, modificationGain).c_str());
}

