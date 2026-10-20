#include <jni.h>
#include <aap/android-audio-plugin.h>
#include <aap/core/host/plugin-host.h>
#include <memory>
#include <aap/ara/araaap.hpp>
#include <ARA_Library/PlugIn/ARAPlug.h>
#include <aap/ext/plugin-info.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "aap/unstable/logging.h"

#define LOG_TAG "AAP.ARA.PluginSample"

struct SamplePluginContext;

namespace {
std::map<uint64_t, std::shared_ptr<SamplePluginContext>> editable_contexts;
struct PublishedModel { std::mutex mutex; std::string json = "{}"; uint64_t revision = 0; };
std::mutex snapshots_mutex;
uint64_t next_snapshot_token = 1;
std::map<uint64_t, std::shared_ptr<PublishedModel>> snapshots;
std::map<AndroidAudioPlugin*, uint64_t> plugin_snapshot_tokens;
std::string json_string(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 32) {
            const char* hex = "0123456789abcdef";
            out << "\\u00" << hex[c >> 4] << hex[c & 15];
        } else out << c;
    }
    out << '"';
    return out.str();
}
}

using namespace ARA;
using namespace ARA::PlugIn;
using aap::ara::hostID;
using aap::ara::hostRef;

struct NamedEntity {
    std::string name;
    std::string persistent_id;
};

struct MusicalContextEntity : public NamedEntity {
    bool has_color{};
    ARAColor color{};
};

struct RegionSequenceEntity : public MusicalContextEntity {
    int32_t order_index{};
    int64_t musical_context_id{};
};

struct AudioSourceEntity : public NamedEntity {
    double rms = 0.0;
    ARASampleCount sample_count{};
    ARASampleRate sample_rate{};
    int32_t channel_count{};
    bool merits_64_bit_samples{};
    std::string channel_layout_tag;
    bool samples_access_enabled{};
    ARASamplePosition changed_start_sample{};
    ARASampleCount changed_sample_count{};
};

struct AudioModificationEntity : public NamedEntity {
    double gain = 1.0;
    int64_t audio_source_id{};
};

struct PlaybackRegionEntity {
    uint32_t transformation_flags{};
    ARATimePosition start_in_modification_time{};
    ARATimeDuration duration_in_modification_time{};
    ARATimePosition start_in_playback_time{};
    ARATimeDuration duration_in_playback_time{};
    int64_t region_sequence_id{};
    std::string name;
    bool has_color{};
    ARAColor color{};
    int64_t audio_modification_id{};
};

class SampleDocumentController;

struct SamplePluginContext {
    std::unique_ptr<ARA::PlugIn::PlugInExtension> binding = std::make_unique<ARA::PlugIn::PlugInExtension>();
    std::unique_ptr<aap::ara::PlugIn> ara;
    std::recursive_mutex model_mutex;
    bool released = false;
    bool reverse_updates_supported = false;
    int64_t analyzed_source = 0;
    std::atomic<double> diagnostic_gain{1.0};
    AndroidAudioPluginHost host;
    std::string plugin_id;
    std::array<int32_t, 2> audio_output_ports{0, 1};
    bool model_update_active{};
    uint32_t last_model_update_flags{};
    bool sample_access_enabled{};
    bool host_supports_float32{};
    std::array<float, 4> pre_change_output_l{};
    std::array<float, 4> pre_change_output_r{};
    std::array<float, 4> post_change_output_l{};
    std::array<float, 4> post_change_output_r{};
    std::string analysis_summary;
    std::mutex analysis_mutex;
    std::shared_ptr<PublishedModel> published = std::make_shared<PublishedModel>();
    uint64_t content_changes = 0;
    uint64_t selection_changes = 0;
    int64_t selection_document_id{};
    std::vector<int64_t> selected_regions, selected_sequences;
    bool selection_has_time_range{};
    double selection_start{}, selection_duration{};
    std::map<int64_t, NamedEntity> documents;
    std::map<int64_t, MusicalContextEntity> musical_contexts;
    std::map<int64_t, RegionSequenceEntity> region_sequences;
    std::map<int64_t, AudioSourceEntity> audio_sources;
    std::map<int64_t, AudioModificationEntity> audio_modifications;
    std::map<int64_t, PlaybackRegionEntity> playback_regions;

