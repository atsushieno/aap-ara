#ifndef AAP_CORE_ARA_AAPXS_H
#define AAP_CORE_ARA_AAPXS_H

#include "aap/core/aapxs/typed-aapxs.h"
#include "aap/ext/ara.h"

// plugin extension opcodes
const int32_t OPCODE_ARA_GET_FACTORY_CAPABILITY = 1;
const int32_t OPCODE_ARA_BEGIN_MODEL_UPDATE = 2;
const int32_t OPCODE_ARA_END_MODEL_UPDATE = 3;
const int32_t OPCODE_ARA_CREATE_DOCUMENT = 4;
const int32_t OPCODE_ARA_UPDATE_DOCUMENT_PROPERTIES = 5;
const int32_t OPCODE_ARA_DESTROY_DOCUMENT = 6;
const int32_t OPCODE_ARA_CREATE_MUSICAL_CONTEXT = 7;
const int32_t OPCODE_ARA_UPDATE_MUSICAL_CONTEXT_PROPERTIES = 8;
const int32_t OPCODE_ARA_DESTROY_MUSICAL_CONTEXT = 9;
const int32_t OPCODE_ARA_CREATE_REGION_SEQUENCE = 10;
const int32_t OPCODE_ARA_UPDATE_REGION_SEQUENCE_PROPERTIES = 11;
const int32_t OPCODE_ARA_DESTROY_REGION_SEQUENCE = 12;
const int32_t OPCODE_ARA_CREATE_AUDIO_SOURCE = 13;
const int32_t OPCODE_ARA_UPDATE_AUDIO_SOURCE_PROPERTIES = 14;
const int32_t OPCODE_ARA_ENABLE_AUDIO_SOURCE_SAMPLES_ACCESS = 15;
const int32_t OPCODE_ARA_NOTIFY_AUDIO_SOURCE_CONTENT_CHANGED = 16;
const int32_t OPCODE_ARA_DESTROY_AUDIO_SOURCE = 17;
const int32_t OPCODE_ARA_CREATE_AUDIO_MODIFICATION = 18;
const int32_t OPCODE_ARA_UPDATE_AUDIO_MODIFICATION_PROPERTIES = 19;
const int32_t OPCODE_ARA_DESTROY_AUDIO_MODIFICATION = 20;
const int32_t OPCODE_ARA_CREATE_PLAYBACK_REGION = 21;
const int32_t OPCODE_ARA_UPDATE_PLAYBACK_REGION_PROPERTIES = 22;
const int32_t OPCODE_ARA_DESTROY_PLAYBACK_REGION = 23;

// host extension opcodes
const int32_t OPCODE_ARA_GET_HOST_CAPABILITY = -1;
const int32_t OPCODE_ARA_READ_AUDIO_SOURCE_SAMPLES = -2;

/*
 * Shared memory backing for both control payloads and chunked sample reads.
 * Sample payloads are returned in this extension-owned shared memory region,
 * immediately following a small response header.
 */
const int32_t ARA_SHARED_MEMORY_SIZE = 0x100000; // 1M

typedef struct aap_ara_optional_color_wire_t {
    bool has_color;
    aap_ara_color_t color;
} aap_ara_optional_color_wire_t;

typedef struct aap_ara_document_properties_wire_t {
    char name[AAP_ARA_MAX_NAME_CHARS];
    char persistent_id[AAP_ARA_MAX_PERSISTENT_ID_CHARS];
} aap_ara_document_properties_wire_t;

typedef struct aap_ara_musical_context_properties_wire_t {
    char name[AAP_ARA_MAX_NAME_CHARS];
    char persistent_id[AAP_ARA_MAX_PERSISTENT_ID_CHARS];
    aap_ara_optional_color_wire_t color;
} aap_ara_musical_context_properties_wire_t;

