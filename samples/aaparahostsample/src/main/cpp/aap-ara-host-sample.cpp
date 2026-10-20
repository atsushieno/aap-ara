#include <jni.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <set>
#include <vector>

#include "aap/core/host/plugin-host.h"
#include "aap/core/host/plugin-instance.h"
#include "sample-host-services.h"
#include "aap/ara/remote-host.h"
#include "aap/ara/registry.h"
#include "aap/core/aapxs/ara-aapxs.h"
#include "aap/unstable/logging.h"

#define LOG_TAG "AAP.ARA.HostSample"

// Use AAP's JNI Binder bridge for an app-owned service endpoint. The exported
// Android service remains exclusively owned by the external AAP host.
extern "C" JNIEXPORT jobject JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_createPrivateConnection(
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
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_closePrivateConnection(
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
constexpr ARASampleCount kAudioSourceLength = 48000;
constexpr ARASamplePosition kAnalysisStart = 64;
constexpr ARASampleCount kAnalysisCount = 128;
constexpr ARASampleCount kContentChangeCount = 2048;

std::string jstringToStdString(JNIEnv* env, jstring s) {
    jboolean copy = false;
    const char* utf = env->GetStringUTFChars(s, &copy);
    std::string result = utf ? utf : "";
    if (utf)
        env->ReleaseStringUTFChars(s, utf);
    return result;
}

std::string boolString(bool value) {
    return value ? "true" : "false";
}

struct HostAnalysisMetrics {
    std::array<float, 4> left{};
    std::array<float, 4> right{};
};

struct HostAudioStore {
    std::array<std::vector<float>, 2> version1{};
    std::array<std::vector<float>, 2> version2{};
    int active_version{1};

    explicit HostAudioStore(int32_t sample_rate) {
        (void) sample_rate;
        for (auto& ch : version1)
            ch.resize(kAudioSourceLength);
        for (auto& ch : version2)
            ch.resize(kAudioSourceLength);
        for (int64_t i = 0; i < kAudioSourceLength; ++i) {
            version1[0][i] = 0.10f + static_cast<float>(i) * 0.0010f;
            version1[1][i] = -0.20f + static_cast<float>(i) * 0.0005f;
            version2[0][i] = 0.50f - static_cast<float>(i) * 0.00075f;
            version2[1][i] = 0.25f + static_cast<float>(i) * 0.00025f;
        }
    }

    const std::array<std::vector<float>, 2>& current() const {
        return active_version == 2 ? version2 : version1;
    }
};

class DeterministicAudioSourceProvider : public SampleAudioSource {
    HostAudioStore& store;
    int32_t sample_rate;

public:
    DeterministicAudioSourceProvider(HostAudioStore& store_, int32_t sampleRate)
            : store(store_), sample_rate(sampleRate) {}

    int32_t channelCount() const override { return 2; }
    ARASampleRate sampleRate() const override { return sample_rate; }
    ARASampleCount sampleCount() const override { return kAudioSourceLength; }
    bool readSamples(ARASamplePosition start, ARASampleCount count, void* const buffers[], bool wide) override {
        if (start < 0 || count < 0 || start > kAudioSourceLength || count > kAudioSourceLength - start) return false;
        for (int c = 0; c < 2; ++c) {
            if (wide) for (int64_t i = 0; i < count; ++i) static_cast<double*>(buffers[c])[i] = store.current()[c][start+i];
            else std::memcpy(buffers[c], store.current()[c].data() + start, count * sizeof(float));
        }
        return true;
    }
};

static float compute_rms(const float* samples, int64_t sample_count) {
    if (!samples || sample_count <= 0)
        return 0.0f;
    double sum = 0.0;
    for (int64_t i = 0; i < sample_count; ++i)
        sum += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    return static_cast<float>(std::sqrt(sum / static_cast<double>(sample_count)));
}

static float compute_peak(const float* samples, int64_t sample_count) {
    float peak = 0.0f;
    if (!samples || sample_count <= 0)
        return peak;
    for (int64_t i = 0; i < sample_count; ++i)
        peak = std::max(peak, std::fabs(samples[i]));
    return peak;
}

static HostAnalysisMetrics compute_metrics(
        const std::array<std::vector<float>, 2>& planes,
        ARASamplePosition start_sample,
        ARASampleCount sample_count) {
    auto frames = std::max<int64_t>(0, std::min<int64_t>(sample_count, kAudioSourceLength - start_sample));
    HostAnalysisMetrics metrics{};
    auto* left = planes[0].data() + start_sample;
    auto* right = planes[1].data() + start_sample;
    metrics.left = {left[0], left[1], compute_rms(left, frames), compute_peak(left, frames)};
    metrics.right = {right[0], right[1], compute_rms(right, frames), compute_peak(right, frames)};
    return metrics;
}

struct HostScenarioContext {
    HostAudioStore store;
    SampleHostServices runtime;
    std::shared_ptr<DeterministicAudioSourceProvider> source_provider;

    explicit HostScenarioContext(int32_t sample_rate) : store(sample_rate) {
        source_provider = std::make_shared<DeterministicAudioSourceProvider>(store, sample_rate);
    }
};

static std::string format_metrics(const std::array<float, 4>& values) {
    std::ostringstream ss;
    ss << "first=(" << values[0] << ", " << values[1] << ") rms=" << values[2] << " peak=" << values[3];
    return ss.str();
}

static bool metrics_match(const std::array<float, 4>& actual, const std::array<float, 4>& expected, float tolerance = 1.0e-5f) {
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::fabs(actual[i] - expected[i]) > tolerance)
            return false;
    }
    return true;
}
}

