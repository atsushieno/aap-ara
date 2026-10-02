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
#include "aap/ext/ara.h"
#include "aap/ara/host-runtime.h"
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
constexpr aap_ara_sample_count_t kAudioSourceLength = 48000;
constexpr aap_ara_sample_position_t kAnalysisStart = 64;
constexpr aap_ara_sample_count_t kAnalysisCount = 128;
constexpr aap_ara_sample_count_t kContentChangeCount = 2048;

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

class DeterministicAudioSourceProvider : public aap::ara::AudioSourceContentProvider {
    HostAudioStore& store;
    int32_t sample_rate;

public:
    DeterministicAudioSourceProvider(HostAudioStore& store_, int32_t sampleRate)
            : store(store_), sample_rate(sampleRate) {}

    int32_t channelCount() const override { return 2; }
    aap_ara_sample_rate_t sampleRate() const override { return sample_rate; }
    aap_ara_sample_count_t sampleCount() const override { return kAudioSourceLength; }
    uint32_t supportedSampleFormats() const override { return AAP_ARA_SAMPLE_FORMAT_FLOAT32; }

    bool readSamples(
            const aap_ara_audio_source_sample_range_t& sampleRange,
            aap_ara_audio_source_samples_buffer_t& destination) override {
        if (sampleRange.sample_format != AAP_ARA_SAMPLE_FORMAT_FLOAT32)
            return false;
        auto frames = std::max<int64_t>(0, std::min<int64_t>(sampleRange.sample_count, kAudioSourceLength - sampleRange.start_sample));
        auto channels = std::min(2, sampleRange.channel_count);
        auto bytes_per_channel = static_cast<size_t>(frames) * sizeof(float);
        auto total_size = bytes_per_channel * static_cast<size_t>(channels);
        if (!destination.data || destination.data_size < total_size) {
            destination.data_size = 0;
            destination.sample_count = 0;
            return false;
        }
        auto* base = static_cast<uint8_t*>(destination.data);
        auto& current = store.current();
        for (int ch = 0; ch < channels; ++ch) {
            memcpy(base + bytes_per_channel * static_cast<size_t>(ch),
                   current[ch].data() + sampleRange.start_sample,
                   bytes_per_channel);
        }
        destination.data_size = total_size;
        destination.sample_count = frames;
        destination.channel_count = channels;
        destination.sample_format = AAP_ARA_SAMPLE_FORMAT_FLOAT32;
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
        aap_ara_sample_position_t start_sample,
        aap_ara_sample_count_t sample_count) {
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
    aap::ara::HostRuntime runtime;
    std::shared_ptr<DeterministicAudioSourceProvider> source_provider;

    explicit HostScenarioContext(int32_t sample_rate) : store(sample_rate) {
        runtime.setCapabilities(
                AAP_ARA_API_GENERATION_2,
                AAP_ARA_ROLE_EDITOR_RENDERER,
                AAP_ARA_SAMPLE_FORMAT_FLOAT32);
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
        auto* client = new aap::PluginClient(connections, &pluginList, registry);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "PluginClient created");
        // createInstance() no longer takes sampleRate (it is passed to prepare() below).
        auto result = client->createInstance(pluginIdString, true);
        aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                     "createInstance returned instanceId=%d error=%s",
                     result.value, result.error.c_str());
        if (!result.error.empty()) {
            delete client;
            std::string message = "Failed to create plugin instance: " + result.error;
            return env->NewStringUTF(message.c_str());
        }

        auto* instance = dynamic_cast<aap::RemotePluginInstance*>(client->getInstanceById(result.value));
        if (!instance) {
            delete client;
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
            delete client;
            return env->NewStringUTF("The plugin did not expose the ARA extension.");
        }
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Resolved plugin ARA extension");

        aap_ara_factory_capability_t capability{};
        capability.struct_size = sizeof(capability);
        ara->get_factory_capability(ara, plugin, &capability);

        report << "Plugin: " << pluginIdString << "\n";
        report << "Factory capability:\n";
        report << "  api_generations=" << capability.api_generations << "\n";
        report << "  role_flags=" << capability.role_flags << "\n";
        report << "  playback_flags=" << capability.supported_playback_transformation_flags << "\n";
        report << "Host sample source:\n";
        report << "  analysis_range=[" << kAnalysisStart << ", " << (kAnalysisStart + kAnalysisCount) << ")\n";
        report << "  content_change=[" << 0 << ", " << kContentChangeCount << ")\n";

