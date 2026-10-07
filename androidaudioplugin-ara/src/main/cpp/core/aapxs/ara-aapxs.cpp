#include "aap/core/aapxs/ara-aapxs.h"
#include <cstring>
#include <algorithm>
#include <mutex>
#include "aap/unstable/logging.h"

#define LOG_TAG "AAP.ARA.AAPXS"

uint32_t aap::xs::AAPXSDefinition_Ara::aapxs_ara_get_request_flags(
        AAPXSDefinition* definition, bool isHostExtension, int32_t opcode) {
    if (isHostExtension) {
        if (definition->process_incoming_host_aapxs_request != aapxs_ara_process_incoming_host_aapxs_request)
            return 0;
        switch (opcode) {
            case OPCODE_ARA_GET_HOST_CAPABILITY:
            case OPCODE_ARA_READ_AUDIO_SOURCE_SAMPLES:
                return AAPXS_REQUEST_READ_ONLY;
            default:
                // Content updates carry payloads; none may be coalesced or dropped.
                return 0;
        }
    }
    if (definition->process_incoming_plugin_aapxs_request != aapxs_ara_process_incoming_plugin_aapxs_request)
        return 0;
    switch (opcode) {
        case OPCODE_ARA_GET_FACTORY_CAPABILITY:
        case OPCODE_ARA_STORE_MODIFICATION_STATE:
            // Query/export does not mutate the model. It still accesses plugin
            // state, so READ_ONLY does not grant concurrent dispatch or RT safety.
            return AAPXS_REQUEST_READ_ONLY;
        default:
            // Model edits, restore and unknown requests retain control exclusion
            // and the runtime's conservative snapshot invalidation.
            return 0;
    }
}

