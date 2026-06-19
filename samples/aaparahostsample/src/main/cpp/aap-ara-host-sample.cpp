#include <jni.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "aap/core/host/plugin-host.h"
#include "aap/core/host/plugin-instance.h"
#include "aap/ext/ara.h"
#include "aap/ara/host-runtime.h"
#include "aap/ara/registry.h"
#include "aap/unstable/logging.h"

#define LOG_TAG "AAP.ARA.HostSample"

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
        aap::ara::ScopedRemotePluginInstanceARAHostContext host_bridge(host_context.runtime, *instance);
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