        constexpr aap_ara_document_id_t documentId = 1;
        constexpr aap_ara_musical_context_id_t musicalContextId = 10;
        constexpr aap_ara_region_sequence_id_t regionSequenceId = 20;
        constexpr aap_ara_audio_source_id_t audioSourceId = 30;
        constexpr aap_ara_audio_modification_id_t audioModificationId = 40;
        constexpr aap_ara_playback_region_id_t playbackRegionId = 50;

        HostScenarioContext host_context(sampleRate);
        host_context.runtime.registerAudioSource(audioSourceId, host_context.source_provider);
        aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                     "Registered host audio source id=%d", audioSourceId);
        auto host_bridge = std::make_unique<aap::ara::ScopedRemotePluginInstanceARAHostContext>(host_context.runtime, *instance);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Installed scoped host ARA extension bridge");

        aap_ara_color_t contextColor{0.2f, 0.6f, 0.9f};
        aap_ara_color_t regionColor{0.8f, 0.4f, 0.1f};
        aap_ara_color_t playbackColor{0.1f, 0.8f, 0.3f};

        aap_ara_document_properties_t documentProperties{
                sizeof(aap_ara_document_properties_t),
                "Sample Document",
                "sample-document"
        };
        aap_ara_musical_context_properties_t contextProperties{
                sizeof(aap_ara_musical_context_properties_t),
                "Song",
                &contextColor,
                "song-context"
        };
        aap_ara_region_sequence_properties_t sequenceProperties{
                sizeof(aap_ara_region_sequence_properties_t),
                "Lead Vox",
                0,
                musicalContextId,
                &regionColor,
                "lead-vox-sequence"
        };
        aap_ara_audio_source_properties_t audioSourceProperties{
                sizeof(aap_ara_audio_source_properties_t),
                "Lead Vox Take",
                "lead-vox-source",
                kAudioSourceLength,
                static_cast<double>(sampleRate),
                2,
                false,
                "stereo"
        };
        aap_ara_audio_modification_properties_t modificationProperties{
                sizeof(aap_ara_audio_modification_properties_t),
                "Lead Vox Edit",
                "lead-vox-edit"
        };
        aap_ara_playback_region_properties_t playbackProperties{
                sizeof(aap_ara_playback_region_properties_t),
                AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_NONE,
                0.0,
                1.0,
                0.0,
                1.0,
                regionSequenceId,
                "Lead Vox Region",
                &playbackColor
        };

        report << "\nCreating ARA model...\n";
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Beginning ARA create/update transaction");
        ara->begin_model_update(ara, plugin, AAP_ARA_MODEL_UPDATE_FLAG_NONE);
        ara->create_document(ara, plugin, documentId, &documentProperties);
        ara->create_musical_context(ara, plugin, documentId, musicalContextId, &contextProperties);
        ara->create_region_sequence(ara, plugin, documentId, regionSequenceId, &sequenceProperties);
        ara->create_audio_source(ara, plugin, documentId, audioSourceId, &audioSourceProperties);
        ara->create_audio_modification(ara, plugin, audioSourceId, audioModificationId, &modificationProperties);
        ara->create_playback_region(ara, plugin, audioModificationId, playbackRegionId, &playbackProperties);

        documentProperties.name = "Sample Document Updated";
        sequenceProperties.order_index = 1;
        audioSourceProperties.sample_count = kAudioSourceLength;
        modificationProperties.name = "Lead Vox Edit Updated";
        playbackProperties.duration_in_modification_time = 2.0;
        playbackProperties.duration_in_playback_time = 2.0;

        ara->update_document_properties(ara, plugin, documentId, &documentProperties);
        ara->update_region_sequence_properties(ara, plugin, regionSequenceId, &sequenceProperties);
        ara->update_audio_source_properties(ara, plugin, audioSourceId, &audioSourceProperties);
        ara->update_audio_modification_properties(ara, plugin, audioModificationId, &modificationProperties);
        ara->update_playback_region_properties(ara, plugin, playbackRegionId, &playbackProperties);
        ara->end_model_update(ara, plugin);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Completed ARA create/update transaction");
        report << "  create/update transaction completed\n";

        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Preparing instance");
        instance->prepare(8, sampleRate);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Enabling audio source samples access");
        ara->enable_audio_source_samples_access(ara, plugin, audioSourceId, true);
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
        ara->notify_audio_source_content_changed(ara, plugin, audioSourceId, 0, kContentChangeCount);
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
        ara->enable_audio_source_samples_access(ara, plugin, audioSourceId, false);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Disabled audio source samples access");
        report << "  sample access enabled=false\n";

        report << "\nDestroying ARA model...\n";
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Beginning ARA destroy transaction");
        ara->begin_model_update(ara, plugin, AAP_ARA_MODEL_UPDATE_FLAG_NONE);
        ara->destroy_playback_region(ara, plugin, playbackRegionId);
        ara->destroy_audio_modification(ara, plugin, audioModificationId);
        ara->destroy_audio_source(ara, plugin, audioSourceId);
        ara->destroy_region_sequence(ara, plugin, regionSequenceId);
        ara->destroy_musical_context(ara, plugin, musicalContextId);
        ara->destroy_document(ara, plugin, documentId);
        ara->end_model_update(ara, plugin);
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Completed ARA destroy transaction");
        report << "  destroy transaction completed\n";

        host_context.runtime.unregisterAudioSource(audioSourceId);
        aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                     "Unregistered host audio source id=%d", audioSourceId);
        host_bridge.reset();
        client->destroyInstance(instance);
        delete client;
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
class ToneProvider : public aap::ara::AudioSourceContentProvider {
    int rate;
public:
    float frequency = 220, gain = 0.2f;
    explicit ToneProvider(int rate_) : rate(rate_) {}
    int32_t channelCount() const override { return 2; }
    double sampleRate() const override { return rate; }
    aap_ara_sample_count_t sampleCount() const override { return rate * 60; }
    uint32_t supportedSampleFormats() const override { return AAP_ARA_SAMPLE_FORMAT_FLOAT32; }
    bool readSamples(const aap_ara_audio_source_sample_range_t& r,
                     aap_ara_audio_source_samples_buffer_t& d) override {
        if (r.sample_format != AAP_ARA_SAMPLE_FORMAT_FLOAT32 || r.start_sample < 0 ||
            r.sample_count <= 0 || r.channel_count <= 0 || r.start_sample >= sampleCount()) return false;
        auto frames = std::min(r.sample_count, sampleCount() - r.start_sample);
        auto channels = std::min(r.channel_count, 2);
        size_t bytes = size_t(frames) * channels * sizeof(float);
        if (!d.data || d.data_size < bytes) return false;
        auto* data = static_cast<float*>(d.data);
        for (int c = 0; c < channels; ++c)
            for (int i = 0; i < frames; ++i)
                data[c * frames + i] = gain * std::sin(6.283185307179586 * frequency * (r.start_sample + i) / rate);
        d.data_size = bytes; d.sample_count = frames; d.channel_count = channels;
        d.sample_format = AAP_ARA_SAMPLE_FORMAT_FLOAT32;
        return true;
    }
};
struct EditorClip { int track; std::string name, plugin_state; double start, offset, duration; float frequency, gain;
    std::shared_ptr<ToneProvider> provider; };