namespace {
template <typename T>
static inline T* asWire(AAPXSSerializationContext* serialization) {
    return reinterpret_cast<T*>(serialization->data);
}

static inline void copyString(char* destination, size_t capacity, const char* source) {
    if (!destination || capacity == 0)
        return;
    if (!source) {
        destination[0] = 0;
        return;
    }
    strncpy(destination, source, capacity - 1);
    destination[capacity - 1] = 0;
}

static inline const aap_ara_color_t* unpackColor(const aap_ara_optional_color_wire_t& wire, aap_ara_color_t& scratch) {
    if (!wire.has_color)
        return nullptr;
    scratch = wire.color;
    return &scratch;
}

static inline void packColor(aap_ara_optional_color_wire_t& wire, const aap_ara_color_t* color) {
    wire.has_color = color != nullptr;
    wire.color = color ? *color : aap_ara_color_t{};
}

static inline void packDocumentProperties(aap_ara_document_properties_wire_t& wire, const aap_ara_document_properties_t* properties) {
    memset(&wire, 0, sizeof(wire));
    if (!properties)
        return;
    copyString(wire.name, sizeof(wire.name), properties->name);
    copyString(wire.persistent_id, sizeof(wire.persistent_id), properties->persistent_id);
}

static inline aap_ara_document_properties_t unpackDocumentProperties(const aap_ara_document_properties_wire_t& wire) {
    return aap_ara_document_properties_t{
            sizeof(aap_ara_document_properties_t),
            wire.name[0] ? wire.name : nullptr,
            wire.persistent_id[0] ? wire.persistent_id : nullptr
    };
}

static inline void packMusicalContextProperties(aap_ara_musical_context_properties_wire_t& wire, const aap_ara_musical_context_properties_t* properties) {
    memset(&wire, 0, sizeof(wire));
    if (!properties)
        return;
    copyString(wire.name, sizeof(wire.name), properties->name);
    copyString(wire.persistent_id, sizeof(wire.persistent_id), properties->persistent_id);
    packColor(wire.color, properties->color);
}

static inline aap_ara_musical_context_properties_t unpackMusicalContextProperties(const aap_ara_musical_context_properties_wire_t& wire, aap_ara_color_t& colorScratch) {
    return aap_ara_musical_context_properties_t{
            sizeof(aap_ara_musical_context_properties_t),
            wire.name[0] ? wire.name : nullptr,
            unpackColor(wire.color, colorScratch),
            wire.persistent_id[0] ? wire.persistent_id : nullptr
    };
}

static inline void packRegionSequenceProperties(aap_ara_region_sequence_properties_wire_t& wire, const aap_ara_region_sequence_properties_t* properties) {
    memset(&wire, 0, sizeof(wire));
    if (!properties)
        return;
    copyString(wire.name, sizeof(wire.name), properties->name);
    wire.order_index = properties->order_index;
    wire.musical_context_id = properties->musical_context_id;
    packColor(wire.color, properties->color);
    copyString(wire.persistent_id, sizeof(wire.persistent_id), properties->persistent_id);
}

static inline aap_ara_region_sequence_properties_t unpackRegionSequenceProperties(const aap_ara_region_sequence_properties_wire_t& wire, aap_ara_color_t& colorScratch) {
    return aap_ara_region_sequence_properties_t{
            sizeof(aap_ara_region_sequence_properties_t),
            wire.name[0] ? wire.name : nullptr,
            wire.order_index,
            wire.musical_context_id,
            unpackColor(wire.color, colorScratch),
            wire.persistent_id[0] ? wire.persistent_id : nullptr
    };
}

static inline void packAudioSourceProperties(aap_ara_audio_source_properties_wire_t& wire, const aap_ara_audio_source_properties_t* properties) {
    memset(&wire, 0, sizeof(wire));
    if (!properties)
        return;
    copyString(wire.name, sizeof(wire.name), properties->name);
    copyString(wire.persistent_id, sizeof(wire.persistent_id), properties->persistent_id);
    wire.sample_count = properties->sample_count;
    wire.sample_rate = properties->sample_rate;
    wire.channel_count = properties->channel_count;
    wire.merits_64_bit_samples = properties->merits_64_bit_samples;
    copyString(wire.channel_layout_tag, sizeof(wire.channel_layout_tag), properties->channel_layout_tag);
}

static inline aap_ara_audio_source_properties_t unpackAudioSourceProperties(const aap_ara_audio_source_properties_wire_t& wire) {
    return aap_ara_audio_source_properties_t{
            sizeof(aap_ara_audio_source_properties_t),
            wire.name[0] ? wire.name : nullptr,
            wire.persistent_id[0] ? wire.persistent_id : nullptr,
            wire.sample_count,
            wire.sample_rate,
            wire.channel_count,
            wire.merits_64_bit_samples,
            wire.channel_layout_tag[0] ? wire.channel_layout_tag : nullptr
    };
}

static inline void packAudioModificationProperties(aap_ara_audio_modification_properties_wire_t& wire, const aap_ara_audio_modification_properties_t* properties) {
    memset(&wire, 0, sizeof(wire));
    if (!properties)
        return;
    copyString(wire.name, sizeof(wire.name), properties->name);
    copyString(wire.persistent_id, sizeof(wire.persistent_id), properties->persistent_id);
}

static inline aap_ara_audio_modification_properties_t unpackAudioModificationProperties(const aap_ara_audio_modification_properties_wire_t& wire) {
    return aap_ara_audio_modification_properties_t{
            sizeof(aap_ara_audio_modification_properties_t),
            wire.name[0] ? wire.name : nullptr,
            wire.persistent_id[0] ? wire.persistent_id : nullptr
    };
}

static inline void packPlaybackRegionProperties(aap_ara_playback_region_properties_wire_t& wire, const aap_ara_playback_region_properties_t* properties) {
    memset(&wire, 0, sizeof(wire));
    if (!properties)
        return;
    wire.transformation_flags = properties->transformation_flags;
    wire.start_in_modification_time = properties->start_in_modification_time;
    wire.duration_in_modification_time = properties->duration_in_modification_time;
    wire.start_in_playback_time = properties->start_in_playback_time;
    wire.duration_in_playback_time = properties->duration_in_playback_time;
    wire.region_sequence_id = properties->region_sequence_id;
    copyString(wire.name, sizeof(wire.name), properties->name);
    packColor(wire.color, properties->color);
}

static inline aap_ara_playback_region_properties_t unpackPlaybackRegionProperties(const aap_ara_playback_region_properties_wire_t& wire, aap_ara_color_t& colorScratch) {
    return aap_ara_playback_region_properties_t{
            sizeof(aap_ara_playback_region_properties_t),
            wire.transformation_flags,
            wire.start_in_modification_time,
            wire.duration_in_modification_time,
            wire.start_in_playback_time,
            wire.duration_in_playback_time,
            wire.region_sequence_id,
            wire.name[0] ? wire.name : nullptr,
            unpackColor(wire.color, colorScratch)
    };
}
}