    explicit SamplePluginContext(AndroidAudioPluginHost* host_, const char* id) : host(*host_), plugin_id(id ? id : "") {}
};

static SamplePluginContext* get_context(AndroidAudioPlugin* plugin) {
    return static_cast<std::shared_ptr<SamplePluginContext>*>(plugin->plugin_specific)->get();
}

// Publish only completed transactions. The UI reads this immutable copy and
// never touches the live ARA maps or holds a lock across reverse host callbacks.
static void publish_model(SamplePluginContext* context, bool contentChanged = true) {
    const auto old_regions = context->selected_regions, old_sequences = context->selected_sequences;
    context->selected_regions.erase(std::remove_if(context->selected_regions.begin(), context->selected_regions.end(),
        [context](int64_t id) { return !context->playback_regions.count(id); }), context->selected_regions.end());
    context->selected_sequences.erase(std::remove_if(context->selected_sequences.begin(), context->selected_sequences.end(),
        [context](int64_t id) { return !context->region_sequences.count(id); }), context->selected_sequences.end());
    if (!old_sequences.empty() && context->selected_sequences.empty())
        context->selection_has_time_range = false;
    const bool removed_document = context->selection_document_id && !context->documents.count(context->selection_document_id);
    if (removed_document) {
        context->selection_document_id = 0;
        context->selection_has_time_range = false;
    }
    if (old_regions != context->selected_regions || old_sequences != context->selected_sequences || removed_document)
        ++context->selection_changes;
    auto& snapshot = *context->published;
    std::lock_guard<std::mutex> lock(snapshot.mutex);
    std::ostringstream out;
    out.precision(17);
    if (contentChanged)
        ++snapshot.revision;
    out << "{\"canEdit\":" << (context->reverse_updates_supported ? "true" : "false") << ",\"revision\":" << snapshot.revision << ",\"contentChanges\":" << context->content_changes;
    out << ",\"selectionChanges\":" << context->selection_changes << ",\"selection\":{\"clips\":[";
    for (size_t i = 0; i < context->selected_regions.size(); ++i) {
        if (i) out << ',';
        out << context->selected_regions[i];
    }
    out << "],\"tracks\":[";
    for (size_t i = 0; i < context->selected_sequences.size(); ++i) {
        if (i) out << ',';
        out << context->selected_sequences[i];
    }
    out << "],\"range\":";
    if (context->selection_has_time_range)
        out << "{\"start\":" << context->selection_start << ",\"duration\":" << context->selection_duration << '}';
    else
        out << "null";
    out << '}';
    out << ",\"title\":" << json_string(context->documents.empty() ? "No ARA document" : context->documents.begin()->second.name);
    out << ",\"tracks\":[";
    bool comma = false;
    for (auto& [id, track] : context->region_sequences) {
        if (comma) out << ',';
        comma = true;
        out << "{\"id\":" << id << ",\"name\":" << json_string(track.name) << ",\"order\":" << track.order_index << '}';
    }
    out << "],\"clips\":[";
    comma = false;
    for (auto& [id, clip] : context->playback_regions) {
        if (comma) out << ',';
        comma = true;
        auto modification = context->audio_modifications.find(clip.audio_modification_id);
        int source = modification == context->audio_modifications.end() ? 0 : modification->second.audio_source_id;
        out << "{\"id\":" << id << ",\"track\":" << clip.region_sequence_id << ",\"name\":" << json_string(clip.name)
            << ",\"start\":" << clip.start_in_playback_time << ",\"offset\":" << clip.start_in_modification_time
            << ",\"duration\":" << clip.duration_in_playback_time << ",\"sourceId\":" << source
            << ",\"modificationId\":" << clip.audio_modification_id
            << ",\"modificationGain\":" << (modification == context->audio_modifications.end() ? 1.0 : modification->second.gain)
            << ",\"processedRms\":" << (context->audio_sources.count(source) && modification != context->audio_modifications.end()
                ? context->audio_sources.at(source).rms * modification->second.gain : 0.0) << '}';
    }
    out << "],\"sources\":[";
    comma = false;
    for (auto& [id, source] : context->audio_sources) {
        if (comma) out << ',';
        comma = true;
        out << "{\"id\":" << id << ",\"name\":" << json_string(source.name) << ",\"sampleCount\":" << source.sample_count
            << ",\"sampleRate\":" << source.sample_rate << ",\"access\":" << (source.samples_access_enabled ? "true" : "false")
            << ",\"changedStart\":" << source.changed_start_sample << ",\"changedCount\":" << source.changed_sample_count << '}';
    }
    std::lock_guard<std::mutex> analysis_lock(context->analysis_mutex);
    out << "],\"analysis\":" << json_string(context->analysis_summary) << '}';
    snapshot.json = out.str();
}


