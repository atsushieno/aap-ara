#include <jni.h>
#include <aap/android-audio-plugin.h>
#include <aap/core/host/plugin-host.h>
#include <memory>
#include <aap/ext/ara.h>
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
#include <stdexcept>
#include <string>

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

extern "C" {

struct NamedEntity {
    std::string name;
    std::string persistent_id;
};

struct MusicalContextEntity : public NamedEntity {
    bool has_color{};
    aap_ara_color_t color{};
};

struct RegionSequenceEntity : public MusicalContextEntity {
    int32_t order_index{};
    aap_ara_musical_context_id_t musical_context_id{};
};

struct AudioSourceEntity : public NamedEntity {
    double rms = 0.0;
    aap_ara_sample_count_t sample_count{};
    aap_ara_sample_rate_t sample_rate{};
    int32_t channel_count{};
    bool merits_64_bit_samples{};
    std::string channel_layout_tag;
    bool samples_access_enabled{};
    aap_ara_sample_position_t changed_start_sample{};
    aap_ara_sample_count_t changed_sample_count{};
};

struct AudioModificationEntity : public NamedEntity {
    double gain = 1.0;
    aap_ara_audio_source_id_t audio_source_id{};
};

struct PlaybackRegionEntity {
    uint32_t transformation_flags{};
    aap_ara_time_position_t start_in_modification_time{};
    aap_ara_time_duration_t duration_in_modification_time{};
    aap_ara_time_position_t start_in_playback_time{};
    aap_ara_time_duration_t duration_in_playback_time{};
    aap_ara_region_sequence_id_t region_sequence_id{};
    std::string name;
    bool has_color{};
    aap_ara_color_t color{};
    aap_ara_audio_modification_id_t audio_modification_id{};
};

struct SamplePluginContext {
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
    std::map<aap_ara_document_id_t, NamedEntity> documents;
    std::map<aap_ara_musical_context_id_t, MusicalContextEntity> musical_contexts;
    std::map<aap_ara_region_sequence_id_t, RegionSequenceEntity> region_sequences;
    std::map<aap_ara_audio_source_id_t, AudioSourceEntity> audio_sources;
    std::map<aap_ara_audio_modification_id_t, AudioModificationEntity> audio_modifications;
    std::map<aap_ara_playback_region_id_t, PlaybackRegionEntity> playback_regions;

    explicit SamplePluginContext(AndroidAudioPluginHost* host_, const char* id) : host(*host_), plugin_id(id ? id : "") {}
};

static SamplePluginContext* get_context(AndroidAudioPlugin* plugin) {
    return static_cast<std::shared_ptr<SamplePluginContext>*>(plugin->plugin_specific)->get();
}

// Publish only completed transactions. The UI reads this immutable copy and
// never touches the live ARA maps or holds a lock across reverse host callbacks.
static void publish_model(SamplePluginContext* context) {
    auto& snapshot = *context->published;
    std::lock_guard<std::mutex> lock(snapshot.mutex);
    std::ostringstream out;
    out.precision(17);
    out << "{\"canEdit\":" << (context->reverse_updates_supported ? "true" : "false") << ",\"revision\":" << ++snapshot.revision << ",\"contentChanges\":" << context->content_changes;
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

static std::string copy_nullable_string(const char* value) {
    return value ? value : "";
}

static aap_ara_host_extension_t* ensure_host_extension(SamplePluginContext* context) {
    return context && context->host.get_extension
        ? static_cast<aap_ara_host_extension_t*>(context->host.get_extension(&context->host, AAP_ARA_EXTENSION_URI))
        : nullptr;
}

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

static void analyze_audio_range(
        SamplePluginContext* context,
        aap_ara_audio_source_id_t audio_source_id,
        aap_ara_sample_position_t start_sample,
        aap_ara_sample_count_t sample_count,
        std::array<float, 4>& out_left,
        std::array<float, 4>& out_right,
        const char* label) {
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "analyze_audio_range begin label=%s audioSourceId=%d start=%d count=%d",
                 label ? label : "(null)", audio_source_id, start_sample, sample_count);
    auto* host_ext = ensure_host_extension(context);
    if (!host_ext)
        throw std::runtime_error("ARA host extension was not available");

    aap_ara_host_capability_t capability{};
    capability.struct_size = sizeof(capability);
    host_ext->get_host_capability(host_ext, &context->host, &capability);
    context->reverse_updates_supported = capability.struct_size >= sizeof(capability) &&
        (capability.supported_model_updates & AAP_ARA_HOST_SUPPORTS_CONTENT_UPDATES);
    context->host_supports_float32 = (capability.supported_sample_formats & AAP_ARA_SAMPLE_FORMAT_FLOAT32) != 0;
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "host capability api=%u roles=%u formats=%u updates=%u size=%u supportsFloat32=%d",
                 capability.api_generations,
                 capability.role_flags,
                 capability.supported_sample_formats,
                 capability.supported_model_updates, capability.struct_size,
                 context->host_supports_float32 ? 1 : 0);
    if (!context->host_supports_float32)
        throw std::runtime_error("Host does not advertise float32 sample reads");

    aap_ara_audio_source_sample_range_t range{
        sizeof(aap_ara_audio_source_sample_range_t),
        start_sample,
        sample_count,
        2,
        AAP_ARA_SAMPLE_FORMAT_FLOAT32
    };
    aap_ara_audio_source_samples_buffer_t destination{
        sizeof(aap_ara_audio_source_samples_buffer_t),
        nullptr,
        0,
        0,
        0,
        0
    };
    host_ext->read_audio_source_samples(host_ext, &context->host, audio_source_id, &range, &destination);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "read_audio_source_samples returned format=%u channels=%d samples=%d size=%zu",
                 destination.sample_format, destination.channel_count, destination.sample_count, destination.data_size);

    if (destination.sample_format != AAP_ARA_SAMPLE_FORMAT_FLOAT32)
        throw std::runtime_error("Host returned unexpected sample format");
    if (destination.channel_count < 2)
        throw std::runtime_error("Host returned fewer than 2 channels");
    if (!destination.data || destination.sample_count < 2 ||
        destination.data_size < static_cast<size_t>(destination.sample_count) * 2 * sizeof(float))
        throw std::runtime_error("Host returned an incomplete stereo sample buffer");

    auto* base = static_cast<const float*>(destination.data);
    auto frames = destination.sample_count;
    auto* left = base;
    auto* right = base + frames;

    out_left = {left[0], left[1], compute_rms(left, frames), compute_peak(left, frames)};
    out_right = {right[0], right[1], compute_rms(right, frames), compute_peak(right, frames)};
    context->audio_sources[audio_source_id].rms = out_left[2];
    context->analyzed_source = audio_source_id;
    context->diagnostic_gain.store(1.0);
    for (const auto& entry : context->audio_modifications)
        if (entry.second.audio_source_id == audio_source_id) { context->diagnostic_gain.store(entry.second.gain); break; }

    std::ostringstream ss;
    ss << label << " range[" << start_sample << ", " << (start_sample + sample_count)
       << ") left=(" << out_left[0] << ", " << out_left[1] << ", rms=" << out_left[2]
       << ", peak=" << out_left[3] << ") right=(" << out_right[0] << ", " << out_right[1]
       << ", rms=" << out_right[2] << ", peak=" << out_right[3] << ")";
    auto summary = ss.str();
    {
        std::lock_guard<std::mutex> lock(context->analysis_mutex);
        context->analysis_summary = summary;
    }
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG, "analyze_audio_range end summary=%s", summary.c_str());
}

