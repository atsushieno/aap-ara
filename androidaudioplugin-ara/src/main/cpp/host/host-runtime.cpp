#include "aap/ara/host-runtime.h"

#include <algorithm>
#include <cstring>
#include "aap/unstable/logging.h"

#define LOG_TAG "AAP.ARA.HostRuntime"

using namespace aap::ara;

namespace {
void* resolve_ara_host_extension(HostRuntime& runtime, const char* uri) {
    if (uri && strcmp(uri, AAP_ARA_EXTENSION_URI) == 0)
        return runtime.getHostExtension();
    return nullptr;
}
}

HostRuntime::HostRuntime() {
    host_extension.aapxs_context = this;
    host_extension.get_host_capability = staticGetHostCapability;
    host_extension.read_audio_source_samples = staticReadAudioSourceSamples;
}

void HostRuntime::setCapabilities(uint32_t apiGenerations, uint32_t roleFlags, uint32_t supportedSampleFormats) {
    api_generations = apiGenerations;
    role_flags = roleFlags;
    supported_sample_formats = supportedSampleFormats;
}

void HostRuntime::registerAudioSource(
        aap_ara_audio_source_id_t audioSourceId,
        std::shared_ptr<AudioSourceContentProvider> provider) {
    sources[audioSourceId] = SourceEntry{std::move(provider)};
}

void HostRuntime::unregisterAudioSource(aap_ara_audio_source_id_t audioSourceId) {
    sources.erase(audioSourceId);
}

void HostRuntime::getHostCapability(aap_ara_host_capability_t& destination) const {
    destination.struct_size = sizeof(aap_ara_host_capability_t);
    destination.api_generations = api_generations;
    destination.role_flags = role_flags;
    destination.supported_sample_formats = supported_sample_formats;
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "getHostCapability api=%u roles=%u formats=%u",
                 destination.api_generations, destination.role_flags, destination.supported_sample_formats);
}

bool HostRuntime::readAudioSourceSamples(
        aap_ara_audio_source_id_t audioSourceId,
        const aap_ara_audio_source_sample_range_t& sampleRange,
        aap_ara_audio_source_samples_buffer_t& destination) const {
    auto it = sources.find(audioSourceId);
    if (it == sources.end() || !it->second.provider)
        return false;
    auto& provider = *it->second.provider;
    if (!(provider.supportedSampleFormats() & sampleRange.sample_format))
        return false;
    if (sampleRange.channel_count <= 0 || sampleRange.channel_count > provider.channelCount())
        return false;
    if (sampleRange.start_sample < 0 || sampleRange.sample_count < 0)
        return false;
    if (sampleRange.start_sample >= provider.sampleCount())
        return false;
    return provider.readSamples(sampleRange, destination);
}

void HostRuntime::staticGetHostCapability(
        aap_ara_host_extension_t* ext,
        AndroidAudioPluginHost*,
        aap_ara_host_capability_t* destination) {
    if (!ext || !destination)
        return;
    auto* runtime = static_cast<HostRuntime*>(ext->aapxs_context);
    runtime->getHostCapability(*destination);
}

void HostRuntime::staticReadAudioSourceSamples(
        aap_ara_host_extension_t* ext,
        AndroidAudioPluginHost*,
        aap_ara_audio_source_id_t audioSourceId,
        const aap_ara_audio_source_sample_range_t* sampleRange,
        aap_ara_audio_source_samples_buffer_t* destination) {
    if (!ext || !sampleRange || !destination)
        return;
    auto* runtime = static_cast<HostRuntime*>(ext->aapxs_context);
    aap::a_log_f(AAP_LOG_LEVEL_INFO, LOG_TAG,
                 "readAudioSourceSamples audioSourceId=%d start=%d count=%d channels=%d format=%u",
                 audioSourceId,
                 sampleRange->start_sample,
                 sampleRange->sample_count,
                 sampleRange->channel_count,
                 sampleRange->sample_format);
    runtime->readAudioSourceSamples(audioSourceId, *sampleRange, *destination);
}

ScopedRemotePluginInstanceARAHostContext::ScopedRemotePluginInstanceARAHostContext(
        HostRuntime& runtime,
        aap::RemotePluginInstance& instance_)
        : instance(&instance_),
          previous_callback(instance_.getHostExtension) {
    instance_.getHostExtension =
            [&runtime](aap::RemotePluginInstance*, uint8_t, const char* uri) -> void* {
                return resolve_ara_host_extension(runtime, uri);
            };
}

ScopedRemotePluginInstanceARAHostContext::~ScopedRemotePluginInstanceARAHostContext() {
    if (instance)
        instance->getHostExtension = std::move(previous_callback);
}
