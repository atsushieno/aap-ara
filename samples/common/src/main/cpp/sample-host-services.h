#ifndef SAMPLE_ARA_HOST_SERVICES_H
#define SAMPLE_ARA_HOST_SERVICES_H
#include <aap/ara/araaap.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <functional>
using namespace ARA;
using aap::ara::hostRef;
using aap::ara::hostID;

class SampleAudioSource {
public:
    virtual ~SampleAudioSource() = default;
    virtual ARAChannelCount channelCount() const = 0;
    virtual ARASampleRate sampleRate() const = 0;
    virtual ARASampleCount sampleCount() const = 0;
    virtual bool readSamples(ARASamplePosition start, ARASampleCount count, void* const buffers[], bool wide) = 0;
};
struct SampleContentUpdate { int kind; int64_t object_id; };
struct SampleHostServices {
    std::map<int64_t, std::shared_ptr<SampleAudioSource>> sources;
    std::function<void(SampleContentUpdate)> onUpdate;
    std::mutex mutex;
    struct Reader { std::shared_ptr<SampleAudioSource> source; bool wide; };
    ARAAudioAccessControllerInterface audio{sizeof(audio),
        [](ARAAudioAccessControllerHostRef r, ARAAudioSourceHostRef source, ARABool wide) -> ARAAudioReaderHostRef {
            auto* s = reinterpret_cast<SampleHostServices*>(r); const std::lock_guard<std::mutex> lock(s->mutex);
            auto it = s->sources.find(hostID(source)); if (it == s->sources.end()) return nullptr;
            return reinterpret_cast<ARAAudioReaderHostRef>(new Reader{it->second, wide != 0});
        },
        [](ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef ref, ARASamplePosition start, ARASampleCount count, void* const buffers[]) -> ARABool {
            auto* r = reinterpret_cast<Reader*>(ref); return r && r->source->readSamples(start, count, buffers, r->wide) ? kARATrue : kARAFalse;
        },
        [](ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef ref) { delete reinterpret_cast<Reader*>(ref); }};
    void notify(int kind, int64_t id) { if (onUpdate) onUpdate({kind, id}); }
    ARAModelUpdateControllerInterface updates{sizeof(updates),
        [](ARAModelUpdateControllerHostRef, ARAAudioSourceHostRef, ARAAnalysisProgressState, float) {},
        [](ARAModelUpdateControllerHostRef r, ARAAudioSourceHostRef source, const ARAContentTimeRange*, ARAContentUpdateFlags) { reinterpret_cast<SampleHostServices*>(r)->notify(1, hostID(source)); },
        [](ARAModelUpdateControllerHostRef r, ARAAudioModificationHostRef m, const ARAContentTimeRange*, ARAContentUpdateFlags) { reinterpret_cast<SampleHostServices*>(r)->notify(2, hostID(m)); },
        [](ARAModelUpdateControllerHostRef r, ARAPlaybackRegionHostRef region, const ARAContentTimeRange*, ARAContentUpdateFlags) { reinterpret_cast<SampleHostServices*>(r)->notify(3, hostID(region)); },
        [](ARAModelUpdateControllerHostRef r) { reinterpret_cast<SampleHostServices*>(r)->notify(4, 0); }};
    ARADocumentControllerHostInstance instance{sizeof(instance), reinterpret_cast<ARAAudioAccessControllerHostRef>(this), &audio,
        nullptr, nullptr, nullptr, nullptr, reinterpret_cast<ARAModelUpdateControllerHostRef>(this), &updates, nullptr, nullptr};
    void registerAudioSource(int64_t id, std::shared_ptr<SampleAudioSource> provider) { const std::lock_guard<std::mutex> lock(mutex); sources[id] = std::move(provider); }
    void unregisterAudioSource(int64_t id) { const std::lock_guard<std::mutex> lock(mutex); sources.erase(id); }
};
#endif