void aap::xs::AAPXSDefinition_Ara::aapxs_ara_process_incoming_plugin_aapxs_request(
        struct AAPXSDefinition* feature, AAPXSRecipientInstance* aapxsInstance,
        AndroidAudioPlugin* plugin, AAPXSRequestContext* request) {
    auto ext = (aap_ara_extension_t*) plugin->get_extension(plugin, AAP_ARA_EXTENSION_URI);
    auto* serialization = request->serialization;
    aap_ara_color_t colorScratch{};

    switch (request->opcode) {
        case OPCODE_ARA_GET_FACTORY_CAPABILITY: {
            auto capability = aap_ara_factory_capability_t{};
            capability.struct_size = sizeof(aap_ara_factory_capability_t);
            if (ext && ext->get_factory_capability)
                ext->get_factory_capability(ext, plugin, &capability);
            *asWire<aap_ara_factory_capability_t>(serialization) = capability;
            serialization->data_size = sizeof(aap_ara_factory_capability_t);
            aapxsInstance->send_aapxs_reply(aapxsInstance, request);
            return;
        }
        case OPCODE_ARA_BEGIN_MODEL_UPDATE: {
            if (ext && ext->begin_model_update)
                ext->begin_model_update(ext, plugin, asWire<aap_ara_begin_model_update_wire_t>(serialization)->flags);
            break;
        }
        case OPCODE_ARA_END_MODEL_UPDATE:
            if (ext && ext->end_model_update)
                ext->end_model_update(ext, plugin);
            break;
        case OPCODE_ARA_CREATE_DOCUMENT: {
            auto* wire = asWire<aap_ara_document_wire_t>(serialization);
            auto properties = unpackDocumentProperties(wire->properties);
            if (ext && ext->create_document)
                ext->create_document(ext, plugin, wire->document_id, &properties);
            break;
        }
        case OPCODE_ARA_UPDATE_DOCUMENT_PROPERTIES: {
            auto* wire = asWire<aap_ara_document_wire_t>(serialization);
            auto properties = unpackDocumentProperties(wire->properties);
            if (ext && ext->update_document_properties)
                ext->update_document_properties(ext, plugin, wire->document_id, &properties);
            break;
        }
        case OPCODE_ARA_DESTROY_DOCUMENT:
            if (ext && ext->destroy_document)
                ext->destroy_document(ext, plugin, asWire<aap_ara_object_id_wire_t>(serialization)->object_id);
            break;
        case OPCODE_ARA_CREATE_MUSICAL_CONTEXT: {
            auto* wire = asWire<aap_ara_musical_context_wire_t>(serialization);
            auto properties = unpackMusicalContextProperties(wire->properties, colorScratch);
            if (ext && ext->create_musical_context)
                ext->create_musical_context(ext, plugin, wire->document_id, wire->musical_context_id, &properties);
            break;
        }
        case OPCODE_ARA_UPDATE_MUSICAL_CONTEXT_PROPERTIES: {
            auto* wire = asWire<aap_ara_musical_context_wire_t>(serialization);
            auto properties = unpackMusicalContextProperties(wire->properties, colorScratch);
            if (ext && ext->update_musical_context_properties)
                ext->update_musical_context_properties(ext, plugin, wire->musical_context_id, &properties);
            break;
        }
        case OPCODE_ARA_DESTROY_MUSICAL_CONTEXT:
            if (ext && ext->destroy_musical_context)
                ext->destroy_musical_context(ext, plugin, asWire<aap_ara_object_id_wire_t>(serialization)->object_id);
            break;
        case OPCODE_ARA_CREATE_REGION_SEQUENCE: {
            auto* wire = asWire<aap_ara_region_sequence_wire_t>(serialization);
            auto properties = unpackRegionSequenceProperties(wire->properties, colorScratch);
            if (ext && ext->create_region_sequence)
                ext->create_region_sequence(ext, plugin, wire->document_id, wire->region_sequence_id, &properties);
            break;
        }
        case OPCODE_ARA_UPDATE_REGION_SEQUENCE_PROPERTIES: {
            auto* wire = asWire<aap_ara_region_sequence_wire_t>(serialization);
            auto properties = unpackRegionSequenceProperties(wire->properties, colorScratch);
            if (ext && ext->update_region_sequence_properties)
                ext->update_region_sequence_properties(ext, plugin, wire->region_sequence_id, &properties);
            break;
        }
        case OPCODE_ARA_DESTROY_REGION_SEQUENCE:
            if (ext && ext->destroy_region_sequence)
                ext->destroy_region_sequence(ext, plugin, asWire<aap_ara_object_id_wire_t>(serialization)->object_id);
            break;
        case OPCODE_ARA_CREATE_AUDIO_SOURCE: {
            auto* wire = asWire<aap_ara_audio_source_wire_t>(serialization);
            auto properties = unpackAudioSourceProperties(wire->properties);
            if (ext && ext->create_audio_source)
                ext->create_audio_source(ext, plugin, wire->document_id, wire->audio_source_id, &properties);
            break;
        }
        case OPCODE_ARA_UPDATE_AUDIO_SOURCE_PROPERTIES: {
            auto* wire = asWire<aap_ara_audio_source_wire_t>(serialization);
            auto properties = unpackAudioSourceProperties(wire->properties);
            if (ext && ext->update_audio_source_properties)
                ext->update_audio_source_properties(ext, plugin, wire->audio_source_id, &properties);
            break;
        }
        case OPCODE_ARA_ENABLE_AUDIO_SOURCE_SAMPLES_ACCESS: {
            auto* wire = asWire<aap_ara_enable_audio_source_samples_access_wire_t>(serialization);
            if (ext && ext->enable_audio_source_samples_access)
                ext->enable_audio_source_samples_access(ext, plugin, wire->audio_source_id, wire->enable);
            break;
        }
        case OPCODE_ARA_NOTIFY_AUDIO_SOURCE_CONTENT_CHANGED: {
            auto* wire = asWire<aap_ara_notify_audio_source_content_changed_wire_t>(serialization);
            if (ext && ext->notify_audio_source_content_changed)
                ext->notify_audio_source_content_changed(ext, plugin, wire->audio_source_id, wire->changed_start_sample, wire->changed_sample_count);
            break;
        }
        case OPCODE_ARA_DESTROY_AUDIO_SOURCE:
            if (ext && ext->destroy_audio_source)
                ext->destroy_audio_source(ext, plugin, asWire<aap_ara_object_id_wire_t>(serialization)->object_id);
            break;
        case OPCODE_ARA_CREATE_AUDIO_MODIFICATION: {
            auto* wire = asWire<aap_ara_audio_modification_wire_t>(serialization);
            auto properties = unpackAudioModificationProperties(wire->properties);
            if (ext && ext->create_audio_modification)
                ext->create_audio_modification(ext, plugin, wire->audio_source_id, wire->audio_modification_id, &properties);
            break;
        }
        case OPCODE_ARA_UPDATE_AUDIO_MODIFICATION_PROPERTIES: {
            auto* wire = asWire<aap_ara_audio_modification_wire_t>(serialization);
            auto properties = unpackAudioModificationProperties(wire->properties);
            if (ext && ext->update_audio_modification_properties)
                ext->update_audio_modification_properties(ext, plugin, wire->audio_modification_id, &properties);
            break;
        }
        case OPCODE_ARA_DESTROY_AUDIO_MODIFICATION:
            if (ext && ext->destroy_audio_modification)
                ext->destroy_audio_modification(ext, plugin, asWire<aap_ara_object_id_wire_t>(serialization)->object_id);
            break;
        case OPCODE_ARA_CREATE_PLAYBACK_REGION: {
            auto* wire = asWire<aap_ara_playback_region_wire_t>(serialization);
            auto properties = unpackPlaybackRegionProperties(wire->properties, colorScratch);
            if (ext && ext->create_playback_region)
                ext->create_playback_region(ext, plugin, wire->audio_modification_id, wire->playback_region_id, &properties);
            break;
        }
        case OPCODE_ARA_UPDATE_PLAYBACK_REGION_PROPERTIES: {
            auto* wire = asWire<aap_ara_playback_region_wire_t>(serialization);
            auto properties = unpackPlaybackRegionProperties(wire->properties, colorScratch);
            if (ext && ext->update_playback_region_properties)
                ext->update_playback_region_properties(ext, plugin, wire->playback_region_id, &properties);
            break;
        }
        case OPCODE_ARA_DESTROY_PLAYBACK_REGION:
            if (ext && ext->destroy_playback_region)
                ext->destroy_playback_region(ext, plugin, asWire<aap_ara_object_id_wire_t>(serialization)->object_id);
            break;
        case OPCODE_ARA_STORE_MODIFICATION_STATE:
        case OPCODE_ARA_RESTORE_MODIFICATION_STATE: {
            // Binder carries the fixed shared-memory record, but not the
            // sender's serialization.data_size. Validate capacity and the
            // archive's embedded byte count instead.
            if (!serialization->data || serialization->data_capacity < sizeof(aap_ara_archive_wire_t)) break;
            auto* wire = asWire<aap_ara_archive_wire_t>(serialization);
            wire->succeeded = false;
            aap_ara_factory_capability_t capability{};
            capability.struct_size = sizeof(capability);
            if (ext && ext->get_factory_capability) ext->get_factory_capability(ext, plugin, &capability);
            if (capability.struct_size >= sizeof(capability) &&
                (capability.supported_features & AAP_ARA_FEATURE_MODIFICATION_ARCHIVE)) {
                if (request->opcode == OPCODE_ARA_STORE_MODIFICATION_STATE && ext->store_audio_modification_state) {
                    aap_ara_archive_buffer_t destination{sizeof(destination), wire->data, sizeof(wire->data), 0};
                    wire->succeeded = ext->store_audio_modification_state(ext, plugin, wire->modification_id, &destination)
                        && destination.data_size <= sizeof(wire->data);
                    wire->data_size = wire->succeeded ? destination.data_size : 0;
                } else if (wire->data_size <= sizeof(wire->data) && ext->restore_audio_modification_state)
                    wire->succeeded = ext->restore_audio_modification_state(ext, plugin, wire->modification_id, wire->data, wire->data_size);
            }
            serialization->data_size = sizeof(*wire);
            aapxsInstance->send_aapxs_reply(aapxsInstance, request);
            return;
        }
        default:
            break;
    }

    serialization->data_size = 0;
    aapxsInstance->send_aapxs_reply(aapxsInstance, request);
}

