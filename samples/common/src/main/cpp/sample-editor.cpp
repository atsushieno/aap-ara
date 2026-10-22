#include "sample-editor.h"
#include "sample-project.h"
#include "upstream-scenarios.h"
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
#include "aap/ara/araaap.hpp"
#include <type_traits>

using aap::ara::hostRef;
using aap::ara::hostID;
#include "aap/ara/remote-host.h"
#include "aap/ara/registry.h"
#include "aap/core/aapxs/ara-aapxs.h"
#include "aap/unstable/logging.h"

#define LOG_TAG "AAP.ARA.SampleEditor"

namespace {
constexpr ARASampleCount kAudioSourceLength = 48000;
constexpr ARASamplePosition kAnalysisStart = 64;
constexpr ARASampleCount kAnalysisCount = 128;
constexpr ARASampleCount kContentChangeCount = 2048;

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

std::string sample_editor::runScenario(aap::PluginClientConnectionList* connections,
        aap::PluginListSnapshot& pluginList, const std::string& pluginIdString, int sampleRate) {
    std::ostringstream report;
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "runScenario start pluginId=%s sampleRate=%d",
                 pluginIdString.c_str(), sampleRate);

    try {
        if (!connections)
            return "Failed to resolve service connection list.";
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Resolved plugin connection list");

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
            return message;
        }