struct SampleSource : AudioSource {
    using AudioSource::AudioSource;
    double rms{};
    ARASamplePosition changedStart{};
    ARASampleCount changedCount{};
};
struct SampleModification : AudioModification {
    double gain = 1.0;
    SampleModification(AudioSource* source, ARAAudioModificationHostRef host, const AudioModification* clone) noexcept
        : AudioModification(source, host, clone), gain(clone ? static_cast<const SampleModification*>(clone)->gain : 1.0) {}
};
class SampleView;
class SampleDocumentController : public DocumentController {
public:
    std::weak_ptr<SamplePluginContext> companion;
    explicit SampleDocumentController(const PlugInEntry* entry, const ARADocumentControllerHostInstance* host) noexcept : DocumentController(entry, host) {}
    void sync(bool changed = true) noexcept {
        auto c = companion.lock(); if (!c) return;
        c->documents.clear(); c->region_sequences.clear(); c->audio_sources.clear(); c->audio_modifications.clear(); c->playback_regions.clear();
        auto* document = getDocument();
        if (document) {
            c->documents[1].name = text(document->getName());
            for (auto* track : document->getRegionSequences()) {
                auto& t = c->region_sequences[hostID(track->getHostRef())]; t.name = text(track->getName()); t.order_index = track->getOrderIndex();
                for (auto* region : track->getPlaybackRegions()) {
                    auto& r = c->playback_regions[hostID(region->getHostRef())]; r.name = text(region->getName());
                    r.region_sequence_id = hostID(track->getHostRef()); r.audio_modification_id = hostID(region->getAudioModification()->getHostRef());
                    r.start_in_modification_time = region->getStartInAudioModificationTime(); r.duration_in_modification_time = region->getDurationInAudioModificationTime();
                    r.start_in_playback_time = region->getStartInPlaybackTime(); r.duration_in_playback_time = region->getDurationInPlaybackTime();
                }
            }
            for (auto* source : document->getAudioSources<SampleSource>()) {
                auto& v = c->audio_sources[hostID(source->getHostRef())]; v.name = text(source->getName()); v.rms = source->rms;
                v.sample_count = source->getSampleCount(); v.sample_rate = source->getSampleRate(); v.channel_count = source->getChannelCount();
                v.samples_access_enabled = source->isSampleAccessEnabled(); v.changed_start_sample = source->changedStart; v.changed_sample_count = source->changedCount;
                for (auto* modification : source->getAudioModifications<SampleModification>()) {
                    auto& m = c->audio_modifications[hostID(modification->getHostRef())]; m.name = text(modification->getName());
                    m.audio_source_id = hostID(source->getHostRef()); m.gain = modification->gain;
                    if (c->analyzed_source == m.audio_source_id) c->diagnostic_gain.store(m.gain);
                }
            }
        }
        c->reverse_updates_supported = getHostModelUpdateController() != nullptr;
        publish_model(c.get(), changed);
    }
    static const char* text(const OptionalProperty<ARAUtf8String>& value) { return value ? static_cast<const char*>(value) : ""; }
    bool setGain(int64_t id, double gain) {
        if (!getDocument()) return false;
        for (auto* source : getDocument()->getAudioSources()) for (auto* m : source->getAudioModifications<SampleModification>()) {
            if (hostID(m->getHostRef()) != id) continue;
            if (m->gain == gain) return true;
            m->gain = gain;
            // The SDK queues this until the host calls notifyModelUpdates().
            notifyAudioModificationContentChanged(m, ContentUpdateScopes(kARAContentUpdateEverythingChanged));
            if (auto c = companion.lock()) ++c->content_changes;
            sync(); return true;
        }
        return false;
    }
protected:
    void willBeginEditing() noexcept override { if (auto c = companion.lock()) c->model_update_active = true; }
    void didEndEditing() noexcept override { if (auto c = companion.lock()) c->model_update_active = false; sync(); }
    void willDestroyDocument(Document*) noexcept override {
        if (auto c = companion.lock()) { c->documents.clear(); c->region_sequences.clear(); c->audio_sources.clear(); c->audio_modifications.clear(); c->playback_regions.clear(); publish_model(c.get()); }
    }
    AudioSource* doCreateAudioSource(Document* doc, ARAAudioSourceHostRef host) noexcept override { return new SampleSource(doc, host); }
    AudioModification* doCreateAudioModification(AudioSource* source, ARAAudioModificationHostRef host, const AudioModification* clone) noexcept override { return new SampleModification(source, host, clone); }
    void doUpdateMusicalContextContent(MusicalContext*, const ARAContentTimeRange*, ContentUpdateScopes) noexcept override {}
    void doUpdateAudioSourceContent(AudioSource* source, const ARAContentTimeRange* range, ContentUpdateScopes) noexcept override {
        auto* s = static_cast<SampleSource*>(source);
        s->changedStart = range ? std::llround(range->start * s->getSampleRate()) : 0;
        s->changedCount = range ? std::llround(range->duration * s->getSampleRate()) : s->getSampleCount();
        if (auto c = companion.lock()) ++c->content_changes;
        if (s->isSampleAccessEnabled()) analyze(s, true);
    }
    void willEnableAudioSourceSamplesAccess(AudioSource*, bool) noexcept override {}
    void didEnableAudioSourceSamplesAccess(AudioSource* source, bool enabled) noexcept override { if (enabled) analyze(static_cast<SampleSource*>(source), false); sync(); }
    bool doStoreObjectsToArchive(HostArchiveWriter* writer, const StoreObjectsFilter* filter) noexcept override {
        std::ostringstream out; out.precision(17); out << "ARAAAP-gain-v2\n";
        for (auto* m : filter->getAudioModificationsToStore<SampleModification>()) out << m->getPersistentID() << '\t' << m->gain << '\n';
        auto bytes = out.str(); return writer->writeBytesToArchive(0, bytes.size(), reinterpret_cast<const ARAByte*>(bytes.data()));
    }
    bool doRestoreObjectsFromArchive(HostArchiveReader* reader, const RestoreObjectsFilter* filter) noexcept override {
        auto size = reader->getArchiveSize();
        if (!size) { // The sample's empty archive means default gain for the selected modification(s).
            for (auto* source : getDocument()->getAudioSources()) for (auto* m : source->getAudioModifications<SampleModification>())
                if (filter->getAudioModificationToRestoreStateWithID(m->getPersistentID().c_str())) m->gain = 1.0;
            sync(); return true;
        }
        std::string bytes(size, '\0'); if (!reader->readBytesFromArchive(0, size, reinterpret_cast<ARAByte*>(bytes.data()))) return false;
        // Projects saved before the SDK migration contain one opaque gain value,
        // without an SDK archive envelope or persistent ID. Import it through the
        // SDK's restore filter so the host's arrangement and saved edit both load.
        if (bytes.compare(0, 8, "gain-v1:") == 0) {
            char* end{};
            const char* start = bytes.c_str() + 8;
            const auto gain = std::strtod(start, &end);
            if (end == start || end != bytes.c_str() + bytes.size() || !std::isfinite(gain) || gain < 0 || gain > 2)
                return false;
            for (auto* source : getDocument()->getAudioSources())
                for (auto* m : source->getAudioModifications<SampleModification>())
                    if (filter->getAudioModificationToRestoreStateWithID(m->getPersistentID().c_str())) m->gain = gain;
            sync(); return true;
        }
        std::istringstream in(bytes); std::string header, line; std::getline(in, header); if (header != "ARAAAP-gain-v2") return false;
        std::vector<std::pair<SampleModification*, double>> changes;
        while (std::getline(in, line)) {
            auto tab = line.find('\t'); if (tab == std::string::npos) return false;
            char* end{}; auto gain = std::strtod(line.c_str() + tab + 1, &end);
            if (end != line.c_str() + line.size() || !std::isfinite(gain) || gain < 0 || gain > 2) return false;
            auto* m = filter->getAudioModificationToRestoreStateWithID<SampleModification>(line.substr(0, tab).c_str());
            if (m) changes.emplace_back(m, gain);
        }
        for (auto& entry : changes) entry.first->gain = entry.second;
        sync(); return true;
    }
    EditorView* doCreateEditorView() noexcept override;
private:
    void analyze(SampleSource* source, bool post) noexcept {
        auto c = companion.lock(); if (!c) return;
        std::array<float, 128> left{}, right{}; void* buffers[]{left.data(), right.data()};
        HostAudioReader reader(source);
        if (!reader.readAudioSamples(64, 128, buffers)) { std::lock_guard<std::mutex> lock(c->analysis_mutex); c->analysis_summary = "SDK audio reader failed"; return; }
        auto metrics = [](const auto& samples) { double sum = 0; float peak = 0; for (float v : samples) { sum += double(v) * v; peak = std::max(peak, std::fabs(v)); } return std::array<float, 4>{samples[0], samples[1], float(std::sqrt(sum / samples.size())), peak}; };
        auto l = metrics(left), r = metrics(right); source->rms = l[2]; c->analyzed_source = hostID(source->getHostRef());
        std::lock_guard<std::mutex> lock(c->analysis_mutex);
        if (post) { c->post_change_output_l = l; c->post_change_output_r = r; } else { c->pre_change_output_l = l; c->pre_change_output_r = r; }
        c->analysis_summary = std::string("ARA SDK ") + (post ? "post-change" : "pre-change") + " sample read";
    }
};
class SampleView : public EditorView {
public:
    using EditorView::EditorView;
protected:
    void doNotifySelection(const ViewSelection* selection) noexcept override {
        auto* dc = getDocumentController<SampleDocumentController>(); auto c = dc->companion.lock(); if (!c) return;
        c->selected_regions.clear(); c->selected_sequences.clear();
        for (auto* r : selection->getPlaybackRegions()) c->selected_regions.push_back(hostID(r->getHostRef()));
        for (auto* s : selection->getRegionSequences()) c->selected_sequences.push_back(hostID(s->getHostRef()));
        const ARAContentTimeRange* range = selection->getTimeRange(); c->selection_has_time_range = range != nullptr;
        c->selection_start = range ? range->start : 0; c->selection_duration = range ? range->duration : 0;
        c->selection_document_id = 1; ++c->selection_changes; dc->sync(false);
    }
};
EditorView* SampleDocumentController::doCreateEditorView() noexcept { return new SampleView(this); }
class SampleFactoryConfig : public FactoryConfig {
public:
    ARAAPIGeneration getLowestSupportedApiGeneration() const noexcept override { return kARAAPIGeneration_2_0_Final; }
    ARAAPIGeneration getHighestSupportedApiGeneration() const noexcept override { return kARAAPIGeneration_2_3_Final; }
    const char* getFactoryID() const noexcept override { return "org.androidaudioplugin.ara.sample.factory"; }
    const char* getPlugInName() const noexcept override { return "AAP ARA SDK Sample"; }
    const char* getManufacturerName() const noexcept override { return "AAP"; }
    const char* getInformationURL() const noexcept override { return "https://github.com/atsushieno/aap-ara"; }
    const char* getVersion() const noexcept override { return "0.1.0"; }
    const char* getDocumentArchiveID() const noexcept override { return "org.androidaudioplugin.ara.sample.archive/2"; }
};
static ARAAAPPlugInExtension sdkExtension{
    [](AndroidAudioPlugin*) -> const ARAFactory* { return PlugInEntry::getPlugInEntry<SampleFactoryConfig, SampleDocumentController>()->getFactory(); },
    [](AndroidAudioPlugin* plugin, ARADocumentControllerRef ref, ARAPlugInInstanceRoleFlags known, ARAPlugInInstanceRoleFlags assigned) {
        auto context = *static_cast<std::shared_ptr<SamplePluginContext>*>(plugin->plugin_specific);
        auto* result = context->binding->bindToARA(ref, known, assigned);
        auto* dc = context->binding->getDocumentController<SampleDocumentController>(); dc->companion = context;
        if (auto* view = context->binding->getEditorView()) view->setEditorOpen(true);
        dc->sync(false); return result;
    }
};
static void sample_prepare(AndroidAudioPlugin* plugin, int32_t, aap_buffer_t*) {
    auto* context = get_context(plugin);
    auto* info_extension = context->host.get_extension
        ? static_cast<aap_host_plugin_info_extension_t*>(context->host.get_extension(&context->host, AAP_PLUGIN_INFO_EXTENSION_URI))
        : nullptr;
    if (!info_extension || !info_extension->get) return;
    auto info = info_extension->get(info_extension, &context->host, context->plugin_id.c_str());
    if (!info.plugin_id || !info.get_port_count || !info.get_port) return;
    context->audio_output_ports = {-1, -1};
    int output = 0;
    for (int i = 0, n = info.get_port_count(&info); i < n && output < 2; ++i) {
        auto port = info.get_port(&info, i);
        if (port.content_type(&port) == AAP_CONTENT_TYPE_AUDIO && port.direction(&port) == AAP_PORT_DIRECTION_OUTPUT)
            context->audio_output_ports[output++] = i;
    }
}
static void sample_activate(AndroidAudioPlugin*) {}
static void sample_deactivate(AndroidAudioPlugin*) {}
static void sample_process(AndroidAudioPlugin* plugin, aap_buffer_t* buffer, int32_t frameCount, int64_t) {
    auto* context = get_context(plugin);
    std::array<std::array<float, 4>, 2> pre, post;
    const auto gain = context->diagnostic_gain.load();
    {
        std::lock_guard<std::mutex> lock(context->analysis_mutex);
        pre = {context->pre_change_output_l, context->pre_change_output_r};
        post = {context->post_change_output_l, context->post_change_output_r};
    }
    // Leave MIDI/AAPXS transport buffers intact. Hosts may inject these ports in
    // addition to the two audio outputs declared by the sample metadata.
    for (int output = 0; output < 2; ++output) {
        int port = context->audio_output_ports[output];
        if (port < 0 || port >= buffer->num_ports(buffer)) continue;
        auto* data = static_cast<float*>(buffer->get_buffer(buffer, port));
        if (!data) continue;
        auto bytes = buffer->get_buffer_size(buffer, port);
        memset(data, 0, bytes);
        auto values = std::min<int32_t>(std::max<int32_t>(0, frameCount),
                                      std::min<size_t>(8, bytes / sizeof(float)));
        for (int f = 0; f < values; ++f)
            data[f] = (f < 4 ? pre[output][f] : post[output][f - 4]) * gain;
    }
}