static void run_analysis(
        SamplePluginContext* context,
        aap_ara_audio_source_id_t audioSourceId,
        bool postChange) {
    try {
        std::array<float, 4> left{};
        std::array<float, 4> right{};
        if (postChange) {
            analyze_audio_range(
                    context, audioSourceId, 64, 128, left, right, "post-change");
            std::lock_guard<std::mutex> analysisLock(context->analysis_mutex);
            context->post_change_output_l = left;
            context->post_change_output_r = right;
        } else {
            analyze_audio_range(
                    context, audioSourceId, 64, 128, left, right, "pre-change");
            std::lock_guard<std::mutex> analysisLock(context->analysis_mutex);
            context->pre_change_output_l = left;
            context->pre_change_output_r = right;
        }
    } catch (const std::exception& ex) {
        std::lock_guard<std::mutex> analysisLock(context->analysis_mutex);
        context->analysis_summary = ex.what();
        aap::a_log_f(AAP_LOG_LEVEL_ERROR, LOG_TAG, "run_analysis failed: %s", ex.what());
    }
}

static void fill_named_entity(NamedEntity& entity, const char* name, const char* persistentId) {
    entity.name = copy_nullable_string(name);
    entity.persistent_id = copy_nullable_string(persistentId);
}

static void sample_get_factory_capability(aap_ara_extension_t*, AndroidAudioPlugin*, aap_ara_factory_capability_t* destination) {
    if (!destination)
        return;
    if (destination->struct_size >= sizeof(*destination))
        destination->supported_features = AAP_ARA_FEATURE_MODIFICATION_ARCHIVE;
    destination->struct_size = sizeof(aap_ara_factory_capability_t);
    destination->api_generations = AAP_ARA_API_GENERATION_2;
    destination->role_flags = AAP_ARA_ROLE_PLAYBACK_RENDERER | AAP_ARA_ROLE_EDITOR_RENDERER | AAP_ARA_ROLE_EDITOR_VIEW;
    destination->supported_playback_transformation_flags = AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_NONE;
}