        auto* instance = dynamic_cast<aap::RemotePluginInstance*>(client->getInstanceById(result.value));
        if (!instance) {
            client.reset();
            return "Plugin instance was created but could not be resolved.";
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
            return "The plugin did not expose the ARA extension.";
        }
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "Resolved plugin ARA extension");

        HostScenarioContext host_context(sampleRate);
        host_context.runtime.registerAudioSource(hostRef<ARAAudioSourceHostRef>(30), host_context.source_provider);
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
        report << "\nAudio fetch result: " << (verified ? "PASS" : "FAIL") << "\n";
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
        auto upstream = sample_scenarios::runUpstreamScenarios(connections, pluginIdString);
        const bool allPassed = verified && upstream.find("Upstream scenarios: PASS (8/8)") != std::string::npos;
        aap::a_log(AAP_LOG_LEVEL_INFO, LOG_TAG, "runScenario completed successfully");

        // Lead with the complete result and case list in the editor's small status panel.
        return std::string("Result: ") + (allPassed ? "PASS" : "FAIL") + "\n" + upstream +
                "\nAudio fetch details:\n" + report.str();
    } catch (const std::exception& ex) {
        aap::a_log_f(AAP_LOG_LEVEL_ERROR, LOG_TAG,
                     "Scenario failed with native exception: %s", ex.what());
        std::string message = std::string("Scenario failed with native exception: ") + ex.what();
        return message;
    } catch (...) {
        aap::a_log(AAP_LOG_LEVEL_ERROR, LOG_TAG, "Scenario failed with unknown native exception");
        return "Scenario failed with an unknown native exception.";
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
struct EditorClip {
    std::string plugin_state;
    std::shared_ptr<ToneProvider> provider;
};
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
    std::map<int, EditorClip> clips;
    bool document_created{};
    bool supports_archive = false;
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
        runtime.unregisterAudioSource(hostRef<ARAAudioSourceHostRef>(id)); clips.erase(id); regionRefs.erase(id); modificationRefs.erase(id); sourceRefs.erase(id);
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

int sample_editor::editorInstanceId() {
    return editor && editor->instance ? editor->instance->getInstanceId() : -1;
}

std::string sample_editor::openEditor(aap::PluginClientConnectionList* connections,
        aap::PluginListSnapshot& list, const std::string& pluginId, int rate) {
    try {
        editor.reset();
        auto session = std::make_unique<EditorSession>(rate);
        if (!connections) throw std::runtime_error("No service connection");
        auto* registry = aap::xs::AAPXSDefinitionRegistry::getStandardExtensions();
        aap::ara::addToRegistry(*registry);
        session->client = std::make_unique<aap::PluginClient>(connections, &list, registry);
        auto result = session->client->createInstance(pluginId, true);
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
        session->supports_archive = true;
        session->instance->prepare(8, rate);
        session->ara->beginEditing(session->dc);
        ARAMusicalContextProperties context{sizeof(context), "Song", 0, nullptr};
        session->musicalContext = session->ara->createMusicalContext(session->dc, hostRef<ARAMusicalContextHostRef>(1), &context);
        session->ara->endEditing(session->dc);
        session->document_created = true;
        editor = std::move(session);
        return "";
    } catch (const std::exception& e) { return e.what(); }
}

void sample_editor::closeEditor() { editor.reset(); }

const ARADocumentControllerInstance* sample_editor::documentController() {
    return editor ? editor->sdk->documentController() : nullptr;
}
const ARAPlugInExtensionInstance* sample_editor::plugInExtension() {
    return editor ? editor->binding : nullptr;
}
ARASampleRate sample_editor::sampleRate() { return editor ? editor->rate : 48000; }
ARARegionSequenceRef sample_editor::regionSequenceRef(ARARegionSequenceHostRef ref) {
    if (!editor) return nullptr;
    auto found = editor->sequenceRefs.find(hostID(ref));
    return found == editor->sequenceRefs.end() ? nullptr : found->second;
}
ARAPlaybackRegionRef sample_editor::playbackRegionRef(ARAPlaybackRegionHostRef ref) {
    if (!editor) return nullptr;
    auto found = editor->regionRefs.find(hostID(ref));
    return found == editor->regionRefs.end() ? nullptr : found->second;
}
std::string sample_editor::notifyEditorSelection(const ARAViewSelection& selection) {
    auto* binding = plugInExtension();
    if (!binding) return "Editor is not connected";
    if (!binding->editorViewInterface) return "Plugin does not support host selection";
    try {
        binding->editorViewInterface->notifySelection(binding->editorViewRef, &selection);
        return "";
    } catch (const std::exception& e) { return e.what(); }
}

// Drain on the editor worker, after the reverse Binder callback has returned.
// Alternating identity/hex archive strings keep arbitrary plugin state opaque.
std::vector<std::string> sample_editor::drainPluginUpdates() {
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
            std::visit([&](auto ref) {
                using Ref = decltype(ref);
                if constexpr (std::is_same_v<Ref, std::monostate>) {
                    for (const auto& clip : s.clips) ids.insert(clip.first);
                } else {
                    auto id = hostID(ref);
                    if (s.clips.count(id)) ids.insert(id);
                }
            }, update);
        }
        for (int id : ids) {
            std::string bytes;
            if (!s.sdk->storeModification(s.modificationRefs.at(id), bytes)) {
                const std::lock_guard<std::mutex> lock(s.updates_mutex); s.pending_updates.push_back(hostRef<ARAAudioModificationHostRef>(id)); continue;
            }
            auto state = archive_hex(bytes.data(), bytes.size());
            if (s.clips.at(id).plugin_state == state) continue;
            s.clips.at(id).plugin_state = state;
            result.push_back(std::to_string(id)); result.push_back(state);
        }
    }
    return result;
}

