#include <aap/android-audio-plugin.h>
#include <aap/ext/ara.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "aap/unstable/logging.h"

#define LOG_TAG "AAP.ARA.PluginSample"

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
    AndroidAudioPluginHost host;
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
    std::map<aap_ara_document_id_t, NamedEntity> documents;
    std::map<aap_ara_musical_context_id_t, MusicalContextEntity> musical_contexts;
    std::map<aap_ara_region_sequence_id_t, RegionSequenceEntity> region_sequences;
    std::map<aap_ara_audio_source_id_t, AudioSourceEntity> audio_sources;
    std::map<aap_ara_audio_modification_id_t, AudioModificationEntity> audio_modifications;
    std::map<aap_ara_playback_region_id_t, PlaybackRegionEntity> playback_regions;

    explicit SamplePluginContext(AndroidAudioPluginHost* host_) : host(*host_) {}
};

static SamplePluginContext* get_context(AndroidAudioPlugin* plugin) {
    return static_cast<SamplePluginContext*>(plugin->plugin_specific);
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
    context->host_supports_float32 = (capability.supported_sample_formats & AAP_ARA_SAMPLE_FORMAT_FLOAT32) != 0;
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "host capability api=%u roles=%u formats=%u supportsFloat32=%d",
                 capability.api_generations,
                 capability.role_flags,
                 capability.supported_sample_formats,
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
    if (destination.sample_count <= 0)
        throw std::runtime_error("Host returned no audio samples");

    auto* base = static_cast<const float*>(destination.data);
    auto frames = destination.sample_count;
    auto* left = base;
    auto* right = base + frames;

    out_left = {left[0], left[1], compute_rms(left, frames), compute_peak(left, frames)};
    out_right = {right[0], right[1], compute_rms(right, frames), compute_peak(right, frames)};

    std::ostringstream ss;
    ss << label << " range[" << start_sample << ", " << (start_sample + sample_count)
       << ") left=(" << out_left[0] << ", " << out_left[1] << ", rms=" << out_left[2]
       << ", peak=" << out_left[3] << ") right=(" << out_right[0] << ", " << out_right[1]
       << ", rms=" << out_right[2] << ", peak=" << out_right[3] << ")";
    context->analysis_summary = ss.str();
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG, "analyze_audio_range end summary=%s", context->analysis_summary.c_str());
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
    destination->struct_size = sizeof(aap_ara_factory_capability_t);
    destination->api_generations = AAP_ARA_API_GENERATION_2;
    destination->role_flags = AAP_ARA_ROLE_PLAYBACK_RENDERER | AAP_ARA_ROLE_EDITOR_RENDERER;
    destination->supported_playback_transformation_flags = AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_NONE;
}

static void sample_begin_model_update(aap_ara_extension_t*, AndroidAudioPlugin* plugin, uint32_t flags) {
    auto* context = get_context(plugin);
    context->model_update_active = true;
    context->last_model_update_flags = flags;
}

static void sample_end_model_update(aap_ara_extension_t*, AndroidAudioPlugin* plugin) {
    auto* context = get_context(plugin);
    context->model_update_active = false;
}