static void sample_begin_model_update(aap_ara_extension_t*, AndroidAudioPlugin* plugin, uint32_t flags) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto* context = get_context(plugin);
    context->model_update_active = true;
    context->last_model_update_flags = flags;
}

static void sample_end_model_update(aap_ara_extension_t*, AndroidAudioPlugin* plugin) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto* context = get_context(plugin);
    context->model_update_active = false;
    publish_model(context);
}

static void sample_create_document(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& document = get_context(plugin)->documents[documentId];
    fill_named_entity(document, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
}

static void sample_update_document_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& document = get_context(plugin)->documents[documentId];
    fill_named_entity(document, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
}

static void sample_destroy_document(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    get_context(plugin)->documents.erase(documentId);
}

static void sample_create_musical_context(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& context = get_context(plugin)->musical_contexts[musicalContextId];
    fill_named_entity(context, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    context.has_color = properties && properties->color;
    if (context.has_color)
        context.color = *properties->color;
}

static void sample_update_musical_context_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& context = get_context(plugin)->musical_contexts[musicalContextId];
    fill_named_entity(context, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    context.has_color = properties && properties->color;
    if (context.has_color)
        context.color = *properties->color;
}

static void sample_destroy_musical_context(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musicalContextId) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    get_context(plugin)->musical_contexts.erase(musicalContextId);
}

static void sample_create_region_sequence(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& sequence = get_context(plugin)->region_sequences[regionSequenceId];
    fill_named_entity(sequence, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    sequence.order_index = properties ? properties->order_index : 0;
    sequence.musical_context_id = properties ? properties->musical_context_id : 0;
    sequence.has_color = properties && properties->color;
    if (sequence.has_color)
        sequence.color = *properties->color;
}

static void sample_update_region_sequence_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& sequence = get_context(plugin)->region_sequences[regionSequenceId];
    fill_named_entity(sequence, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    sequence.order_index = properties ? properties->order_index : 0;
    sequence.musical_context_id = properties ? properties->musical_context_id : 0;
    sequence.has_color = properties && properties->color;
    if (sequence.has_color)
        sequence.color = *properties->color;
}

static void sample_destroy_region_sequence(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t regionSequenceId) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    get_context(plugin)->region_sequences.erase(regionSequenceId);
}

static void sample_create_audio_source(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& source = get_context(plugin)->audio_sources[audioSourceId];
    fill_named_entity(source, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    source.sample_count = properties ? properties->sample_count : 0;
    source.sample_rate = properties ? properties->sample_rate : 0.0;
    source.channel_count = properties ? properties->channel_count : 0;
    source.merits_64_bit_samples = properties && properties->merits_64_bit_samples;
    source.channel_layout_tag = properties ? copy_nullable_string(properties->channel_layout_tag) : "";
}

static void sample_update_audio_source_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& source = get_context(plugin)->audio_sources[audioSourceId];
    fill_named_entity(source, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    source.sample_count = properties ? properties->sample_count : 0;
    source.sample_rate = properties ? properties->sample_rate : 0.0;
    source.channel_count = properties ? properties->channel_count : 0;
    source.merits_64_bit_samples = properties && properties->merits_64_bit_samples;
    source.channel_layout_tag = properties ? copy_nullable_string(properties->channel_layout_tag) : "";
}

static void sample_enable_audio_source_samples_access(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, bool enable) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto* context = get_context(plugin);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "enable_audio_source_samples_access audioSourceId=%d enable=%d",
                 audioSourceId, enable ? 1 : 0);
    context->audio_sources[audioSourceId].samples_access_enabled = enable;
    context->sample_access_enabled = enable;
    if (enable)
        run_analysis(context, audioSourceId, false);
    if (!context->model_update_active) publish_model(context);
}

