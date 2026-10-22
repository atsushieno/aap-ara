#pragma once

#include <ARA_Library/Dispatch/ARAHostDispatch.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <variant>

using namespace ARA;

// Generated audio is owned by this sample host, outside the plugin's ARA model.
class SampleAudioSource {
public:
    virtual ~SampleAudioSource() = default;
    virtual ARAChannelCount channelCount() const = 0;
    virtual ARASampleRate sampleRate() const = 0;
    virtual ARASampleCount sampleCount() const = 0;
    virtual bool readSamples(ARASamplePosition start, ARASampleCount count, void* const buffers[], bool wide) = 0;
};

// Queue the SDK callback's actual reference until the editor worker can archive it.
// monostate represents notifyDocumentDataChanged().
using SampleContentUpdate = std::variant<std::monostate, ARAAudioSourceHostRef,
        ARAAudioModificationHostRef, ARAPlaybackRegionHostRef>;

class SampleHostServices : public ARA::Host::AudioAccessControllerInterface,
                           public ARA::Host::ModelUpdateControllerInterface {
    std::map<ARAAudioSourceHostRef, std::shared_ptr<SampleAudioSource>> sources;
    std::mutex mutex;
    struct Reader { std::shared_ptr<SampleAudioSource> source; bool wide; };
public:
    std::function<void(SampleContentUpdate)> onUpdate;
    ARA::Host::DocumentControllerHostInstance instance{this, nullptr, nullptr, this};

    ARAAudioReaderHostRef createAudioReaderForSource(ARAAudioSourceHostRef source, bool wide) noexcept override {
        const std::lock_guard<std::mutex> lock(mutex);
        auto it = sources.find(source);
        return it == sources.end() ? nullptr : reinterpret_cast<ARAAudioReaderHostRef>(new Reader{it->second, wide});
    }
    bool readAudioSamples(ARAAudioReaderHostRef ref, ARASamplePosition start,
            ARASampleCount count, void* const buffers[]) noexcept override {
        auto* reader = reinterpret_cast<Reader*>(ref);
        return reader && reader->source->readSamples(start, count, buffers, reader->wide);
    }
    void destroyAudioReader(ARAAudioReaderHostRef ref) noexcept override { delete reinterpret_cast<Reader*>(ref); }
    void notifyAudioSourceAnalysisProgress(ARAAudioSourceHostRef, ARAAnalysisProgressState, float) noexcept override {}
    void notifyAudioSourceContentChanged(ARAAudioSourceHostRef ref, const ARAContentTimeRange*, ContentUpdateScopes) noexcept override {
        if (onUpdate) onUpdate(ref);
    }
    void notifyAudioModificationContentChanged(ARAAudioModificationHostRef ref, const ARAContentTimeRange*, ContentUpdateScopes) noexcept override {
        if (onUpdate) onUpdate(ref);
    }
    void notifyPlaybackRegionContentChanged(ARAPlaybackRegionHostRef ref, const ARAContentTimeRange*, ContentUpdateScopes) noexcept override {
        if (onUpdate) onUpdate(ref);
    }
    void notifyDocumentDataChanged() noexcept override { if (onUpdate) onUpdate(std::monostate{}); }
    void registerAudioSource(ARAAudioSourceHostRef ref, std::shared_ptr<SampleAudioSource> provider) {
        const std::lock_guard<std::mutex> lock(mutex);
        sources[ref] = std::move(provider);
    }
    void unregisterAudioSource(ARAAudioSourceHostRef ref) {
        const std::lock_guard<std::mutex> lock(mutex);
        sources.erase(ref);
    }
};