static void sample_create_document(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
    auto& document = get_context(plugin)->documents[documentId];
    fill_named_entity(document, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
}

static void sample_update_document_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
    auto& document = get_context(plugin)->documents[documentId];
    fill_named_entity(document, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
}

static void sample_destroy_document(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId) {
    get_context(plugin)->documents.erase(documentId);
}

static void sample_create_musical_context(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
    auto& context = get_context(plugin)->musical_contexts[musicalContextId];
    fill_named_entity(context, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    context.has_color = properties && properties->color;
    if (context.has_color)
        context.color = *properties->color;
}

static void sample_update_musical_context_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
    auto& context = get_context(plugin)->musical_contexts[musicalContextId];
    fill_named_entity(context, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    context.has_color = properties && properties->color;
    if (context.has_color)
        context.color = *properties->color;
}

static void sample_destroy_musical_context(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musicalContextId) {
    get_context(plugin)->musical_contexts.erase(musicalContextId);
}

static void sample_create_region_sequence(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
    auto& sequence = get_context(plugin)->region_sequences[regionSequenceId];
    fill_named_entity(sequence, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    sequence.order_index = properties ? properties->order_index : 0;
    sequence.musical_context_id = properties ? properties->musical_context_id : 0;
    sequence.has_color = properties && properties->color;
    if (sequence.has_color)
        sequence.color = *properties->color;
}

static void sample_update_region_sequence_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
    auto& sequence = get_context(plugin)->region_sequences[regionSequenceId];
    fill_named_entity(sequence, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    sequence.order_index = properties ? properties->order_index : 0;
    sequence.musical_context_id = properties ? properties->musical_context_id : 0;
    sequence.has_color = properties && properties->color;
    if (sequence.has_color)
        sequence.color = *properties->color;
}

static void sample_destroy_region_sequence(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t regionSequenceId) {
    get_context(plugin)->region_sequences.erase(regionSequenceId);
}

static void sample_create_audio_source(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_document_id_t, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
    auto& source = get_context(plugin)->audio_sources[audioSourceId];
    fill_named_entity(source, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    source.sample_count = properties ? properties->sample_count : 0;
    source.sample_rate = properties ? properties->sample_rate : 0.0;
    source.channel_count = properties ? properties->channel_count : 0;
    source.merits_64_bit_samples = properties && properties->merits_64_bit_samples;
    source.channel_layout_tag = properties ? copy_nullable_string(properties->channel_layout_tag) : "";
}

static void sample_update_audio_source_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
    auto& source = get_context(plugin)->audio_sources[audioSourceId];
    fill_named_entity(source, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    source.sample_count = properties ? properties->sample_count : 0;
    source.sample_rate = properties ? properties->sample_rate : 0.0;
    source.channel_count = properties ? properties->channel_count : 0;
    source.merits_64_bit_samples = properties && properties->merits_64_bit_samples;
    source.channel_layout_tag = properties ? copy_nullable_string(properties->channel_layout_tag) : "";
}

static void sample_enable_audio_source_samples_access(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, bool enable) {
    auto* context = get_context(plugin);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "enable_audio_source_samples_access audioSourceId=%d enable=%d",
                 audioSourceId, enable ? 1 : 0);
    context->audio_sources[audioSourceId].samples_access_enabled = enable;
    context->sample_access_enabled = enable;
    if (enable)
        run_analysis(context, audioSourceId, false);
}

static void sample_notify_audio_source_content_changed(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, aap_ara_sample_position_t startSample, aap_ara_sample_count_t sampleCount) {
    auto* context = get_context(plugin);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "notify_audio_source_content_changed audioSourceId=%d start=%d count=%d sampleAccess=%d",
                 audioSourceId, startSample, sampleCount, context->sample_access_enabled ? 1 : 0);
    auto& source = context->audio_sources[audioSourceId];
    source.changed_start_sample = startSample;
    source.changed_sample_count = sampleCount;
    if (context->sample_access_enabled)
        run_analysis(context, audioSourceId, true);
}

static void sample_destroy_audio_source(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId) {
    get_context(plugin)->audio_sources.erase(audioSourceId);
}

static void sample_create_audio_modification(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
    auto& modification = get_context(plugin)->audio_modifications[audioModificationId];
    fill_named_entity(modification, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
    modification.audio_source_id = audioSourceId;
}

static void sample_update_audio_modification_properties(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
    auto& modification = get_context(plugin)->audio_modifications[audioModificationId];
    fill_named_entity(modification, properties ? properties->name : nullptr, properties ? properties->persistent_id : nullptr);
}

static void sample_destroy_audio_modification(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId) {
    get_context(plugin)->audio_modifications.erase(audioModificationId);
}

static void sample_create_playback_region(aap_ara_extension_t*, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId, aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties) {
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
    get_context(plugin)->playback_regions.erase(playbackRegionId);
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
        sample_destroy_playback_region
};

static void sample_prepare(AndroidAudioPlugin*, int32_t, aap_buffer_t*) {}
static void sample_activate(AndroidAudioPlugin*) {}
static void sample_deactivate(AndroidAudioPlugin*) {}
static void sample_process(AndroidAudioPlugin* plugin, aap_buffer_t* buffer, int32_t frameCount, int64_t) {
    auto* context = get_context(plugin);
    std::lock_guard<std::mutex> lock(context->analysis_mutex);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "process frameCount=%d preL0=%f postL0=%f preR0=%f postR0=%f",
                 frameCount,
                 context->pre_change_output_l[0], context->post_change_output_l[0],
                 context->pre_change_output_r[0], context->post_change_output_r[0]);
    for (int32_t i = 0, n = buffer->num_ports(buffer); i < n; ++i) {
        auto* data = static_cast<float*>(buffer->get_buffer(buffer, i));
        if (!data)
            continue;
        auto size = buffer->get_buffer_size(buffer, i);
        memset(data, 0, size);
    }

    int output_index = 0;
    for (int32_t i = 0, n = buffer->num_ports(buffer); i < n; ++i) {
        auto* data = static_cast<float*>(buffer->get_buffer(buffer, i));
        if (!data)
            continue;
        if (output_index == 0) {
            auto values = std::min<int32_t>(frameCount, 8);
            for (int32_t f = 0; f < values && f < 4; ++f)
                data[f] = context->pre_change_output_l[f];
            for (int32_t f = 4; f < values && f < 8; ++f)
                data[f] = context->post_change_output_l[f - 4];
        } else if (output_index == 1) {
            auto values = std::min<int32_t>(frameCount, 8);
            for (int32_t f = 0; f < values && f < 4; ++f)
                data[f] = context->pre_change_output_r[f];
            for (int32_t f = 4; f < values && f < 8; ++f)
                data[f] = context->post_change_output_r[f - 4];
        } else
            continue;
        ++output_index;
    }
}

static void* sample_get_extension(AndroidAudioPlugin*, const char* uri) {
    if (uri && strcmp(uri, AAP_ARA_EXTENSION_URI) == 0)
        return &sample_ara_extension;
    return nullptr;
}

static AndroidAudioPlugin* sample_instantiate(AndroidAudioPluginFactory*, const char*, AndroidAudioPluginHost* host) {
    auto* plugin = new AndroidAudioPlugin();
    plugin->plugin_specific = new SamplePluginContext(host);
    plugin->prepare = sample_prepare;
    plugin->activate = sample_activate;
    plugin->process = sample_process;
    plugin->deactivate = sample_deactivate;
    plugin->get_extension = sample_get_extension;
    plugin->get_plugin_info = nullptr;
    return plugin;
}

static void sample_release(AndroidAudioPluginFactory*, AndroidAudioPlugin* instance) {
    delete static_cast<SamplePluginContext*>(instance->plugin_specific);
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