static void sample_notify_audio_source_content_changed(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, aap_ara_sample_position_t startSample, aap_ara_sample_count_t sampleCount) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto* context = get_context(plugin);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "notify_audio_source_content_changed audioSourceId=%d start=%d count=%d sampleAccess=%d",
                 audioSourceId, startSample, sampleCount, context->sample_access_enabled ? 1 : 0);
    auto& source = context->audio_sources[audioSourceId];
    ++context->content_changes;
    source.changed_start_sample = startSample;
    source.changed_sample_count = sampleCount;
    if (source.samples_access_enabled)
        run_analysis(context, audioSourceId, true);
    if (!context->model_update_active) publish_model(context);
}

static void sample_destroy_audio_source(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    get_context(plugin)->audio_sources.erase(audioSourceId);
}

static void sample_create_audio_modification(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& modification = get_context(plugin)->audio_modifications[audioModificationId];
    fill_named_entity(modification, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    modification.audio_source_id = audioSourceId;
}

static void sample_update_audio_modification_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& modification = get_context(plugin)->audio_modifications[audioModificationId];
    fill_named_entity(modification, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
}

static void sample_destroy_audio_modification(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    get_context(plugin)->audio_modifications.erase(audioModificationId);
}

static void sample_create_playback_region(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId, aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& playback = get_context(plugin)->playback_regions[playbackRegionId];
    playback.audio_modification_id = audioModificationId;
    playback.transformation_flags = properties ? properties->transformation_flags : 0;
    playback.start_in_modification_time = properties ? properties->start_in_modification_time : 0.0;
    playback.duration_in_modification_time = properties ? properties->duration_in_modification_time : 0.0;
    playback.start_in_playback_time = properties ? properties->start_in_playback_time : 0.0;
    playback.duration_in_playback_time = properties ? properties->duration_in_playback_time : 0.0;
    playback.region_sequence_id = properties ? properties->region_sequence_id : 0;
    playback.name = properties ? copy_nullable_string(properties->name) : "";
    playback.has_color = properties && properties->color;
    if (playback.has_color)
        playback.color = *properties->color;
}

static void sample_update_playback_region_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    auto& playback = get_context(plugin)->playback_regions[playbackRegionId];
    playback.transformation_flags = properties ? properties->transformation_flags : 0;
    playback.start_in_modification_time = properties ? properties->start_in_modification_time : 0.0;
    playback.duration_in_modification_time = properties ? properties->duration_in_modification_time : 0.0;
    playback.start_in_playback_time = properties ? properties->start_in_playback_time : 0.0;
    playback.duration_in_playback_time = properties ? properties->duration_in_playback_time : 0.0;
    playback.region_sequence_id = properties ? properties->region_sequence_id : 0;
    playback.name = properties ? copy_nullable_string(properties->name) : "";
    playback.has_color = properties && properties->color;
    if (playback.has_color)
        playback.color = *properties->color;
}

static void sample_destroy_playback_region(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_playback_region_id_t playbackRegionId) {
    const std::lock_guard<std::recursive_mutex> model_lock(get_context(plugin)->model_mutex);
    get_context(plugin)->playback_regions.erase(playbackRegionId);
}

static bool sample_store_modification_state(aap_ara_extension_t*, AndroidAudioPlugin* plugin, int64_t id, aap_ara_archive_buffer_t* destination) {
    auto* context = get_context(plugin);
    const std::lock_guard<std::recursive_mutex> lock(context->model_mutex);
    auto found = context->audio_modifications.find(id);
    if (found == context->audio_modifications.end() || !destination) return false;
    std::ostringstream out;
    out.precision(17); out << "gain-v1:" << found->second.gain;
    auto state = out.str();
    destination->data_size = state.size();
    if (!destination->data || destination->capacity < state.size()) return false;
    memcpy(destination->data, state.data(), state.size());
    return true;
}