static void* sample_get_extension(AndroidAudioPlugin* plugin, const char* uri) {
    return get_context(plugin)->ara->getExtension(uri);
}

static AndroidAudioPlugin* sample_instantiate(AndroidAudioPluginFactory*, const char* pluginId, AndroidAudioPluginHost* host) {
    auto* plugin = new AndroidAudioPlugin();
    auto context = std::make_shared<SamplePluginContext>(host, pluginId);
    plugin->plugin_specific = new std::shared_ptr<SamplePluginContext>(context);
    {
        std::lock_guard<std::mutex> lock(snapshots_mutex);
        auto token = next_snapshot_token++;
        snapshots[token] = context->published;
        editable_contexts[token] = context;
        plugin_snapshot_tokens[plugin] = token;
    }
    plugin->prepare = sample_prepare;
    plugin->activate = sample_activate;
    plugin->process = sample_process;
    plugin->deactivate = sample_deactivate;
    plugin->get_extension = sample_get_extension;
    plugin->get_plugin_info = nullptr;
    context->ara = std::make_unique<aap::ara::PlugIn>(plugin, &context->host, &sdkExtension);
    return plugin;
}

static void sample_release(AndroidAudioPluginFactory*, AndroidAudioPlugin* instance) {
    auto context = *static_cast<std::shared_ptr<SamplePluginContext>*>(instance->plugin_specific);
    const std::lock_guard<std::recursive_mutex> model_lock(context->model_mutex);
    context->released = true;
    {
        std::lock_guard<std::mutex> lock(snapshots_mutex);
        auto found = plugin_snapshot_tokens.find(instance);
        if (found != plugin_snapshot_tokens.end()) {
            snapshots.erase(found->second);
            editable_contexts.erase(found->second);
            plugin_snapshot_tokens.erase(found);
        }
    }
    context->ara->prepareForRelease();
    context->ara->synchronize([&] { context->binding.reset(); });
    context->ara.reset();
    delete static_cast<std::shared_ptr<SamplePluginContext>*>(instance->plugin_specific);
    delete instance;
}

