#ifndef AAP_ARA_H_INCLUDED
#define AAP_ARA_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include "aap/android-audio-plugin.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AAP_ARA_EXTENSION_URI "urn://androidaudioplugin.org/extensions/ara/v1"
#define AAP_ARA_MAX_NAME_CHARS 256
#define AAP_ARA_MAX_PERSISTENT_ID_CHARS 256
#define AAP_ARA_MAX_CHANNEL_LAYOUT_TAG_CHARS 128

/*
 * AAP-native ARA extension entry point.
 *
 * This extension family is intended to become the AAP equivalent of the ARA
 * integrations that exist in other plug-in ecosystems. The public contract is
 * defined in AAP terms between an AAP host and an AAP plug-in.
 *
 * This header intentionally starts with the document-model API surface first:
 * object identities, properties, model-edit lifecycle, and audio-source sample
 * access. More advanced content exchange and editor integration can be layered
 * on top after the core graph is stable.
 */

enum {
    AAP_ARA_API_GENERATION_2 = 1u << 0,
    AAP_ARA_API_GENERATION_3_DRAFT = 1u << 1
};

enum {
    AAP_ARA_ROLE_PLAYBACK_RENDERER = 1u << 0,
    AAP_ARA_ROLE_EDITOR_RENDERER = 1u << 1,
    AAP_ARA_ROLE_EDITOR_VIEW = 1u << 2
};

enum {
    AAP_ARA_MODEL_UPDATE_FLAG_NONE = 0,
    AAP_ARA_MODEL_UPDATE_FLAG_RESTORE = 1u << 0,
    AAP_ARA_MODEL_UPDATE_FLAG_PLAYBACK_STATE = 1u << 1,
    AAP_ARA_MODEL_UPDATE_FLAG_CONTENT_REFRESH = 1u << 2
};

enum {
    AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_NONE = 0,
    AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_TIMESTRETCH = 1u << 0,
    AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_TIMESTRETCH_REFLECTING_TEMPO = 1u << 1,
    AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_CONTENT_BASED_FADE_AT_HEAD = 1u << 2,
    AAP_ARA_PLAYBACK_TRANSFORMATION_FLAG_CONTENT_BASED_FADE_AT_TAIL = 1u << 3
};

enum {
    AAP_ARA_SAMPLE_FORMAT_FLOAT32 = 1u << 0,
    AAP_ARA_SAMPLE_FORMAT_FLOAT64 = 1u << 1
};

typedef int64_t aap_ara_document_id_t;
typedef int64_t aap_ara_musical_context_id_t;
typedef int64_t aap_ara_region_sequence_id_t;
typedef int64_t aap_ara_audio_source_id_t;
typedef int64_t aap_ara_audio_modification_id_t;
typedef int64_t aap_ara_playback_region_id_t;

typedef double aap_ara_time_position_t;
typedef double aap_ara_time_duration_t;
typedef double aap_ara_quarter_position_t;
typedef double aap_ara_quarter_duration_t;
typedef int64_t aap_ara_sample_position_t;
typedef int64_t aap_ara_sample_count_t;
typedef double aap_ara_sample_rate_t;

typedef struct aap_ara_color_t {
    float r;
    float g;
    float b;
} aap_ara_color_t;

typedef struct aap_ara_factory_capability_t {
    uint32_t struct_size;
    uint32_t api_generations;
    uint32_t role_flags;
    uint32_t supported_playback_transformation_flags;
} aap_ara_factory_capability_t;

typedef struct aap_ara_host_capability_t {
    uint32_t struct_size;
    uint32_t api_generations;
    uint32_t role_flags;
    uint32_t supported_sample_formats;
} aap_ara_host_capability_t;

typedef struct aap_ara_document_properties_t {
    uint32_t struct_size;
    const char* name;
    const char* persistent_id;
} aap_ara_document_properties_t;

typedef struct aap_ara_musical_context_properties_t {
    uint32_t struct_size;
    const char* name;
    const aap_ara_color_t* color;
    const char* persistent_id;
} aap_ara_musical_context_properties_t;

typedef struct aap_ara_region_sequence_properties_t {
    uint32_t struct_size;
    const char* name;
    int32_t order_index;
    aap_ara_musical_context_id_t musical_context_id;
    const aap_ara_color_t* color;
    const char* persistent_id;
} aap_ara_region_sequence_properties_t;

typedef struct aap_ara_audio_source_properties_t {
    uint32_t struct_size;
    const char* name;
    const char* persistent_id;
    aap_ara_sample_count_t sample_count;
    aap_ara_sample_rate_t sample_rate;
    int32_t channel_count;
    bool merits_64_bit_samples;
    const char* channel_layout_tag;
} aap_ara_audio_source_properties_t;

typedef struct aap_ara_audio_modification_properties_t {
    uint32_t struct_size;
    const char* name;
    const char* persistent_id;
} aap_ara_audio_modification_properties_t;

typedef struct aap_ara_playback_region_properties_t {
    uint32_t struct_size;
    uint32_t transformation_flags;
    aap_ara_time_position_t start_in_modification_time;
    aap_ara_time_duration_t duration_in_modification_time;
    aap_ara_time_position_t start_in_playback_time;
    aap_ara_time_duration_t duration_in_playback_time;
    aap_ara_region_sequence_id_t region_sequence_id;
    const char* name;
    const aap_ara_color_t* color;
} aap_ara_playback_region_properties_t;