void aap::xs::AAPXSDefinition_Ara::aapxs_ara_process_incoming_host_aapxs_request(
        struct AAPXSDefinition* feature, AAPXSRecipientInstance* aapxsInstance,
        AndroidAudioPluginHost* host, AAPXSRequestContext* request) {
    auto ext = (aap_ara_host_extension_t*) host->get_extension(host, AAP_ARA_EXTENSION_URI);
    auto* serialization = request->serialization;
    switch (request->opcode) {
        case OPCODE_ARA_GET_HOST_CAPABILITY: {
            auto capability = aap_ara_host_capability_t{};
            capability.struct_size = sizeof(aap_ara_host_capability_t);
            if (ext && ext->get_host_capability)
                ext->get_host_capability(ext, host, &capability);
            *asWire<aap_ara_host_capability_t>(serialization) = capability;
            serialization->data_size = sizeof(aap_ara_host_capability_t);
            aapxsInstance->send_aapxs_reply(aapxsInstance, request);
            return;
        }
        case OPCODE_ARA_READ_AUDIO_SOURCE_SAMPLES: {
            auto* wire = asWire<aap_ara_read_audio_source_samples_wire_t>(serialization);
            if (wire) {
                wire->result_data_size = 0;
                wire->result_sample_count = 0;
                wire->result_channel_count = wire->sample_range.channel_count;
                wire->result_sample_format = wire->sample_range.sample_format;
            }
            if (ext && ext->read_audio_source_samples && wire) {
                aap_ara_audio_source_samples_buffer_t destination{
                        sizeof(aap_ara_audio_source_samples_buffer_t),
                        reinterpret_cast<uint8_t*>(serialization->data) + sizeof(aap_ara_read_audio_source_samples_wire_t),
                        serialization->data_capacity - sizeof(aap_ara_read_audio_source_samples_wire_t),
                        0,
                        wire->sample_range.channel_count,
                        wire->sample_range.sample_format
                };
                ext->read_audio_source_samples(ext, host, wire->audio_source_id, &wire->sample_range, &destination);
                wire->result_data_size = destination.data_size;
                wire->result_sample_count = destination.sample_count;
                wire->result_channel_count = destination.channel_count;
                wire->result_sample_format = destination.sample_format;
            }
            serialization->data_size = sizeof(aap_ara_read_audio_source_samples_wire_t) + wire->result_data_size;
            aapxsInstance->send_aapxs_reply(aapxsInstance, request);
            return;
        }
        case OPCODE_ARA_NOTIFY_CONTENT_CHANGED: {
            aap_ara_host_capability_t capability{};
            capability.struct_size = sizeof(capability);
            if (ext && ext->get_host_capability) ext->get_host_capability(ext, host, &capability);
            if (serialization->data && serialization->data_capacity >= sizeof(aap_ara_content_update_t) &&
                capability.struct_size >= sizeof(capability) &&
                (capability.supported_model_updates & AAP_ARA_HOST_SUPPORTS_CONTENT_UPDATES) && ext->notify_content_changed)
                ext->notify_content_changed(ext, host, asWire<aap_ara_content_update_t>(serialization));
            break;
        }
        default:
            break;
    }
    serialization->data_size = 0;
    aapxsInstance->send_aapxs_reply(aapxsInstance, request);
}