extern "C"
JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_runScenario(
        JNIEnv* env,
        jobject,
        jint serviceConnectionId,
        jstring pluginId,
        jint sampleRate) {
    std::ostringstream report;
    auto pluginIdString = jstringToStdString(env, pluginId);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "runScenario start serviceConnectionId=%d pluginId=%s sampleRate=%d",
                 serviceConnectionId, pluginIdString.c_str(), sampleRate);

    try {
        auto* connections = aap::AAPJniFacade::getInstance()->getPluginConnectionListFromJni(
                serviceConnectionId, true);
        if (!connections)
            return env->NewStringUTF("Failed to resolve service connection list.");
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Resolved plugin connection list");

        static aap::PluginListSnapshot pluginList{};
        if (pluginList.getNumPluginInformation() == 0)
            pluginList = aap::PluginListSnapshot::queryServices();
        aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                     "Plugin list ready count=%d",
                     pluginList.getNumPluginInformation());

        auto* registry = aap::xs::AAPXSDefinitionRegistry::getStandardExtensions();
        aap::ara::addToRegistry(*registry);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Registry prepared with standard + ARA extensions");
        auto client = std::make_unique<aap::PluginClient>(connections, &pluginList, registry);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "PluginClient created");
        // createInstance() no longer takes sampleRate (it is passed to prepare() below).
        auto result = client->createInstance(pluginIdString, true);
        aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                     "createInstance returned instanceId=%d error=%s",
                     result.value, result.error.c_str());
        if (!result.error.empty()) {
            client.reset();
            std::string message = "Failed to create plugin instance: " + result.error;
            return env->NewStringUTF(message.c_str());
        }

        auto* instance = dynamic_cast<aap::RemotePluginInstance*>(client->getInstanceById(result.value));
        if (!instance) {
            client.reset();
            return env->NewStringUTF("Plugin instance was created but could not be resolved.");
        }
        aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                     "Resolved RemotePluginInstance instanceId=%d",
                     instance->getInstanceId());

        auto* plugin = instance->getPlugin();
        auto* ara = static_cast<aap_ara_extension_t*>(
                plugin->get_extension(plugin, AAP_ARA_EXTENSION_URI));
        if (!ara) {
            client->destroyInstance(instance);
            client.reset();
            return env->NewStringUTF("The plugin did not expose the ARA extension.");
        }
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Resolved plugin ARA extension");

        HostScenarioContext host_context(sampleRate);
        host_context.runtime.registerAudioSource(30, host_context.source_provider);
        ARADocumentProperties documentProperties{sizeof(documentProperties), "Sample Document"};
        auto sdk = std::make_unique<aap::ara::Host>(plugin, ara, &host_context.runtime.instance, &documentProperties);
        auto host_bridge = std::make_unique<aap::ara::ScopedRemotePluginInstanceARAHostContext>(sdk->transportExtension(), *instance);
        auto* controller = sdk->documentController(); auto* api = controller->documentControllerInterface; auto dc = controller->documentControllerRef;
        auto* binding = sdk->bind(kARAPlaybackRendererRole | kARAEditorRendererRole | kARAEditorViewRole, kARAPlaybackRendererRole | kARAEditorViewRole);
        report << "ARA SDK factory: " << sdk->factory()->factoryID << "\n";
        ARAMusicalContextProperties contextProperties{sizeof(contextProperties), "Song", 0, nullptr};
        api->beginEditing(dc);
        auto musicalContext = api->createMusicalContext(dc, hostRef<ARAMusicalContextHostRef>(10), &contextProperties);
        ARARegionSequenceProperties sequenceProperties{sizeof(sequenceProperties), "Lead Vox", 0, musicalContext, nullptr};
        auto sequence = api->createRegionSequence(dc, hostRef<ARARegionSequenceHostRef>(20), &sequenceProperties);
        ARAAudioSourceProperties sourceProperties{sizeof(sourceProperties), "Lead Vox Take", "lead-vox-source", kAudioSourceLength, double(sampleRate), 2, kARAFalse, kARAChannelArrangementUndefined, nullptr};
        auto source = api->createAudioSource(dc, hostRef<ARAAudioSourceHostRef>(30), &sourceProperties);
        ARAAudioModificationProperties modificationProperties{sizeof(modificationProperties), "Lead Vox Edit", "lead-vox-edit"};
        auto modification = api->createAudioModification(dc, source, hostRef<ARAAudioModificationHostRef>(40), &modificationProperties);
        ARAPlaybackRegionProperties regionProperties{sizeof(regionProperties), kARAPlaybackTransformationNoChanges, 0, 1, 0, 1, nullptr, sequence, "Lead Vox Region", nullptr};
        auto region = api->createPlaybackRegion(dc, modification, hostRef<ARAPlaybackRegionHostRef>(50), &regionProperties);
        documentProperties.name = "Sample Document Updated"; api->updateDocumentProperties(dc, &documentProperties);
        regionProperties.durationInModificationTime = 2; regionProperties.durationInPlaybackTime = 2; api->updatePlaybackRegionProperties(dc, region, &regionProperties);
        api->endEditing(dc);
        binding->playbackRendererInterface->addPlaybackRegion(binding->playbackRendererRef, region);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Preparing instance");
        instance->prepare(8, sampleRate);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Enabling audio source samples access");
        api->enableAudioSourceSamplesAccess(dc, source, kARATrue);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Enabled audio source samples access");
        report << "  sample access enabled=true\n";
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Activating instance");
        instance->activate();

        auto* buffer = instance->getAudioPluginBuffer();
        std::array<float, 4> actual_pre_l{};
        std::array<float, 4> actual_post_l{};
        std::array<float, 4> actual_pre_r{};
        std::array<float, 4> actual_post_r{};

        auto capture_metrics = [&](std::array<float, 4>& left, std::array<float, 4>& right, bool readPre) {
            aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                         "capture_metrics begin readPre=%d", readPre ? 1 : 0);
            instance->process(8, 0);
            int output_index = 0;
            for (int32_t i = 0, n = instance->getNumPorts(); i < n; ++i) {
                auto* port = instance->getPort(i);
                if (!port || port->getContentType() != AAP_CONTENT_TYPE_AUDIO || port->getPortDirection() != AAP_PORT_DIRECTION_OUTPUT)
                    continue;
                auto* data = static_cast<float*>(buffer->get_buffer(buffer, i));
                if (!data)
                    continue;
                auto offset = readPre ? 0 : 4;
                if (output_index == 0) {
                    for (int f = 0; f < 4; ++f)
                        left[f] = data[f + offset];
                } else if (output_index == 1) {
                    for (int f = 0; f < 4; ++f)
                        right[f] = data[f + offset];
                }
                ++output_index;
            }
            aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                         "capture_metrics end readPre=%d left=(%f,%f,%f,%f) right=(%f,%f,%f,%f)",
                         readPre ? 1 : 0,
                         left[0], left[1], left[2], left[3],
                         right[0], right[1], right[2], right[3]);
        };

        auto wait_for_metrics = [&](std::array<float, 4>& left,
                                    std::array<float, 4>& right,
                                    const std::array<float, 4>& expectedLeft,
                                    const std::array<float, 4>& expectedRight,
                                    bool readPre) {
            for (int attempt = 0; attempt < 50; ++attempt) {
                capture_metrics(left, right, readPre);
                if (metrics_match(left, expectedLeft) && metrics_match(right, expectedRight))
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            return false;
        };

        auto expected_pre = compute_metrics(host_context.store.current(), kAnalysisStart, kAnalysisCount);
        auto pre_ready = wait_for_metrics(actual_pre_l, actual_pre_r, expected_pre.left, expected_pre.right, true);

        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Deactivating instance after pre-change capture");
        instance->deactivate();

        host_context.store.active_version = 2;
        aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                     "Notifying audio source content change start=%d count=%d",
                     0, kContentChangeCount);
        ARAContentTimeRange changedRange{0, double(kContentChangeCount) / sampleRate};
        api->beginEditing(dc);
        api->updateAudioSourceContent(dc, source, &changedRange, kARAContentUpdateEverythingChanged);
        api->endEditing(dc);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Audio source content change notified");
        report << "  content change notified\n";
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Re-activating instance for post-change capture");
        instance->activate();
        auto expected_post = compute_metrics(host_context.store.current(), kAnalysisStart, kAnalysisCount);
        auto post_ready = wait_for_metrics(actual_post_l, actual_post_r, expected_post.left, expected_post.right, false);

        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Deactivating instance");
        instance->deactivate();

        report << "\nAudio fetch verification:\n";
        report << "  expected pre  L: " << format_metrics(expected_pre.left) << "\n";
        report << "  observed pre  L: " << format_metrics(actual_pre_l) << "\n";
        report << "  expected pre  R: " << format_metrics(expected_pre.right) << "\n";
        report << "  observed pre  R: " << format_metrics(actual_pre_r) << "\n";
        report << "  expected post L: " << format_metrics(expected_post.left) << "\n";
        report << "  observed post L: " << format_metrics(actual_post_l) << "\n";
        report << "  expected post R: " << format_metrics(expected_post.right) << "\n";
        report << "  observed post R: " << format_metrics(actual_post_r) << "\n";

        auto verified = pre_ready &&
                        post_ready &&
                        metrics_match(actual_pre_l, expected_pre.left) &&
                        metrics_match(actual_pre_r, expected_pre.right) &&
                        metrics_match(actual_post_l, expected_post.left) &&
                        metrics_match(actual_post_r, expected_post.right);
        report << "\nResult: " << (verified ? "PASS" : "FAIL") << "\n";
        report << "  pre_ready=" << boolString(pre_ready) << "\n";
        report << "  post_ready=" << boolString(post_ready) << "\n";
        report << "  verification=" << boolString(verified) << "\n";

        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Disabling audio source samples access");
        api->enableAudioSourceSamplesAccess(dc, source, kARAFalse);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Disabled audio source samples access");
        report << "  sample access enabled=false\n";

        report << "\nDestroying ARA model...\n";
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Beginning ARA destroy transaction");
        binding->playbackRendererInterface->removePlaybackRegion(binding->playbackRendererRef, region);
        api->beginEditing(dc);
        api->destroyPlaybackRegion(dc, region); api->destroyAudioModification(dc, modification); api->destroyAudioSource(dc, source);
        api->destroyRegionSequence(dc, sequence); api->destroyMusicalContext(dc, musicalContext);
        api->endEditing(dc);
        sdk.reset(); host_bridge.reset();
        client->destroyInstance(instance);
        client.reset();
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "runScenario completed successfully");

        return env->NewStringUTF(report.str().c_str());
    } catch (const std::exception& ex) {
        aap::a_log_f(AAP_LOG_LEVEL_ERROR, LOG_TAG,
                     "Scenario failed with native exception: %s", ex.what());
        std::string message = std::string("Scenario failed with native exception: ") + ex.what();
        return env->NewStringUTF(message.c_str());
    } catch (...) {
        aap::a_log(AAP_LOG_LEVEL_ERROR, LOG_TAG, "Scenario failed with unknown native exception");
        return env->NewStringUTF("Scenario failed with an unknown native exception.");
    }
}