struct EditorSession {
    std::unique_ptr<aap::PluginClient> client;
    aap::RemotePluginInstance* instance{};
    aap::ara::HostRuntime runtime;
    std::unique_ptr<aap::ara::ScopedRemotePluginInstanceARAHostContext> bridge;
    // Worker-owned facade for the editor's serialized model calls.
    std::unique_ptr<aap::xs::AraClientAAPXS> ara_client;
    aap_ara_extension_t* ara{};
    AndroidAudioPlugin* plugin{};
    int rate;
    std::map<int, std::string> tracks;
    std::map<int, EditorClip> clips;
    bool document_created{};
    bool supports_archive = false;
    std::mutex updates_mutex;
    std::vector<aap_ara_content_update_t> pending_updates;
    explicit EditorSession(int rate_) : rate(rate_) {
        runtime.setContentUpdateHandler([this](const aap_ara_content_update_t& update) {
            const std::lock_guard<std::mutex> lock(updates_mutex);
            pending_updates.push_back(update);
        });
    }
    void removeClip(int id) {
        ara->destroy_playback_region(ara, plugin, id);
        ara->destroy_audio_modification(ara, plugin, id);
        ara->enable_audio_source_samples_access(ara, plugin, id, false);
        ara->destroy_audio_source(ara, plugin, id);
        runtime.unregisterAudioSource(id);
        clips.erase(id);
    }
    ~EditorSession() {
        runtime.setContentUpdateHandler({});
        if (document_created) {
            ara->begin_model_update(ara, plugin, 0);
            while (!clips.empty()) removeClip(clips.begin()->first);
            for (auto& t : tracks) ara->destroy_region_sequence(ara, plugin, t.first);
            ara->destroy_musical_context(ara, plugin, 1);
            ara->destroy_document(ara, plugin, 1);
            ara->end_model_update(ara, plugin);
        }
        bridge.reset();
        ara_client.reset();
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
    if (text.size() % 2 || text.size() > AAP_ARA_MAX_ARCHIVE_BYTES * 2) return false;
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
        session->ara = static_cast<aap_ara_extension_t*>(session->plugin->get_extension(session->plugin, AAP_ARA_EXTENSION_URI));
        if (!session->ara) throw std::runtime_error("Plugin has no ARA extension");
        auto* initiator = session->instance->getAAPXSDispatcher().getPluginAAPXSByUri(AAP_ARA_EXTENSION_URI);
        session->ara_client = std::make_unique<aap::xs::AraClientAAPXS>(initiator, initiator->serialization);
        session->ara = session->ara_client->asPluginExtension();
        aap_ara_factory_capability_t capability{};
        capability.struct_size = sizeof(capability);
        session->ara->get_factory_capability(session->ara, session->plugin, &capability);
        session->supports_archive = capability.struct_size >= sizeof(capability) &&
            (capability.supported_features & AAP_ARA_FEATURE_MODIFICATION_ARCHIVE);
        session->bridge = std::make_unique<aap::ara::ScopedRemotePluginInstanceARAHostContext>(session->runtime, *session->instance);
        session->instance->prepare(8, rate);
        session->ara->begin_model_update(session->ara, session->plugin, 0);
        aap_ara_document_properties_t doc{sizeof(doc), "Untitled Project", "editor-document"};
        aap_ara_musical_context_properties_t context{sizeof(context), "Song", nullptr, "editor-song"};
        session->ara->create_document(session->ara, session->plugin, 1, &doc);
        session->ara->create_musical_context(session->ara, session->plugin, 1, 1, &context);
        session->ara->end_model_update(session->ara, session->plugin);
        session->document_created = true;
        editor = std::move(session);
        return env->NewStringUTF("");
    } catch (const std::exception& e) { return env->NewStringUTF(e.what()); }
}