static AndroidAudioPluginFactory factory{
        sample_instantiate,
        sample_release,
        nullptr
};

extern "C" AndroidAudioPluginFactory* GetAndroidAudioPluginFactory() {
    return &factory;
}


extern "C" JNIEXPORT jlong JNICALL
Java_org_androidaudioplugin_aaparapluginsample_AraPluginSampleNative_resolveSnapshot(
        JNIEnv*, jobject, jlong servicePointer, jint instanceId) {
    auto* service = reinterpret_cast<aap::PluginService*>(servicePointer);
    auto* instance = service ? service->getLocalInstance(instanceId) : nullptr;
    if (!instance) return 0;
    std::lock_guard<std::mutex> lock(snapshots_mutex);
    auto found = plugin_snapshot_tokens.find(instance->getPlugin());
    return found == plugin_snapshot_tokens.end() ? 0 : found->second;
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparapluginsample_AraPluginSampleNative_snapshot(
        JNIEnv* env, jobject, jlong token) {
    std::shared_ptr<PublishedModel> model;
    {
        std::lock_guard<std::mutex> lock(snapshots_mutex);
        auto found = snapshots.find(token);
        if (found == snapshots.end()) return env->NewStringUTF("null");
        model = found->second;
    }
    std::lock_guard<std::mutex> lock(model->mutex);
    return env->NewStringUTF(model->json.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_org_androidaudioplugin_aaparapluginsample_AraPluginSampleNative_setModificationGain(
        JNIEnv* env, jobject, jlong token, jlong id, jdouble gain) {
    std::shared_ptr<SamplePluginContext> context;
    { const std::lock_guard<std::mutex> lock(snapshots_mutex); auto found = editable_contexts.find(token); if (found == editable_contexts.end()) return env->NewStringUTF("The host has released this instance."); context = found->second; }
    const std::lock_guard<std::recursive_mutex> lifetime(context->model_mutex);
    if (context->released || !context->ara) return env->NewStringUTF("The host has released this instance.");
    std::string error;
    context->ara->synchronize([&] {
        if (context->released) { error = "The host has released this instance."; return; }
        if (context->model_update_active || !std::isfinite(gain) || gain < 0 || gain > 2) { error = "Invalid modification edit"; return; }
        auto* dc = context->binding->getDocumentController<SampleDocumentController>();
        if (!dc || !context->reverse_updates_supported || !dc->setGain(id, gain)) error = "Modification unavailable";
    });
    return env->NewStringUTF(error.c_str());
}