void aap::xs::AAPXSDefinition_Ara::aapxs_ara_process_incoming_plugin_aapxs_reply(
        struct AAPXSDefinition* feature, AAPXSInitiatorInstance* aapxsInstance,
        AndroidAudioPlugin* plugin, AAPXSRequestContext* request) {
    if (request->callback)
        request->callback(request->callback_user_data, plugin);
}

void aap::xs::AAPXSDefinition_Ara::aapxs_ara_process_incoming_host_aapxs_reply(
        struct AAPXSDefinition* feature, AAPXSInitiatorInstance* aapxsInstance,
        AndroidAudioPluginHost* host, AAPXSRequestContext* request) {
    if (request->callback)
        request->callback(request->callback_user_data, host);
}

AAPXSExtensionClientProxy aap::xs::AAPXSDefinition_Ara::aapxs_ara_get_plugin_proxy(
        struct AAPXSDefinition* feature, AAPXSInitiatorInstance* aapxsInstance,
        AAPXSSerializationContext* serialization) {
    (void) feature;
    (void) serialization;
    return AAPXSExtensionClientProxy{aapxsInstance->aapxs_context, aapxs_ara_as_plugin_extension};
}

AAPXSExtensionServiceProxy aap::xs::AAPXSDefinition_Ara::aapxs_ara_get_host_proxy(
        struct AAPXSDefinition* feature, AAPXSInitiatorInstance* aapxsInstance,
        AAPXSSerializationContext* serialization) {
    (void) feature;
    (void) serialization;
    return AAPXSExtensionServiceProxy{aapxsInstance->aapxs_context, aapxs_ara_as_host_extension};
}