extern "C" JNIEXPORT void JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_closeEditor(JNIEnv*, jobject) { editor.reset(); }

// Drain on the editor worker, after the reverse Binder callback has returned.
// Alternating identity/hex archive strings keep arbitrary plugin state opaque.
extern "C" JNIEXPORT jobjectArray JNICALL
Java_org_androidaudioplugin_aaparahostsample_AraHostSampleNative_drainPluginUpdates(JNIEnv* env, jobject) {
    std::vector<std::string> result;
    if (editor && editor->supports_archive) {
        auto& s = *editor;
        std::vector<aap_ara_content_update_t> updates;
        {
            const std::lock_guard<std::mutex> lock(s.updates_mutex);
            updates.swap(s.pending_updates);
        }
        std::set<int> ids;
        for (const auto& update : updates) {
            if (update.kind == AAP_ARA_CONTENT_DOCUMENT) for (const auto& clip : s.clips) ids.insert(clip.first);
            else if (update.kind >= AAP_ARA_CONTENT_AUDIO_SOURCE && update.kind <= AAP_ARA_CONTENT_PLAYBACK_REGION &&
                     s.clips.count(update.object_id)) ids.insert(update.object_id);
        }
        for (int id : ids) {
            std::array<uint8_t, AAP_ARA_MAX_ARCHIVE_BYTES> bytes{};
            aap_ara_archive_buffer_t archive{sizeof(archive), bytes.data(), bytes.size(), 0};
            if (!s.ara->store_audio_modification_state(s.ara, s.plugin, id, &archive)) {
                // A failed fetch must not lose the dirty notification.
                const std::lock_guard<std::mutex> lock(s.updates_mutex);
                s.pending_updates.push_back({sizeof(aap_ara_content_update_t), AAP_ARA_CONTENT_AUDIO_MODIFICATION, id, 0, false, 0, 0});
                continue;
            }
            auto state = archive_hex(bytes.data(), archive.data_size);
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
    auto* a = s.ara; auto* p = s.plugin;
    std::vector<std::string> states;
    std::vector<std::vector<uint8_t>> archives(nc);
    for (int i = 0; i < nc; ++i) {
        states.push_back(nameAt(pluginStates, i));
        if (!archive_bytes(states.back(), archives[i])) return env->NewStringUTF("Invalid plugin state archive");
        if (!s.supports_archive && !states.back().empty()) return env->NewStringUTF("Plugin does not support modification archives");
    }
    a->begin_model_update(a, p, 0);
    auto name = jstringToStdString(env, title);
    aap_ara_document_properties_t doc{sizeof(doc), name.c_str(), "editor-document"};
    a->update_document_properties(a, p, 1, &doc);
    for (auto it = s.clips.begin(); it != s.clips.end();) {
        int id = (it++)->first;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) s.removeClip(id);
    }
    // Create new sequences before moving clips; remove obsolete sequences last.
    for (int i = 0; i < nt; ++i) {
        auto n = nameAt(trackNames, i), persistent = "track-" + std::to_string(tids[i]);
        aap_ara_region_sequence_properties_t props{sizeof(props), n.c_str(), i, 1, nullptr, persistent.c_str()};
        if (s.tracks.count(tids[i])) a->update_region_sequence_properties(a, p, tids[i], &props);
        else a->create_region_sequence(a, p, 1, tids[i], &props);
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
            aap_ara_audio_source_properties_t source{sizeof(source), n.c_str(), persistent.c_str(), s.rate * 60, double(s.rate), 2, false, "stereo"};
            a->create_audio_source(a, p, 1, id, &source);
            aap_ara_audio_modification_properties_t modification{sizeof(modification), n.c_str(), persistent.c_str()};
            a->create_audio_modification(a, p, id, id, &modification);
        }
        c.provider->frequency = c.frequency; c.provider->gain = c.gain;
        aap_ara_playback_region_properties_t region{sizeof(region), 0, c.offset, c.duration, c.start, c.duration, c.track, n.c_str(), nullptr};
        if (fresh) a->create_playback_region(a, p, id, id, &region);
        else {
            a->update_playback_region_properties(a, p, id, &region);
            aap_ara_audio_source_properties_t source{sizeof(source), n.c_str(), persistent.c_str(), s.rate * 60, double(s.rate), 2, false, "stereo"};
            a->update_audio_source_properties(a, p, id, &source);
            aap_ara_audio_modification_properties_t modification{sizeof(modification), n.c_str(), persistent.c_str()};
            a->update_audio_modification_properties(a, p, id, &modification);
        }
        if (fresh) a->enable_audio_source_samples_access(a, p, id, true);
        else if (changed) a->notify_audio_source_content_changed(a, p, id, 0, s.rate * 60);
        if (s.supports_archive && c.plugin_state != states[i]) {
            if (!a->restore_audio_modification_state(a, p, id, archives[i].data(), archives[i].size())) {
                a->end_model_update(a, p);
                return env->NewStringUTF("Plugin rejected modification archive");
            }
            c.plugin_state = states[i];
        }
    }
    for (auto it = s.tracks.begin(); it != s.tracks.end();) {
        if (std::find(tids.begin(), tids.end(), it->first) == tids.end()) {
            a->destroy_region_sequence(a, p, it->first); it = s.tracks.erase(it);
        } else ++it;
    }
    a->end_model_update(a, p);
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
    aap_ara_audio_source_sample_range_t range{sizeof(range), 64, 128, 2, AAP_ARA_SAMPLE_FORMAT_FLOAT32};
    aap_ara_audio_source_samples_buffer_t destination{sizeof(destination), samples.data(), sizeof(samples), 0, 0, 0};
    s.clips.at(clipId).provider->readSamples(range, destination);
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