// An editor session is confined to the activity's single worker thread. The host
// bridge outlives all model calls, including reverse sample-read callbacks.
namespace {
class ToneProvider : public SampleAudioSource {
    int rate;
public:
    float frequency = 220, gain = 0.2f;
    explicit ToneProvider(int rate_) : rate(rate_) {}
    int32_t channelCount() const override { return 2; }
    double sampleRate() const override { return rate; }
    ARASampleCount sampleCount() const override { return rate * 60; }
    bool readSamples(ARASamplePosition start, ARASampleCount count, void* const buffers[], bool wide) override {
        if (start < 0 || count < 0 || start > sampleCount() || count > sampleCount() - start) return false;
        for (int c = 0; c < 2; ++c) for (int64_t i = 0; i < count; ++i) {
            auto value = gain * std::sin(6.283185307179586 * frequency * (start + i) / rate);
            if (wide) static_cast<double*>(buffers[c])[i] = value; else static_cast<float*>(buffers[c])[i] = value;
        }
        return true;
    }
};
struct EditorClip { int track; std::string name, plugin_state; double start, offset, duration; float frequency, gain;
    std::shared_ptr<ToneProvider> provider; };
struct EditorSession {
    std::unique_ptr<aap::PluginClient> client;
    aap::RemotePluginInstance* instance{};
    SampleHostServices runtime;
    std::unique_ptr<aap::ara::ScopedRemotePluginInstanceARAHostContext> bridge;
    // Worker-owned facade for the editor's serialized model calls.
    std::unique_ptr<aap::xs::AraClientAAPXS> ara_client;
    std::unique_ptr<aap::ara::Host> sdk;
    const ARADocumentControllerInterface* ara{};
    ARADocumentControllerRef dc{};
    const ARAPlugInExtensionInstance* binding{};
    ARAMusicalContextRef musicalContext{};
    std::map<int, ARARegionSequenceRef> sequenceRefs;
    std::map<int, ARAAudioSourceRef> sourceRefs;
    std::map<int, ARAAudioModificationRef> modificationRefs;
    std::map<int, ARAPlaybackRegionRef> regionRefs;
    AndroidAudioPlugin* plugin{};
    int rate;
    std::map<int, std::string> tracks;
    std::map<int, EditorClip> clips;
    bool document_created{};
    bool supports_archive = false;
    bool supports_selection = false;
    std::mutex updates_mutex;
    std::vector<SampleContentUpdate> pending_updates;
    explicit EditorSession(int rate_) : rate(rate_) {
        runtime.onUpdate = [this](SampleContentUpdate update) {
            const std::lock_guard<std::mutex> lock(updates_mutex); pending_updates.push_back(update);
        };
    }
    void removeClip(int id) {
        binding->playbackRendererInterface->removePlaybackRegion(binding->playbackRendererRef, regionRefs.at(id));
        ara->destroyPlaybackRegion(dc, regionRefs.at(id)); ara->destroyAudioModification(dc, modificationRefs.at(id));
        ara->enableAudioSourceSamplesAccess(dc, sourceRefs.at(id), kARAFalse); ara->destroyAudioSource(dc, sourceRefs.at(id));
        runtime.unregisterAudioSource(id); clips.erase(id); regionRefs.erase(id); modificationRefs.erase(id); sourceRefs.erase(id);
    }
    ~EditorSession() {
        runtime.onUpdate = {};
        try { if (document_created) {
            ara->beginEditing(dc);
            while (!clips.empty()) removeClip(clips.begin()->first);
            for (auto& t : sequenceRefs) ara->destroyRegionSequence(dc, t.second);
            ara->destroyMusicalContext(dc, musicalContext); ara->endEditing(dc);
        }} catch (const std::exception& e) {
            aap::a_log_f(AAP_LOG_LEVEL_WARN, LOG_TAG, "ARA editor teardown: %s", e.what());
        }
        sdk.reset(); bridge.reset(); ara_client.reset();
        if (instance) client->destroyInstance(instance);
    }
};
std::unique_ptr<EditorSession> editor;
std::string archive_hex(const void* data, size_t size) {
    const char* digits = "0123456789abcdef";
    std::string text;
    auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i) { text += digits[bytes[i] >> 4]; text += digits[bytes[i] & 15]; }
    return text;
}
bool archive_bytes(const std::string& text, std::vector<uint8_t>& data) {
    if (text.size() % 2 || text.size() > 64 * 1024 * 1024) return false;
    auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (size_t i = 0; i < text.size(); i += 2) {
        int a = digit(text[i]), b = digit(text[i + 1]);
        if (a < 0 || b < 0) return false;
        data.push_back((a << 4) | b);
    }
    return true;
}
}