// Stable IDs preserve registered providers across edits.
std::string sample_project::sync(const ARADocumentProperties& document,
        const std::vector<RegionSequence>& sequences, const std::vector<PlaybackRegion>& regions) {
    if (!editor) return "Editor is not connected";
    auto& s = *editor;
    std::vector<int32_t> tids, ids;
    for (const auto& sequence : sequences) tids.push_back(hostID(sequence.first));
    for (const auto& region : regions) {
        ids.push_back(hostID(region.hostRef));
        const auto& p = region.properties;
        if (std::find(tids.begin(), tids.end(), hostID(region.sequenceHostRef)) == tids.end() ||
            !std::isfinite(p.startInPlaybackTime) || !std::isfinite(p.startInModificationTime) ||
            !std::isfinite(p.durationInPlaybackTime) || !std::isfinite(p.durationInModificationTime) ||
            !std::isfinite(region.frequency) || !std::isfinite(region.gain) || p.startInPlaybackTime < 0 ||
            p.startInModificationTime < 0 || p.durationInPlaybackTime <= 0 || p.durationInModificationTime <= 0 ||
            p.startInModificationTime + p.durationInModificationTime > 60 || region.frequency < 20 ||
            region.frequency > 20000 || region.gain < 0 || region.gain > 1 ||
            region.source.sampleRate != s.rate || region.source.sampleCount != s.rate * 60 || region.source.channelCount != 2)
            return "Invalid clip properties";
    }
    const auto nc = regions.size();
    auto* a = s.ara; auto p = s.dc;
    std::vector<std::string> states;
    std::vector<std::vector<uint8_t>> archives(nc);
    for (int i = 0; i < nc; ++i) {
        states.push_back(regions[i].pluginState);
        if (!archive_bytes(states.back(), archives[i])) return "Invalid plugin state archive";
        if (!s.supports_archive && !states.back().empty()) return "Plugin does not support modification archives";
    }
    a->beginEditing(p);
    a->updateDocumentProperties(p, &document);
    for (auto it = s.clips.begin(); it != s.clips.end();) {
        int id = (it++)->first;
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) s.removeClip(id);
    }
    // Create new sequences before moving clips; remove obsolete sequences last.
    for (const auto& [ref, properties] : sequences) {
        const auto id = hostID(ref);
        auto props = properties;
        props.musicalContextRef = s.musicalContext;
        if (s.sequenceRefs.count(id)) a->updateRegionSequenceProperties(p, s.sequenceRefs.at(id), &props);
        else s.sequenceRefs[id] = a->createRegionSequence(p, ref, &props);
    }
    for (size_t i = 0; i < nc; ++i) {
        int id = ids[i];
        bool fresh = !s.clips.count(id);
        const auto& input = regions[i];
        auto& c = s.clips[id];
        bool changed = fresh || c.provider->frequency != float(input.frequency) || c.provider->gain != float(input.gain);
        if (fresh) {
            c.provider = std::make_shared<ToneProvider>(s.rate);
            s.runtime.registerAudioSource(hostRef<ARAAudioSourceHostRef>(id), c.provider);
            s.sourceRefs[id] = a->createAudioSource(p, hostRef<ARAAudioSourceHostRef>(id), &input.source);
            s.modificationRefs[id] = a->createAudioModification(p, s.sourceRefs.at(id), hostRef<ARAAudioModificationHostRef>(id), &input.modification);
        }
        c.provider->frequency = input.frequency;
        c.provider->gain = input.gain;
        auto region = input.properties;
        region.regionSequenceRef = s.sequenceRefs.at(hostID(input.sequenceHostRef));
        if (fresh) {
            s.regionRefs[id] = a->createPlaybackRegion(p, s.modificationRefs.at(id), input.hostRef, &region);
            s.binding->playbackRendererInterface->addPlaybackRegion(s.binding->playbackRendererRef, s.regionRefs.at(id));
        } else {
            a->updatePlaybackRegionProperties(p, s.regionRefs.at(id), &region);
            a->updateAudioSourceProperties(p, s.sourceRefs.at(id), &input.source);
            a->updateAudioModificationProperties(p, s.modificationRefs.at(id), &input.modification);
        }
        if (fresh) a->enableAudioSourceSamplesAccess(p, s.sourceRefs.at(id), kARATrue);
        else if (changed) {
            ARAContentTimeRange range{0, 60};
            a->updateAudioSourceContent(p, s.sourceRefs.at(id), &range, kARAContentUpdateEverythingChanged);
        }
        if (s.supports_archive && c.plugin_state != states[i]) {
            if (!s.sdk->restoreModification(s.modificationRefs.at(id), archives[i].data(), archives[i].size())) {
                a->endEditing(p);
                return "Plugin rejected modification archive";
            }
            c.plugin_state = states[i];
        }
    }
    for (auto it = s.sequenceRefs.begin(); it != s.sequenceRefs.end();) {
        if (std::find(tids.begin(), tids.end(), it->first) == tids.end()) {
            a->destroyRegionSequence(p, it->second);
            it = s.sequenceRefs.erase(it);
        } else ++it;
    }
    a->endEditing(p);
    return "";
}

// Test-only observation of the plugin's retained diagnostic output. In particular,
// disabling a different source must not suppress this source's content reread.
std::string sample_editor::verifyEditorAudio(int clipId, bool postChange, double modificationGain) {
    if (!editor || !editor->clips.count(clipId)) return "Missing editor clip";
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
    return matched ? "" : "Editor source analysis did not match its content";
}
