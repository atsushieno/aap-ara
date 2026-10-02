#ifndef AAP_ARA_HOST_RUNTIME_H
#define AAP_ARA_HOST_RUNTIME_H

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include "aap/core/host/plugin-instance.h"
#include "aap/ext/ara.h"

namespace aap::ara {

class AudioSourceContentProvider {
public:
    virtual ~AudioSourceContentProvider() = default;

    virtual int32_t channelCount() const = 0;
    virtual aap_ara_sample_rate_t sampleRate() const = 0;
    virtual aap_ara_sample_count_t sampleCount() const = 0;
    virtual uint32_t supportedSampleFormats() const = 0;

    virtual bool readSamples(
            const aap_ara_audio_source_sample_range_t& sampleRange,
            aap_ara_audio_source_samples_buffer_t& destination) = 0;
};

class HostRuntime {
    struct SourceEntry {
        std::shared_ptr<AudioSourceContentProvider> provider;
    };

    aap_ara_host_extension_t host_extension{};
    uint32_t api_generations{AAP_ARA_API_GENERATION_2};
    uint32_t role_flags{AAP_ARA_ROLE_EDITOR_RENDERER};
    uint32_t supported_sample_formats{AAP_ARA_SAMPLE_FORMAT_FLOAT32};
    std::map<aap_ara_audio_source_id_t, SourceEntry> sources{};
    mutable std::mutex updates_mutex;
    std::function<void(const aap_ara_content_update_t&)> content_update_handler;

    static void staticGetHostCapability(
            aap_ara_host_extension_t* ext,
            AndroidAudioPluginHost* host,
            aap_ara_host_capability_t* destination);
    static void staticReadAudioSourceSamples(
            aap_ara_host_extension_t* ext,
            AndroidAudioPluginHost* host,
            aap_ara_audio_source_id_t audioSourceId,
            const aap_ara_audio_source_sample_range_t* sampleRange,
            aap_ara_audio_source_samples_buffer_t* destination);
    static void staticNotifyContentChanged(aap_ara_host_extension_t*, AndroidAudioPluginHost*, const aap_ara_content_update_t*);

public:
    HostRuntime();
    ~HostRuntime() = default;

    void setCapabilities(uint32_t apiGenerations, uint32_t roleFlags, uint32_t supportedSampleFormats);
    // The callback runs on a Binder thread and must enqueue, without reentering the plugin.
    void setContentUpdateHandler(std::function<void(const aap_ara_content_update_t&)> handler);

    void registerAudioSource(
            aap_ara_audio_source_id_t audioSourceId,
            std::shared_ptr<AudioSourceContentProvider> provider);
    void unregisterAudioSource(aap_ara_audio_source_id_t audioSourceId);

    aap_ara_host_extension_t* getHostExtension() { return &host_extension; }

    void getHostCapability(aap_ara_host_capability_t& destination) const;
    bool readAudioSourceSamples(
            aap_ara_audio_source_id_t audioSourceId,
            const aap_ara_audio_source_sample_range_t& sampleRange,
            aap_ara_audio_source_samples_buffer_t& destination) const;
};

class ScopedRemotePluginInstanceARAHostContext {
    aap::RemotePluginInstance* instance{};
    std::function<void*(aap::RemotePluginInstance* instance, uint8_t urid, const char* uri)> previous_callback{};

public:
    ScopedRemotePluginInstanceARAHostContext(
            HostRuntime& runtime,
            aap::RemotePluginInstance& instance);
    ~ScopedRemotePluginInstanceARAHostContext();

    ScopedRemotePluginInstanceARAHostContext(const ScopedRemotePluginInstanceARAHostContext&) = delete;
    ScopedRemotePluginInstanceARAHostContext& operator=(const ScopedRemotePluginInstanceARAHostContext&) = delete;
};

} // namespace aap::ara

#endif // AAP_ARA_HOST_RUNTIME_H