extern "C" JNIEXPORT jint JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_editorInstanceId(JNIEnv*, jobject) {
    return editor && editor->instance ? editor->instance->getInstanceId() : -1;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_openEditor(
        JNIEnv* env, jobject, jint connection, jstring pluginId, jint rate) {
    try {
        editor.reset();
        auto session = std::make_unique<EditorSession>(rate);
        auto* connections = aap::AAPJniFacade::getInstance()->getPluginConnectionListFromJni(connection, true);
        if (!connections) throw std::runtime_error("No service connection");
        static auto list = aap::PluginListSnapshot::queryServices();
        auto* registry = aap::xs::AAPXSDefinitionRegistry::getStandardExtensions();
        aap::ara::addToRegistry(*registry);
        session->client = std::make_unique<aap::PluginClient>(connections, &list, registry);
        auto result = session->client->createInstance(jstringToStdString(env, pluginId), true);
        if (!result.error.empty()) throw std::runtime_error(result.error);
        session->instance = dynamic_cast<aap::RemotePluginInstance*>(session->client->getInstanceById(result.value));
        if (!session->instance) throw std::runtime_error("No remote plugin instance");
        session->plugin = session->instance->getPlugin();
        auto* transport = static_cast<aap_ara_extension_t*>(session->plugin->get_extension(session->plugin, AAP_ARA_EXTENSION_URI));
        if (!transport) throw std::runtime_error("Plugin has no ARAAAP extension");
        auto* initiator = session->instance->getAAPXSDispatcher().getPluginAAPXSByUri(AAP_ARA_EXTENSION_URI);
        session->ara_client = std::make_unique<aap::xs::AraClientAAPXS>(initiator, initiator->serialization);
        ARADocumentProperties doc{sizeof(doc), "Untitled Project"};
        session->sdk = std::make_unique<aap::ara::Host>(session->plugin, session->ara_client->asPluginExtension(), &session->runtime.instance, &doc);
        session->bridge = std::make_unique<aap::ara::ScopedRemotePluginInstanceARAHostContext>(session->sdk->transportExtension(), *session->instance);
        auto* controller = session->sdk->documentController(); session->ara = controller->documentControllerInterface; session->dc = controller->documentControllerRef;
        session->binding = session->sdk->bind(kARAPlaybackRendererRole | kARAEditorRendererRole | kARAEditorViewRole, kARAPlaybackRendererRole | kARAEditorViewRole);
        session->supports_archive = true; session->supports_selection = session->binding->editorViewInterface != nullptr;
        session->instance->prepare(8, rate);
        session->ara->beginEditing(session->dc);
        ARAMusicalContextProperties context{sizeof(context), "Song", 0, nullptr};
        session->musicalContext = session->ara->createMusicalContext(session->dc, hostRef<ARAMusicalContextHostRef>(1), &context);
        session->ara->endEditing(session->dc);
        session->document_created = true;
        editor = std::move(session);
        return env->NewStringUTF("");
    } catch (const std::exception& e) { return env->NewStringUTF(e.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_closeEditor(JNIEnv*, jobject) { editor.reset(); }

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_notifyEditorSelection(
        JNIEnv* env, jobject, jintArray clipIds, jintArray trackIds, jboolean hasRange, jdouble start, jdouble duration) {
    if (!editor)
        return env->NewStringUTF("Editor is not connected");
    auto& s = *editor;
    if (!s.supports_selection)
        return env->NewStringUTF("Plugin does not support host selection");
    const auto nc = env->GetArrayLength(clipIds), nt = env->GetArrayLength(trackIds);
    // Bound Java-side allocation too; the transport has a 1 MiB shared buffer.
    if (uint64_t(nc) + nt > (ARA_SHARED_MEMORY_SIZE - sizeof(aap_ara_view_selection_wire_t)) / sizeof(int64_t))
        return env->NewStringUTF("Selection exceeds transport capacity");
    std::vector<jint> clips(nc), tracks(nt);
    env->GetIntArrayRegion(clipIds, 0, nc, clips.data());
    env->GetIntArrayRegion(trackIds, 0, nt, tracks.data());
    for (auto id : clips)
        if (!s.clips.count(id))
            return env->NewStringUTF("Unknown selected clip");
    for (auto id : tracks)
        if (!s.tracks.count(id))
            return env->NewStringUTF("Unknown selected track");
    std::vector<ARAPlaybackRegionRef> regions; std::vector<ARARegionSequenceRef> sequences;
    for (auto id : clips) regions.push_back(s.regionRefs.at(id));
    for (auto id : tracks) sequences.push_back(s.sequenceRefs.at(id));
    ARAContentTimeRange range{start, duration};
    ARAViewSelection selection{sizeof(selection), regions.size(), regions.data(), sequences.size(), sequences.data(), hasRange ? &range : nullptr};
    try { s.binding->editorViewInterface->notifySelection(s.binding->editorViewRef, &selection); return env->NewStringUTF(""); }
    catch (const std::exception& e) { return env->NewStringUTF(e.what()); }
}

// Drain on the editor worker, after the reverse Binder callback has returned.
// Alternating identity/hex archive strings keep arbitrary plugin state opaque.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_drainPluginUpdates(JNIEnv* env, jobject) {
    std::vector<std::string> result;
    if (editor && editor->supports_archive) {
        auto& s = *editor;
        s.ara->notifyModelUpdates(s.dc);
        std::vector<SampleContentUpdate> updates;
        {
            const std::lock_guard<std::mutex> lock(s.updates_mutex);
            updates.swap(s.pending_updates);
        }
        std::set<int> ids;
        for (const auto& update : updates) {
            if (update.kind == 4) for (const auto& clip : s.clips) ids.insert(clip.first);
            else if (update.kind >= 1 && update.kind <= 3 &&
                     s.clips.count(update.object_id)) ids.insert(update.object_id);
        }
        for (int id : ids) {
            std::string bytes;
            if (!s.sdk->storeModification(s.modificationRefs.at(id), bytes)) {
                const std::lock_guard<std::mutex> lock(s.updates_mutex); s.pending_updates.push_back({2, id}); continue;
            }
            auto state = archive_hex(bytes.data(), bytes.size());
            if (s.clips.at(id).plugin_state == state) continue;
            s.clips.at(id).plugin_state = state;
            result.push_back(std::to_string(id)); result.push_back(state);
        }
    }
    auto strings = env->FindClass("java/lang/String");
    auto array = env->NewObjectArray(result.size(), strings, nullptr);
    for (size_t i = 0; i < result.size(); ++i) {
        auto value = env->NewStringUTF(result[i].c_str());
        env->SetObjectArrayElement(array, i, value); env->DeleteLocalRef(value);
    }
    env->DeleteLocalRef(strings);
    return array;
}

// Rows are passed as Java objects rather than a custom wire format. All IDs are
// stable across edits; unchanged sources retain their registered providers.
extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_syncEditor(
        JNIEnv* env, jobject, jstring title, jintArray trackIds, jobjectArray trackNames,
        jintArray clipIds, jintArray clipTracks, jobjectArray clipNames, jdoubleArray values, jobjectArray pluginStates) {
    if (!editor) return env->NewStringUTF("Editor is not connected");
    auto& s = *editor;
    auto nt = env->GetArrayLength(trackIds), nc = env->GetArrayLength(clipIds);
    if (env->GetArrayLength(trackNames) != nt || env->GetArrayLength(clipTracks) != nc ||
        env->GetArrayLength(clipNames) != nc || env->GetArrayLength(values) != nc * 5 || env->GetArrayLength(pluginStates) != nc)
        return env->NewStringUTF("Invalid model arrays");
    std::vector<jint> tids(nt), ids(nc), parents(nc);
    std::vector<jdouble> v(nc * 5);
    env->GetIntArrayRegion(trackIds, 0, nt, tids.data());
    env->GetIntArrayRegion(clipIds, 0, nc, ids.data());
    env->GetIntArrayRegion(clipTracks, 0, nc, parents.data());
    env->GetDoubleArrayRegion(values, 0, nc * 5, v.data());
    for (int i = 0; i < nc; ++i) {
        if (std::find(tids.begin(), tids.end(), parents[i]) == tids.end() ||
            !std::isfinite(v[i*5]) || !std::isfinite(v[i*5+1]) || !std::isfinite(v[i*5+2]) ||
            !std::isfinite(v[i*5+3]) || !std::isfinite(v[i*5+4]) || v[i*5] < 0 || v[i*5+1] < 0 ||
            v[i*5+2] <= 0 || v[i*5+1] + v[i*5+2] > 60 || v[i*5+3] < 20 ||
            v[i*5+3] > 20000 || v[i*5+4] < 0 || v[i*5+4] > 1)
            return env->NewStringUTF("Invalid clip properties");
    }
    auto nameAt = [&](jobjectArray array, int i) {
        auto item = static_cast<jstring>(env->GetObjectArrayElement(array, i));
        auto name = jstringToStdString(env, item); env->DeleteLocalRef(item); return name;
    };
    auto* a = s.ara; auto p = s.dc;
    std::vector<std::string> states;
    std::vector<std::vector<uint8_t>> archives(nc);
    for (int i = 0; i < nc; ++i) {
        states.push_back(nameAt(pluginStates, i));
        if (!archive_bytes(states.back(), archives[i])) return env->NewStringUTF("Invalid plugin state archive");
        if (!s.supports_archive && !states.back().empty()) return env->NewStringUTF("Plugin does not support modification archives");
    }
    a->beginEditing(p);
    auto name = jstringToStdString(env, title);
    ARADocumentProperties doc{sizeof(doc), name.c_str()};
    a->updateDocumentProperties(p, &doc);
    for (auto it = s.clips.begin(); it != s.clips.end();) {
        int id = (it++)->first;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) s.removeClip(id);
    }
    // Create new sequences before moving clips; remove obsolete sequences last.
    for (int i = 0; i < nt; ++i) {
        auto n = nameAt(trackNames, i), persistent = "track-" + std::to_string(tids[i]);
        ARARegionSequenceProperties props{sizeof(props), n.c_str(), i, s.musicalContext, nullptr};
        if (s.tracks.count(tids[i])) a->updateRegionSequenceProperties(p, s.sequenceRefs.at(tids[i]), &props);
        else s.sequenceRefs[tids[i]] = a->createRegionSequence(p, hostRef<ARARegionSequenceHostRef>(tids[i]), &props);
        s.tracks[tids[i]] = n;
    }
    for (int i = 0; i < nc; ++i) {
        int id = ids[i]; bool fresh = !s.clips.count(id);
        auto n = nameAt(clipNames, i), persistent = "clip-" + std::to_string(id);
        auto& c = s.clips[id];
        bool changed = fresh || c.frequency != float(v[i*5+3]) || c.gain != float(v[i*5+4]);
        c.track = parents[i]; c.name = n; c.start = v[i*5]; c.offset = v[i*5+1]; c.duration = v[i*5+2];
        c.frequency = v[i*5+3]; c.gain = v[i*5+4];
        if (fresh) {
            c.provider = std::make_shared<ToneProvider>(s.rate);
            s.runtime.registerAudioSource(id, c.provider);
            ARAAudioSourceProperties source{sizeof(source), n.c_str(), persistent.c_str(), s.rate * 60, double(s.rate), 2, kARAFalse, kARAChannelArrangementUndefined, nullptr};
            s.sourceRefs[id] = a->createAudioSource(p, hostRef<ARAAudioSourceHostRef>(id), &source);
            ARAAudioModificationProperties modification{sizeof(modification), n.c_str(), persistent.c_str()};
            s.modificationRefs[id] = a->createAudioModification(p, s.sourceRefs.at(id), hostRef<ARAAudioModificationHostRef>(id), &modification);
        }
        c.provider->frequency = c.frequency; c.provider->gain = c.gain;
        ARAPlaybackRegionProperties region{sizeof(region), kARAPlaybackTransformationNoChanges, c.offset, c.duration, c.start, c.duration, nullptr, s.sequenceRefs.at(c.track), n.c_str(), nullptr};
        if (fresh) { s.regionRefs[id] = a->createPlaybackRegion(p, s.modificationRefs.at(id), hostRef<ARAPlaybackRegionHostRef>(id), &region);
            s.binding->playbackRendererInterface->addPlaybackRegion(s.binding->playbackRendererRef, s.regionRefs.at(id)); }
        else {
            a->updatePlaybackRegionProperties(p, s.regionRefs.at(id), &region);
            ARAAudioSourceProperties source{sizeof(source), n.c_str(), persistent.c_str(), s.rate * 60, double(s.rate), 2, kARAFalse, kARAChannelArrangementUndefined, nullptr};
            a->updateAudioSourceProperties(p, s.sourceRefs.at(id), &source);
            ARAAudioModificationProperties modification{sizeof(modification), n.c_str(), persistent.c_str()};
            a->updateAudioModificationProperties(p, s.modificationRefs.at(id), &modification);
        }
        if (fresh) a->enableAudioSourceSamplesAccess(p, s.sourceRefs.at(id), kARATrue);
        else if (changed) { ARAContentTimeRange range{0, 60}; a->updateAudioSourceContent(p, s.sourceRefs.at(id), &range, kARAContentUpdateEverythingChanged); }
        if (s.supports_archive && c.plugin_state != states[i]) {
            if (!s.sdk->restoreModification(s.modificationRefs.at(id), archives[i].data(), archives[i].size())) {
                a->endEditing(p);
                return env->NewStringUTF("Plugin rejected modification archive");
            }
            c.plugin_state = states[i];
        }
    }
    for (auto it = s.tracks.begin(); it != s.tracks.end();) {
        if (std::find(tids.begin(), tids.end(), it->first) == tids.end()) {
            a->destroyRegionSequence(p, s.sequenceRefs.at(it->first)); s.sequenceRefs.erase(it->first); it = s.tracks.erase(it);
        } else ++it;
    }
    a->endEditing(p);
    return env->NewStringUTF("");
}

// Test-only observation of the plugin's retained diagnostic output. In particular,
// disabling a different source must not suppress this source's content reread.
extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_verifyEditorAudio(
        JNIEnv* env, jobject, jint clipId, jboolean postChange, jdouble modificationGain) {
    if (!editor || !editor->clips.count(clipId)) return env->NewStringUTF("Missing editor clip");
    auto& s = *editor;
    std::array<float, 256> samples{};
    void* buffers[]{samples.data(), samples.data() + 128};
    s.clips.at(clipId).provider->readSamples(64, 128, buffers, false);
    auto expected = [&](const float* data) { return std::array<float, 4>{data[0], data[1], compute_rms(data, 128), compute_peak(data, 128)}; };
    auto left = expected(samples.data()), right = expected(samples.data() + 128);
    for (int i = 0; i < 4; ++i) { left[i] *= modificationGain; right[i] *= modificationGain; }
    bool matched = false;
    s.instance->activate();
    for (int attempt = 0; attempt < 50 && !matched; ++attempt) {
        s.instance->process(8, 0);
        auto* buffer = s.instance->getAudioPluginBuffer();
        int output = 0; matched = true;
        for (int i = 0; i < s.instance->getNumPorts(); ++i) {
            auto* port = s.instance->getPort(i);
            if (!port || port->getContentType() != AAP_CONTENT_TYPE_AUDIO || port->getPortDirection() != AAP_PORT_DIRECTION_OUTPUT) continue;
            auto* data = static_cast<float*>(buffer->get_buffer(buffer, i));
            if (!data || output >= 2) continue;
            std::array<float, 4> actual{};
            std::copy_n(data + (postChange ? 4 : 0), 4, actual.begin());
            matched = matched && metrics_match(actual, output == 0 ? left : right);
            ++output;
        }
        matched = matched && output == 2;
        if (!matched) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    s.instance->deactivate();
    return env->NewStringUTF(matched ? "" : "Editor source analysis did not match its content");
}