void aap::xs::AAPXSDefinition_Ara::aapxs_ara_release_instance_context(AAPXSDefinition*, void* context) {
    delete static_cast<TypedAAPXS*>(context);
}

void aap::xs::AraClientAAPXS::invokeVoidEdit(int32_t opcode, const void* payload, size_t payloadSize) {
    auto result = callAndWait<bool>(opcode, payload, payloadSize, [](AAPXSSerializationContext*) { return true; }, 0);
    if (!result.isOk())
        aap::a_log_f(AAP_LOG_LEVEL_WARN, LOG_TAG, "ARA model edit (opcode %d) failed: %s",
                     opcode, result.error.c_str());
}

void aap::xs::AraClientAAPXS::getFactoryCapability(aap_ara_factory_capability_t& destination) {
    auto result = callAndWait<aap_ara_factory_capability_t>(OPCODE_ARA_GET_FACTORY_CAPABILITY, nullptr, 0,
            [](AAPXSSerializationContext* ctx) {
                aap_ara_factory_capability_t value{};
                memcpy(&value, ctx->data, std::min(ctx->data_size, sizeof(value)));
                return value;
            }, sizeof(aap_ara_factory_capability_t));
    if (result.isOk())
        memcpy(&destination, &result.value, std::min<size_t>(destination.struct_size, sizeof(destination)));
    else
        aap::a_log_f(AAP_LOG_LEVEL_WARN, LOG_TAG, "ARA getFactoryCapability failed: %s", result.error.c_str());
}