typedef struct aap_ara_region_sequence_properties_wire_t {
    char name[AAP_ARA_MAX_NAME_CHARS];
    int32_t order_index;
    aap_ara_musical_context_id_t musical_context_id;
    aap_ara_optional_color_wire_t color;
    char persistent_id[AAP_ARA_MAX_PERSISTENT_ID_CHARS];
} aap_ara_region_sequence_properties_wire_t;

typedef struct aap_ara_audio_source_properties_wire_t {
    char name[AAP_ARA_MAX_NAME_CHARS];
    char persistent_id[AAP_ARA_MAX_PERSISTENT_ID_CHARS];
    aap_ara_sample_count_t sample_count;
    aap_ara_sample_rate_t sample_rate;
    int32_t channel_count;
    bool merits_64_bit_samples;
    char channel_layout_tag[AAP_ARA_MAX_CHANNEL_LAYOUT_TAG_CHARS];
} aap_ara_audio_source_properties_wire_t;

typedef struct aap_ara_audio_modification_properties_wire_t {
    char name[AAP_ARA_MAX_NAME_CHARS];
    char persistent_id[AAP_ARA_MAX_PERSISTENT_ID_CHARS];
} aap_ara_audio_modification_properties_wire_t;

typedef struct aap_ara_playback_region_properties_wire_t {
    uint32_t transformation_flags;
    aap_ara_time_position_t start_in_modification_time;
    aap_ara_time_duration_t duration_in_modification_time;
    aap_ara_time_position_t start_in_playback_time;
    aap_ara_time_duration_t duration_in_playback_time;
    aap_ara_region_sequence_id_t region_sequence_id;
    char name[AAP_ARA_MAX_NAME_CHARS];
    aap_ara_optional_color_wire_t color;
} aap_ara_playback_region_properties_wire_t;

typedef struct aap_ara_begin_model_update_wire_t {
    uint32_t flags;
} aap_ara_begin_model_update_wire_t;

typedef struct aap_ara_document_wire_t {
    aap_ara_document_id_t document_id;
    aap_ara_document_properties_wire_t properties;
} aap_ara_document_wire_t;

typedef struct aap_ara_musical_context_wire_t {
    aap_ara_document_id_t document_id;
    aap_ara_musical_context_id_t musical_context_id;
    aap_ara_musical_context_properties_wire_t properties;
} aap_ara_musical_context_wire_t;

typedef struct aap_ara_region_sequence_wire_t {
    aap_ara_document_id_t document_id;
    aap_ara_region_sequence_id_t region_sequence_id;
    aap_ara_region_sequence_properties_wire_t properties;
} aap_ara_region_sequence_wire_t;

typedef struct aap_ara_audio_source_wire_t {
    aap_ara_document_id_t document_id;
    aap_ara_audio_source_id_t audio_source_id;
    aap_ara_audio_source_properties_wire_t properties;
} aap_ara_audio_source_wire_t;

typedef struct aap_ara_enable_audio_source_samples_access_wire_t {
    aap_ara_audio_source_id_t audio_source_id;
    bool enable;
} aap_ara_enable_audio_source_samples_access_wire_t;

typedef struct aap_ara_notify_audio_source_content_changed_wire_t {
    aap_ara_audio_source_id_t audio_source_id;
    aap_ara_sample_position_t changed_start_sample;
    aap_ara_sample_count_t changed_sample_count;
} aap_ara_notify_audio_source_content_changed_wire_t;

typedef struct aap_ara_audio_modification_wire_t {
    aap_ara_audio_source_id_t audio_source_id;
    aap_ara_audio_modification_id_t audio_modification_id;
    aap_ara_audio_modification_properties_wire_t properties;
} aap_ara_audio_modification_wire_t;

typedef struct aap_ara_playback_region_wire_t {
    aap_ara_audio_modification_id_t audio_modification_id;
    aap_ara_playback_region_id_t playback_region_id;
    aap_ara_playback_region_properties_wire_t properties;
} aap_ara_playback_region_wire_t;