static bool sample_restore_modification_state(aap_ara_extension_t*, AndroidAudioPlugin* plugin, int64_t id, const void* data, size_t size) {
    auto* context = get_context(plugin);
    const std::lock_guard<std::recursive_mutex> lock(context->model_mutex);
    auto found = context->audio_modifications.find(id);
    if (found == context->audio_modifications.end() || size > AAP_ARA_MAX_ARCHIVE_BYTES || (!data && size)) return false;
    double gain = 1.0;
    if (size) {
        std::string state(static_cast<const char*>(data), size);
        if (state.compare(0, 8, "gain-v1:") != 0) return false;
        char* end{};
        gain = std::strtod(state.c_str() + 8, &end);
        if (end == state.c_str() + 8 || end != state.c_str() + size || !std::isfinite(gain) || gain < 0 || gain > 2) return false;
    }
    found->second.gain = gain;
    if (context->analyzed_source == found->second.audio_source_id) context->diagnostic_gain.store(gain);
    // Host restore (load/undo/redo) must never echo a reverse notification.
    if (!context->model_update_active) publish_model(context);
    return true;
}

static aap_ara_extension_t sample_ara_extension{
        nullptr,
        sample_get_factory_capability,
        sample_begin_model_update,
        sample_end_model_update,
        sample_create_document,
        sample_update_document_properties,
        sample_destroy_document,
        sample_create_musical_context,
        sample_update_musical_context_properties,
        sample_destroy_musical_context,
        sample_create_region_sequence,
        sample_update_region_sequence_properties,
        sample_destroy_region_sequence,
        sample_create_audio_source,
        sample_update_audio_source_properties,
        sample_enable_audio_source_samples_access,
        sample_notify_audio_source_content_changed,
        sample_destroy_audio_source,
        sample_create_audio_modification,
        sample_update_audio_modification_properties,
        sample_destroy_audio_modification,
        sample_create_playback_region,
        sample_update_playback_region_properties,
        sample_destroy_playback_region,
        sample_store_modification_state,
        sample_restore_modification_state
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

static void* sample_get_extension(AndroidAudioPlugin*, const char* uri) {
    if (uri && strcmp(uri, AAP_ARA_EXTENSION_URI) == 0)
        return &sample_ara_extension;
    return nullptr;
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
    delete static_cast<std::shared_ptr<SamplePluginContext>*>(instance->plugin_specific);
    delete instance;
}

static AndroidAudioPluginFactory factory{
        sample_instantiate,
        sample_release,
        nullptr
};

AndroidAudioPluginFactory* GetAndroidAudioPluginFactory() {
    return &factory;
}

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
    {
        const std::lock_guard<std::mutex> lock(snapshots_mutex);
        auto found = editable_contexts.find(token);
        if (found == editable_contexts.end()) return env->NewStringUTF("The host has released this instance.");
        context = found->second;
    }
    const std::lock_guard<std::recursive_mutex> lock(context->model_mutex);
    if (context->released) return env->NewStringUTF("The host has released this instance.");
    if (context->model_update_active) return env->NewStringUTF("The host is updating the document. Try again.");
    if (!context->reverse_updates_supported) return env->NewStringUTF("This host does not support plugin content updates.");
    auto found = context->audio_modifications.find(id);
    if (found == context->audio_modifications.end() || !std::isfinite(gain) || gain < 0 || gain > 2)
        return env->NewStringUTF("Invalid modification");
    if (found->second.gain == gain) return env->NewStringUTF("");
    auto* host = ensure_host_extension(context.get());
    if (!host || !host->notify_content_changed) return env->NewStringUTF("Content callback unavailable");
    found->second.gain = gain;
    if (context->analyzed_source == found->second.audio_source_id) context->diagnostic_gain.store(gain);
    ++context->content_changes;
    publish_model(context.get());
    aap_ara_content_update_t update{sizeof(update), AAP_ARA_CONTENT_AUDIO_MODIFICATION, id, 0, false, 0, 0};
    // The receiving host enqueues this notification. It cannot call back into
    // this model until we return and release the lock.
    host->notify_content_changed(host, &context->host, &update);
    return env->NewStringUTF("");
}