void aap::xs::AraClientAAPXS::beginModelUpdate(uint32_t flags) {
    aap_ara_begin_model_update_wire_t wire{flags};
    invokeVoidEdit(OPCODE_ARA_BEGIN_MODEL_UPDATE, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::endModelUpdate() {
    invokeVoidEdit(OPCODE_ARA_END_MODEL_UPDATE, nullptr, 0);
}

void aap::xs::AraClientAAPXS::createDocument(aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
    aap_ara_document_wire_t wire{};
    wire.document_id = documentId;
    packDocumentProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_CREATE_DOCUMENT, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::updateDocumentProperties(aap_ara_document_id_t documentId, const aap_ara_document_properties_t* properties) {
    aap_ara_document_wire_t wire{};
    wire.document_id = documentId;
    packDocumentProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_UPDATE_DOCUMENT_PROPERTIES, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::destroyDocument(aap_ara_document_id_t documentId) {
    aap_ara_object_id_wire_t wire{documentId};
    invokeVoidEdit(OPCODE_ARA_DESTROY_DOCUMENT, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::createMusicalContext(aap_ara_document_id_t documentId, aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
    aap_ara_musical_context_wire_t wire{};
    wire.document_id = documentId;
    wire.musical_context_id = musicalContextId;
    packMusicalContextProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_CREATE_MUSICAL_CONTEXT, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::updateMusicalContextProperties(aap_ara_musical_context_id_t musicalContextId, const aap_ara_musical_context_properties_t* properties) {
    aap_ara_musical_context_wire_t wire{};
    wire.musical_context_id = musicalContextId;
    packMusicalContextProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_UPDATE_MUSICAL_CONTEXT_PROPERTIES, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::destroyMusicalContext(aap_ara_musical_context_id_t musicalContextId) {
    aap_ara_object_id_wire_t wire{musicalContextId};
    invokeVoidEdit(OPCODE_ARA_DESTROY_MUSICAL_CONTEXT, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::createRegionSequence(aap_ara_document_id_t documentId, aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
    aap_ara_region_sequence_wire_t wire{};
    wire.document_id = documentId;
    wire.region_sequence_id = regionSequenceId;
    packRegionSequenceProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_CREATE_REGION_SEQUENCE, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::updateRegionSequenceProperties(aap_ara_region_sequence_id_t regionSequenceId, const aap_ara_region_sequence_properties_t* properties) {
    aap_ara_region_sequence_wire_t wire{};
    wire.region_sequence_id = regionSequenceId;
    packRegionSequenceProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_UPDATE_REGION_SEQUENCE_PROPERTIES, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::destroyRegionSequence(aap_ara_region_sequence_id_t regionSequenceId) {
    aap_ara_object_id_wire_t wire{regionSequenceId};
    invokeVoidEdit(OPCODE_ARA_DESTROY_REGION_SEQUENCE, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::createAudioSource(aap_ara_document_id_t documentId, aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
    aap_ara_audio_source_wire_t wire{};
    wire.document_id = documentId;
    wire.audio_source_id = audioSourceId;
    packAudioSourceProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_CREATE_AUDIO_SOURCE, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::updateAudioSourceProperties(aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_properties_t* properties) {
    aap_ara_audio_source_wire_t wire{};
    wire.audio_source_id = audioSourceId;
    packAudioSourceProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_UPDATE_AUDIO_SOURCE_PROPERTIES, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::enableAudioSourceSamplesAccess(aap_ara_audio_source_id_t audioSourceId, bool enable) {
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "enableAudioSourceSamplesAccess begin audioSourceId=%d enable=%d serialization=%p",
                 audioSourceId, enable ? 1 : 0, serialization ? serialization->data : nullptr);
    aap_ara_enable_audio_source_samples_access_wire_t wire{};
    wire.audio_source_id = audioSourceId;
    wire.enable = enable;
    invokeVoidEdit(OPCODE_ARA_ENABLE_AUDIO_SOURCE_SAMPLES_ACCESS, &wire, sizeof(wire));
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "enableAudioSourceSamplesAccess end audioSourceId=%d enable=%d",
                 audioSourceId, enable ? 1 : 0);
}

void aap::xs::AraClientAAPXS::notifyAudioSourceContentChanged(aap_ara_audio_source_id_t audioSourceId, aap_ara_sample_position_t changedStartSample, aap_ara_sample_count_t changedSampleCount) {
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "notifyAudioSourceContentChanged begin audioSourceId=%d start=%d count=%d serialization=%p",
                 audioSourceId, changedStartSample, changedSampleCount, serialization ? serialization->data : nullptr);
    aap_ara_notify_audio_source_content_changed_wire_t wire{};
    wire.audio_source_id = audioSourceId;
    wire.changed_start_sample = changedStartSample;
    wire.changed_sample_count = changedSampleCount;
    invokeVoidEdit(OPCODE_ARA_NOTIFY_AUDIO_SOURCE_CONTENT_CHANGED, &wire, sizeof(wire));
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "notifyAudioSourceContentChanged end audioSourceId=%d start=%d count=%d",
                 audioSourceId, changedStartSample, changedSampleCount);
}

void aap::xs::AraClientAAPXS::destroyAudioSource(aap_ara_audio_source_id_t audioSourceId) {
    aap_ara_object_id_wire_t wire{audioSourceId};
    invokeVoidEdit(OPCODE_ARA_DESTROY_AUDIO_SOURCE, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::createAudioModification(aap_ara_audio_source_id_t audioSourceId, aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
    aap_ara_audio_modification_wire_t wire{};
    wire.audio_source_id = audioSourceId;
    wire.audio_modification_id = audioModificationId;
    packAudioModificationProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_CREATE_AUDIO_MODIFICATION, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::updateAudioModificationProperties(aap_ara_audio_modification_id_t audioModificationId, const aap_ara_audio_modification_properties_t* properties) {
    aap_ara_audio_modification_wire_t wire{};
    wire.audio_modification_id = audioModificationId;
    packAudioModificationProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_UPDATE_AUDIO_MODIFICATION_PROPERTIES, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::destroyAudioModification(aap_ara_audio_modification_id_t audioModificationId) {
    aap_ara_object_id_wire_t wire{audioModificationId};
    invokeVoidEdit(OPCODE_ARA_DESTROY_AUDIO_MODIFICATION, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::createPlaybackRegion(aap_ara_audio_modification_id_t audioModificationId, aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties) {
    aap_ara_playback_region_wire_t wire{};
    wire.audio_modification_id = audioModificationId;
    wire.playback_region_id = playbackRegionId;
    packPlaybackRegionProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_CREATE_PLAYBACK_REGION, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::updatePlaybackRegionProperties(aap_ara_playback_region_id_t playbackRegionId, const aap_ara_playback_region_properties_t* properties) {
    aap_ara_playback_region_wire_t wire{};
    wire.playback_region_id = playbackRegionId;
    packPlaybackRegionProperties(wire.properties, properties);
    invokeVoidEdit(OPCODE_ARA_UPDATE_PLAYBACK_REGION_PROPERTIES, &wire, sizeof(wire));
}

void aap::xs::AraClientAAPXS::destroyPlaybackRegion(aap_ara_playback_region_id_t playbackRegionId) {
    aap_ara_object_id_wire_t wire{playbackRegionId};
    invokeVoidEdit(OPCODE_ARA_DESTROY_PLAYBACK_REGION, &wire, sizeof(wire));
}

void aap::xs::AraServiceAAPXS::getHostCapability(aap_ara_host_capability_t& destination) {
    // Blocking-sync on the async core: gains a request timeout and a host-reported error channel.
    auto result = callAndWait<aap_ara_host_capability_t>(OPCODE_ARA_GET_HOST_CAPABILITY, nullptr, 0,
            [](AAPXSSerializationContext* ctx) {
                aap_ara_host_capability_t value{};
                memcpy(&value, ctx->data, std::min(ctx->data_size, sizeof(value)));
                return value;
            }, sizeof(aap_ara_host_capability_t));
    if (result.isOk())
        memcpy(&destination, &result.value, std::min<size_t>(destination.struct_size, sizeof(destination)));
    else
        aap::a_log_f(AAP_LOG_LEVEL_WARN, LOG_TAG, "ARA getHostCapability failed: %s", result.error.c_str());
}

void aap::xs::AraServiceAAPXS::readAudioSourceSamples(aap_ara_audio_source_id_t audioSourceId, const aap_ara_audio_source_sample_range_t* sampleRange, aap_ara_audio_source_samples_buffer_t* destination) {
    aap_ara_read_audio_source_samples_wire_t wire{};
    wire.audio_source_id = audioSourceId;
    wire.sample_range = sampleRange ? *sampleRange : aap_ara_audio_source_sample_range_t{};
    auto result = callAndWait<bool>(OPCODE_ARA_READ_AUDIO_SOURCE_SAMPLES, &wire, sizeof(wire),
            [this, &wire](AAPXSSerializationContext* ctx) {
        wire = *asWire<aap_ara_read_audio_source_samples_wire_t>(ctx);
        auto samples = reinterpret_cast<uint8_t*>(ctx->data) + sizeof(wire);
        auto available = ctx->data_size > sizeof(wire) ? ctx->data_size - sizeof(wire) : 0;
        read_samples.assign(samples, samples + std::min<size_t>(wire.result_data_size, available));
        return true;
    });
    if (!result.isOk())
        aap::a_log_f(AAP_LOG_LEVEL_WARN, LOG_TAG, "ARA readAudioSourceSamples failed: %s", result.error.c_str());
    if (result.isOk() && destination) {
        destination->struct_size = sizeof(aap_ara_audio_source_samples_buffer_t);
        destination->data = read_samples.data();
        destination->data_size = read_samples.size();
        destination->sample_count = wire.result_sample_count;
        destination->channel_count = wire.result_channel_count;
        destination->sample_format = wire.result_sample_format;
    }
}

bool aap::xs::AraClientAAPXS::storeModificationState(int64_t id, aap_ara_archive_buffer_t& destination) {
    aap_ara_archive_wire_t wire{};
    wire.modification_id = id;
    auto result = callAndWait<aap_ara_archive_wire_t>(OPCODE_ARA_STORE_MODIFICATION_STATE, &wire, sizeof(wire),
            [](AAPXSSerializationContext* ctx) { return *asWire<aap_ara_archive_wire_t>(ctx); }, sizeof(wire));
    destination.data_size = 0;
    if (!result.isOk() || !result.value.succeeded || result.value.data_size > AAP_ARA_MAX_ARCHIVE_BYTES) return false;
    destination.data_size = result.value.data_size;
    if (destination.data_size > destination.capacity || (!destination.data && destination.data_size)) return false;
    memcpy(destination.data, result.value.data, destination.data_size);
    return true;
}

bool aap::xs::AraClientAAPXS::restoreModificationState(int64_t id, const void* data, size_t size) {
    if (size > AAP_ARA_MAX_ARCHIVE_BYTES || (!data && size)) return false;
    aap_ara_archive_wire_t wire{};
    wire.modification_id = id; wire.data_size = size;
    if (size) memcpy(wire.data, data, size);
    auto result = callAndWait<aap_ara_archive_wire_t>(OPCODE_ARA_RESTORE_MODIFICATION_STATE, &wire, sizeof(wire),
            [](AAPXSSerializationContext* ctx) { return *asWire<aap_ara_archive_wire_t>(ctx); }, sizeof(wire));
    return result.isOk() && result.value.succeeded;
}

void aap::xs::AraServiceAAPXS::notifyContentChanged(const aap_ara_content_update_t& update) {
    auto result = callAndWait<bool>(OPCODE_ARA_NOTIFY_CONTENT_CHANGED, &update, sizeof(update),
            [](AAPXSSerializationContext*) { return true; }, 0);
    if (!result.isOk()) aap::a_log_f(AAP_LOG_LEVEL_WARN, LOG_TAG, "ARA content update failed: %s", result.error.c_str());
}