typedef struct aap_ara_object_id_wire_t {
    int64_t object_id;
} aap_ara_object_id_wire_t;

typedef struct aap_ara_read_audio_source_samples_wire_t {
    aap_ara_audio_source_id_t audio_source_id;
    aap_ara_audio_source_sample_range_t sample_range;
    size_t result_data_size;
    aap_ara_sample_count_t result_sample_count;
    int32_t result_channel_count;
    uint32_t result_sample_format;
} aap_ara_read_audio_source_samples_wire_t;

namespace aap::xs {
    class AraClientAAPXS : public TypedAAPXS {
        static void staticGetFactoryCapability(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_factory_capability_t* destination) {
            ((AraClientAAPXS*) ext->aapxs_context)->getFactoryCapability(*destination);
        }
        static void staticBeginModelUpdate(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, uint32_t flags) {
            ((AraClientAAPXS*) ext->aapxs_context)->beginModelUpdate(flags);
        }
        static void staticEndModelUpdate(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin) {
            ((AraClientAAPXS*) ext->aapxs_context)->endModelUpdate();
        }
        static void staticCreateDocument(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->createDocument(documentId, properties);
        }
        static void staticUpdateDocumentProperties(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->updateDocumentProperties(documentId, properties);
        }
        static void staticDestroyDocument(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId) {
            ((AraClientAAPXS*) ext->aapxs_context)->destroyDocument(documentId);
        }
        static void staticCreateMusicalContext(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->createMusicalContext(documentId, musicalContextId, properties);
        }
        static void staticUpdateMusicalContextProperties(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->updateMusicalContextProperties(musicalContextId, properties);
        }
        static void staticDestroyMusicalContext(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musicalContextId) {
            ((AraClientAAPXS*) ext->aapxs_context)->destroyMusicalContext(musicalContextId);
        }
        static void staticCreateRegionSequence(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->createRegionSequence(documentId, regionSequenceId, properties);
        }
        static void staticUpdateRegionSequenceProperties(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->updateRegionSequenceProperties(regionSequenceId, properties);
        }
        static void staticDestroyRegionSequence(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t regionSequenceId) {
            ((AraClientAAPXS*) ext->aapxs_context)->destroyRegionSequence(regionSequenceId);
        }
        static void staticCreateAudioSource(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t documentId, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->createAudioSource(documentId, audioSourceId, properties);
        }
        static void staticUpdateAudioSourceProperties(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->updateAudioSourceProperties(audioSourceId, properties);
        }
        static void staticEnableAudioSourceSamplesAccess(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, bool enable) {
            ((AraClientAAPXS*) ext->aapxs_context)->enableAudioSourceSamplesAccess(audioSourceId, enable);
        }
        static void staticNotifyAudioSourceContentChanged(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, aap_ara_sample_position_t changedStartSample, aap_ara_sample_count_t changedSampleCount) {
            ((AraClientAAPXS*) ext->aapxs_context)->notifyAudioSourceContentChanged(audioSourceId, changedStartSample, changedSampleCount);
        }
        static void staticDestroyAudioSource(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId) {
            ((AraClientAAPXS*) ext->aapxs_context)->destroyAudioSource(audioSourceId);
        }
        static void staticCreateAudioModification(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audioSourceId, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->createAudioModification(audioSourceId, audioModificationId, properties);
        }
        static void staticUpdateAudioModificationProperties(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->updateAudioModificationProperties(audioModificationId, properties);
        }
        static void staticDestroyAudioModification(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId) {
            ((AraClientAAPXS*) ext->aapxs_context)->destroyAudioModification(audioModificationId);
        }
        static void staticCreatePlaybackRegion(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audioModificationId, aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->createPlaybackRegion(audioModificationId, playbackRegionId, properties);
        }
        static void staticUpdatePlaybackRegionProperties(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties) {
            ((AraClientAAPXS*) ext->aapxs_context)->updatePlaybackRegionProperties(playbackRegionId, properties);
        }
        static void staticDestroyPlaybackRegion(aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_playback_region_id_t playbackRegionId) {
            ((AraClientAAPXS*) ext->aapxs_context)->destroyPlaybackRegion(playbackRegionId);
        }