typedef struct aap_ara_audio_source_sample_range_t {
    uint32_t struct_size;
    aap_ara_sample_position_t start_sample;
    aap_ara_sample_count_t sample_count;
    int32_t channel_count;
    uint32_t sample_format;
} aap_ara_audio_source_sample_range_t;

/*
 * Audio sample data is stored as tightly packed contiguous planes:
 *   [channel 0 samples][channel 1 samples]...
 * Each plane contains sample_count frames in the selected sample format.
 */
typedef struct aap_ara_audio_source_samples_buffer_t {
    uint32_t struct_size;
    void* data;
    size_t data_size;
    aap_ara_sample_count_t sample_count;
    int32_t channel_count;
    uint32_t sample_format;
} aap_ara_audio_source_samples_buffer_t;

/*
 * The plug-in extension is driven by the host. The host uses it to construct
 * and mutate the ARA model graph owned by the plug-in.
 */
typedef struct aap_ara_extension_t {
    /* Reserved for future AAPXS-backed transport internals. */
    void* aapxs_context;

    RT_UNSAFE void (*get_factory_capability) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_factory_capability_t* destination);

    RT_UNSAFE void (*begin_model_update) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            uint32_t flags);

    RT_UNSAFE void (*end_model_update) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin);

    RT_UNSAFE void (*create_document) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_document_id_t document_id,
            const aap_ara_document_properties_t* properties);

    RT_UNSAFE void (*update_document_properties) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_document_id_t document_id,
            const aap_ara_document_properties_t* properties);

    RT_UNSAFE void (*destroy_document) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_document_id_t document_id);

    RT_UNSAFE void (*create_musical_context) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_document_id_t document_id,
            aap_ara_musical_context_id_t musical_context_id,
            const aap_ara_musical_context_properties_t* properties);

    RT_UNSAFE void (*update_musical_context_properties) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_musical_context_id_t musical_context_id,
            const aap_ara_musical_context_properties_t* properties);

    RT_UNSAFE void (*destroy_musical_context) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_musical_context_id_t musical_context_id);

    RT_UNSAFE void (*create_region_sequence) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_document_id_t document_id,
            aap_ara_region_sequence_id_t region_sequence_id,
            const aap_ara_region_sequence_properties_t* properties);

    RT_UNSAFE void (*update_region_sequence_properties) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_region_sequence_id_t region_sequence_id,
            const aap_ara_region_sequence_properties_t* properties);

    RT_UNSAFE void (*destroy_region_sequence) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_region_sequence_id_t region_sequence_id);

    RT_UNSAFE void (*create_audio_source) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_document_id_t document_id,
            aap_ara_audio_source_id_t audio_source_id,
            const aap_ara_audio_source_properties_t* properties);

    RT_UNSAFE void (*update_audio_source_properties) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_source_id_t audio_source_id,
            const aap_ara_audio_source_properties_t* properties);

    RT_UNSAFE void (*enable_audio_source_samples_access) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_source_id_t audio_source_id,
            bool enable);

    RT_UNSAFE void (*notify_audio_source_content_changed) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_source_id_t audio_source_id,
            aap_ara_sample_position_t changed_start_sample,
            aap_ara_sample_count_t changed_sample_count);

    RT_UNSAFE void (*destroy_audio_source) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_source_id_t audio_source_id);

    RT_UNSAFE void (*create_audio_modification) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_source_id_t audio_source_id,
            aap_ara_audio_modification_id_t audio_modification_id,
            const aap_ara_audio_modification_properties_t* properties);

    RT_UNSAFE void (*update_audio_modification_properties) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_modification_id_t audio_modification_id,
            const aap_ara_audio_modification_properties_t* properties);

    RT_UNSAFE void (*destroy_audio_modification) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_modification_id_t audio_modification_id);

    RT_UNSAFE void (*create_playback_region) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_audio_modification_id_t audio_modification_id,
            aap_ara_playback_region_id_t playback_region_id,
            const aap_ara_playback_region_properties_t* properties);

    RT_UNSAFE void (*update_playback_region_properties) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_playback_region_id_t playback_region_id,
            const aap_ara_playback_region_properties_t* properties);

    RT_UNSAFE void (*destroy_playback_region) (
            struct aap_ara_extension_t* ext,
            AndroidAudioPlugin* plugin,
            aap_ara_playback_region_id_t playback_region_id);
} aap_ara_extension_t;

/*
 * The host extension is driven by the plug-in. It provides the host-side
 * services needed by the plug-in after the host has declared the model graph.
 *
 * For read_audio_source_samples(), `destination->data` is an output field.
 * After the call returns, it points to an AAPXS-owned shared-memory region
 * containing tightly packed sample planes. The plug-in must not free it and
 * should treat it as valid only until the next ARA host-extension call on the
 * same instance.
 */
typedef struct aap_ara_host_extension_t {
    /* Reserved for future AAPXS-backed transport internals. */
    void* aapxs_context;

    RT_UNSAFE void (*get_host_capability) (
            struct aap_ara_host_extension_t* ext,
            AndroidAudioPluginHost* host,
            aap_ara_host_capability_t* destination);

    RT_UNSAFE void (*read_audio_source_samples) (
            struct aap_ara_host_extension_t* ext,
            AndroidAudioPluginHost* host,
            aap_ara_audio_source_id_t audio_source_id,
            const aap_ara_audio_source_sample_range_t* sample_range,
            aap_ara_audio_source_samples_buffer_t* destination);
} aap_ara_host_extension_t;

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* AAP_ARA_H_INCLUDED */