        aap_ara_extension_t as_plugin_extension {
                this,
                staticGetFactoryCapability,
                staticBeginModelUpdate,
                staticEndModelUpdate,
                staticCreateDocument,
                staticUpdateDocumentProperties,
                staticDestroyDocument,
                staticCreateMusicalContext,
                staticUpdateMusicalContextProperties,
                staticDestroyMusicalContext,
                staticCreateRegionSequence,
                staticUpdateRegionSequenceProperties,
                staticDestroyRegionSequence,
                staticCreateAudioSource,
                staticUpdateAudioSourceProperties,
                staticEnableAudioSourceSamplesAccess,
                staticNotifyAudioSourceContentChanged,
                staticDestroyAudioSource,
                staticCreateAudioModification,
                staticUpdateAudioModificationProperties,
                staticDestroyAudioModification,
                staticCreatePlaybackRegion,
                staticUpdatePlaybackRegionProperties,
                staticDestroyPlaybackRegion
        };

        // Synchronously invokes a void model-edit command and logs any failure/timeout. The
        // aap_ara_extension_t functions return void, so the error cannot be propagated further;
        // logging is the only in-API way to surface it.
        void invokeVoidEdit(int32_t opcode);

    public:
        AraClientAAPXS(AAPXSInitiatorInstance* initiatorInstance, AAPXSSerializationContext* serialization)
                : TypedAAPXS(AAP_ARA_EXTENSION_URI, initiatorInstance, serialization) {}

        void getFactoryCapability(aap_ara_factory_capability_t& destination);
        void beginModelUpdate(uint32_t flags);
        void endModelUpdate();
        void createDocument(aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties);
        void updateDocumentProperties(aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties);
        void destroyDocument(aap_ara_document_id_t documentId);
        void createMusicalContext(aap_ara_document_id_t documentId, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties);
        void updateMusicalContextProperties(aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties);
        void destroyMusicalContext(aap_ara_musical_context_id_t musicalContextId);
        void createRegionSequence(aap_ara_document_id_t documentId, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties);
        void updateRegionSequenceProperties(aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties);
        void destroyRegionSequence(aap_ara_region_sequence_id_t regionSequenceId);
        void createAudioSource(aap_ara_document_id_t documentId, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties);
        void updateAudioSourceProperties(aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties);
        void enableAudioSourceSamplesAccess(aap_ara_audio_source_id_t audioSourceId, bool enable);
        void notifyAudioSourceContentChanged(aap_ara_audio_source_id_t audioSourceId, aap_ara_sample_position_t changedStartSample, aap_ara_sample_count_t changedSampleCount);
        void destroyAudioSource(aap_ara_audio_source_id_t audioSourceId);
        void createAudioModification(aap_ara_audio_source_id_t audioSourceId, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties);
        void updateAudioModificationProperties(aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties);
        void destroyAudioModification(aap_ara_audio_modification_id_t audioModificationId);
        void createPlaybackRegion(aap_ara_audio_modification_id_t audioModificationId, aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties);
        void updatePlaybackRegionProperties(aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties);
        void destroyPlaybackRegion(aap_ara_playback_region_id_t playbackRegionId);

        aap_ara_extension_t* asPluginExtension() { return &as_plugin_extension; }
    };

    class AraServiceAAPXS : public TypedAAPXS {
        static void staticGetHostCapability(aap_ara_host_extension_t* ext, AndroidAudioPluginHost* host, aap_ara_host_capability_t* destination) {
            ((AraServiceAAPXS*) ext->aapxs_context)->getHostCapability(*destination);
        }
        static void staticReadAudioSourceSamples(aap_ara_host_extension_t* ext, AndroidAudioPluginHost* host, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_sample_range_t* sampleRange, aap_ara_audio_source_samples_buffer_t* destination) {
            ((AraServiceAAPXS*) ext->aapxs_context)->readAudioSourceSamples(audioSourceId, sampleRange, destination);
        }
        aap_ara_host_extension_t as_host_extension{this, staticGetHostCapability, staticReadAudioSourceSamples};

    public:
        AraServiceAAPXS(AAPXSInitiatorInstance* initiatorInstance, AAPXSSerializationContext* serialization)
                : TypedAAPXS(AAP_ARA_EXTENSION_URI, initiatorInstance, serialization) {}

        void getHostCapability(aap_ara_host_capability_t& destination);
        void readAudioSourceSamples(aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_sample_range_t* sampleRange, aap_ara_audio_source_samples_buffer_t* destination);

        aap_ara_host_extension_t* asHostExtension() { return &as_host_extension; }
    };

    class AAPXSDefinition_Ara : public AAPXSDefinitionWrapper {
        static void aapxs_ara_process_incoming_plugin_aapxs_request(
                struct AAPXSDefinition* feature,
                AAPXSRecipientInstance* aapxsInstance,
                AndroidAudioPlugin* plugin,
                AAPXSRequestContext* request);
        static void aapxs_ara_process_incoming_host_aapxs_request(
                struct AAPXSDefinition* feature,
                AAPXSRecipientInstance* aapxsInstance,
                AndroidAudioPluginHost* host,
                AAPXSRequestContext* request);
        static void aapxs_ara_process_incoming_plugin_aapxs_reply(
                struct AAPXSDefinition* feature,
                AAPXSInitiatorInstance* aapxsInstance,
                AndroidAudioPlugin* plugin,
                AAPXSRequestContext* request);
        static void aapxs_ara_process_incoming_host_aapxs_reply(
                struct AAPXSDefinition* feature,
                AAPXSInitiatorInstance* aapxsInstance,
                AndroidAudioPluginHost* host,
                AAPXSRequestContext* request);
        static AAPXSExtensionClientProxy aapxs_ara_get_plugin_proxy(
                struct AAPXSDefinition* feature,
                AAPXSInitiatorInstance* aapxsInstance,
                AAPXSSerializationContext* serialization);
        static AAPXSExtensionServiceProxy aapxs_ara_get_host_proxy(
                struct AAPXSDefinition* feature,
                AAPXSInitiatorInstance* aapxsInstance,
                AAPXSSerializationContext* serialization);
        static void* aapxs_ara_as_plugin_extension(AAPXSExtensionClientProxy* proxy) {
            return ((AraClientAAPXS*) proxy->aapxs_context)->asPluginExtension();
        }
        static void* aapxs_ara_as_host_extension(AAPXSExtensionServiceProxy* proxy) {
            return ((AraServiceAAPXS*) proxy->aapxs_context)->asHostExtension();
        }

        AAPXSDefinition aapxs_ara{this,
                                  AAP_ARA_EXTENSION_URI,
                                  ARA_SHARED_MEMORY_SIZE,
                                  aapxs_ara_process_incoming_plugin_aapxs_request,
                                  aapxs_ara_process_incoming_host_aapxs_request,
                                  aapxs_ara_process_incoming_plugin_aapxs_reply,
                                  aapxs_ara_process_incoming_host_aapxs_reply,
                                  aapxs_ara_get_plugin_proxy,
                                  aapxs_ara_get_host_proxy,
                                  // is_command_rt_safe == nullptr: ARA is Binder-only. ARA model
                                  // edits/reads are not real-time and (e.g. readAudioSourceSamples)
                                  // can transfer large payloads, so they must never take the SysEx8
                                  // realtime path. They are invoked asynchronously over Binder.
                                  nullptr};

    public:
        AAPXSDefinition& asPublic() override { return aapxs_ara; }
    };
}

#endif // AAP_CORE_ARA_AAPXS_H
