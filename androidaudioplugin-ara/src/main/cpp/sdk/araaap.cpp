#include "aap/ara/araaap.hpp"
#include "aap/ara/internal/wire.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <mutex>
#include <stdexcept>
#include <vector>
#include <condition_variable>
#include <future>
#include <queue>
#include <thread>

namespace aap::ara {
using namespace ARA;
namespace {
// Binder callbacks are not thread-affine. Marshal every SDK model call and
// plug-in UI edit to the document controller's dedicated creation thread.
class ModelThread {
    std::mutex mutex;
    std::condition_variable ready;
    std::queue<std::function<void()>> pending;
    bool stopping{};
    std::thread worker;
public:
    ModelThread() : worker([this] {
        std::unique_lock<std::mutex> lock(mutex);
        for (;;) {
            ready.wait(lock, [this] { return stopping || !pending.empty(); });
            if (stopping && pending.empty()) return;
            auto action = std::move(pending.front()); pending.pop();
            lock.unlock(); action(); lock.lock();
        }
    }) {}
    ~ModelThread() { { std::lock_guard<std::mutex> lock(mutex); stopping = true; } ready.notify_one(); worker.join(); }
    template<class F> auto run(F&& action) -> decltype(action()) {
        if (std::this_thread::get_id() == worker.get_id()) return action();
        using Result = decltype(action());
        auto task = std::make_shared<std::packaged_task<Result()>>(std::forward<F>(action)); auto result = task->get_future();
        { std::lock_guard<std::mutex> lock(mutex); pending.push([task] { (*task)(); }); }
        ready.notify_one(); return result.get();
    }
};
ModelThread& modelThread() { static ModelThread thread; return thread; }

enum Command : uint32_t { Factory = 1, Initialize, Bind, Poll, Store, ReadArchive, WriteArchive, Restore, RoleAdd, RoleRemove, HideSequences, ContentAvailable, ContentGrade, CreateContentReader, ContentCount, ContentEvent, DestroyContentReader, AnalysisIncomplete, RequestAnalysis, UpdateContent, Deactivate, CloneModification, HeadTail, AlgorithmsCount, AlgorithmProperties, SourceAlgorithm, RequestAlgorithm, Licensed, PreservesSignal, RoleSequenceAdd, RoleSequenceRemove, SourceArrangement, ContextOrder, DocumentStatus, HostPlayback, HostProgress, FactoryDetail, StoreFileChunk, DestroyDocument, Shutdown };
struct Message {
    uint32_t size = sizeof(Message), command{}, success{}, generation{};
    int64_t id{}, offset{};
    uint32_t known{}, assigned{}, length{}, flags{};
    char factoryID[256]{}, name[256]{}, manufacturer[256]{}, url[256]{}, version[256]{}, archiveID[256]{};
    uint32_t lowest{}, highest{}, transformations{};
    double start{}, duration{};
    uint32_t kind{}, type{}, hasRange{}, index{};
    uint8_t bytes[4096]{};
};
// Never memcpy SDK structs across ABIs: the SDK uses different packing on
// x86 and ARM. Events use scalar wire values, with copied UTF-8 string data.
struct EventWire { double values[32]{}; char name[2048]{}; uint32_t hasName{}; };
struct Event {
    ARAContentNote note{}; ARAContentTempoEntry tempo{}; ARAContentBarSignature meter{};
    ARAContentTuning tuning{}; ARAContentKeySignature key{}; ARAContentChord chord{};
    std::string name;
    const void* decode(uint32_t type, const uint8_t* bytes) {
        EventWire w; std::memcpy(&w, bytes, sizeof(w)); w.name[sizeof(w.name)-1] = 0;
        name = w.name; const char* label = w.hasName ? name.c_str() : nullptr;
        switch (type) {
            case kARAContentTypeNotes: note = {float(w.values[0]), ARAPitchNumber(w.values[1]), float(w.values[2]), w.values[3], w.values[4], w.values[5], w.values[6]}; return &note;
            case kARAContentTypeTempoEntries: tempo = {w.values[0], w.values[1]}; return &tempo;
            case kARAContentTypeBarSignatures: meter = {ARAInt32(w.values[0]), ARAInt32(w.values[1]), w.values[2]}; return &meter;
            case kARAContentTypeStaticTuning:
                tuning.concertPitchFrequency = float(w.values[0]); tuning.root = ARACircleOfFifthsIndex(w.values[1]);
                for (int i = 0; i < 12; ++i) tuning.tunings[i] = float(w.values[i+2]); tuning.name = label; return &tuning;
            case kARAContentTypeKeySignatures:
                key.root = ARACircleOfFifthsIndex(w.values[0]); key.position = w.values[1]; key.name = label;
                for (int i = 0; i < 12; ++i) key.intervals[i] = ARAKeySignatureIntervalUsage(w.values[i+2]); return &key;
            case kARAContentTypeSheetChords:
                chord.root = ARACircleOfFifthsIndex(w.values[0]); chord.bass = ARACircleOfFifthsIndex(w.values[1]); chord.position = w.values[2]; chord.name = label;
                for (int i = 0; i < 12; ++i) chord.intervals[i] = ARAChordIntervalUsage(w.values[i+3]); return &chord;
            default: return nullptr;
        }
    }
    static bool encode(uint32_t type, const void* data, uint8_t* bytes) {
        if (!data) return false; EventWire w; const char* name{};
        switch (type) {
            case kARAContentTypeNotes: { auto& v = *static_cast<const ARAContentNote*>(data); double a[]{v.frequency, double(v.pitchNumber), v.volume, v.startPosition, v.attackDuration, v.noteDuration, v.signalDuration}; std::copy(std::begin(a), std::end(a), w.values); break; }
            case kARAContentTypeTempoEntries: { auto& v = *static_cast<const ARAContentTempoEntry*>(data); w.values[0] = v.timePosition; w.values[1] = v.quarterPosition; break; }
            case kARAContentTypeBarSignatures: { auto& v = *static_cast<const ARAContentBarSignature*>(data); w.values[0] = v.numerator; w.values[1] = v.denominator; w.values[2] = v.position; break; }
            case kARAContentTypeStaticTuning: { auto& v = *static_cast<const ARAContentTuning*>(data); w.values[0] = v.concertPitchFrequency; w.values[1] = v.root; for (int i = 0; i < 12; ++i) w.values[i+2] = v.tunings[i]; name = v.name; break; }
            case kARAContentTypeKeySignatures: { auto& v = *static_cast<const ARAContentKeySignature*>(data); w.values[0] = v.root; w.values[1] = v.position; for (int i = 0; i < 12; ++i) w.values[i+2] = v.intervals[i]; name = v.name; break; }
            case kARAContentTypeSheetChords: { auto& v = *static_cast<const ARAContentChord*>(data); w.values[0] = v.root; w.values[1] = v.bass; w.values[2] = v.position; for (int i = 0; i < 12; ++i) w.values[i+3] = v.intervals[i]; name = v.name; break; }
            default: return false;
        }
        if (name) { if (std::strlen(name) >= sizeof(w.name)) return false; std::strcpy(w.name, name); w.hasName = 1; }
        std::memcpy(bytes, &w, sizeof(w)); return true;
    }
};
struct Encoder {
    std::vector<uint8_t> bytes;
    template<class T> void value(T v) { auto* p = reinterpret_cast<const uint8_t*>(&v); bytes.insert(bytes.end(), p, p + sizeof(v)); }
    void string(const char* v) { if (!v) throw std::invalid_argument("Null ARA persistent ID"); size_t n = std::strlen(v); if (n > UINT32_MAX) throw std::length_error("ARA persistent ID too long"); value<uint32_t>(n); bytes.insert(bytes.end(), v, v + n); }
};
struct Decoder {
    const std::vector<uint8_t>& bytes; size_t offset{};
    template<class T> T value() { if (sizeof(T) > bytes.size() - offset) throw std::invalid_argument("Truncated ARA filter"); T v; std::memcpy(&v, bytes.data() + offset, sizeof(v)); offset += sizeof(v); return v; }
    std::string string() { auto n = value<uint32_t>(); if (n > bytes.size() - offset) throw std::invalid_argument("Truncated ARA ID"); std::string v(reinterpret_cast<const char*>(bytes.data() + offset), n); offset += n; return v; }
    void finish() { if (offset != bytes.size()) throw std::invalid_argument("Invalid ARA filter size"); }
};
struct RemoteReader { int64_t id; uint32_t type; Event event; };
void copy(char* dest, const char* src) { if (!src) src = ""; if (std::strlen(src) >= 256) throw std::runtime_error("ARA factory metadata exceeds transport capacity"); std::strcpy(dest, src); }
// Copy only complete fields advertised by a versioned SDK property struct.
ARADocumentProperties properties(const ARADocumentProperties* input) {
    if (!input || input->structSize < kARADocumentPropertiesMinSize) throw std::invalid_argument("Invalid ARADocumentProperties");
    ARADocumentProperties result{}; result.structSize = sizeof(result);
    if (input->structSize >= offsetof(ARADocumentProperties, name) + sizeof(input->name)) result.name = input->name;
    return result;
}
ARAMusicalContextProperties properties(const ARAMusicalContextProperties* input) {
    if (!input || input->structSize < kARAMusicalContextPropertiesMinSize) throw std::invalid_argument("Invalid ARAMusicalContextProperties");
    ARAMusicalContextProperties result{}; result.structSize = sizeof(result);
    if (input->structSize >= offsetof(ARAMusicalContextProperties, name) + sizeof(input->name)) result.name = input->name;
    if (input->structSize >= offsetof(ARAMusicalContextProperties, orderIndex) + sizeof(input->orderIndex)) result.orderIndex = input->orderIndex;
    if (input->structSize >= offsetof(ARAMusicalContextProperties, color) + sizeof(input->color)) result.color = input->color;
    return result;
}
ARARegionSequenceProperties properties(const ARARegionSequenceProperties* input) {
    if (!input || input->structSize < kARARegionSequencePropertiesMinSize) throw std::invalid_argument("Invalid ARARegionSequenceProperties");
    ARARegionSequenceProperties result{}; result.structSize = sizeof(result);
    if (input->structSize >= offsetof(ARARegionSequenceProperties, name) + sizeof(input->name)) result.name = input->name;
    if (input->structSize >= offsetof(ARARegionSequenceProperties, orderIndex) + sizeof(input->orderIndex)) result.orderIndex = input->orderIndex;
    if (input->structSize >= offsetof(ARARegionSequenceProperties, musicalContextRef) + sizeof(input->musicalContextRef)) result.musicalContextRef = input->musicalContextRef;
    if (input->structSize >= offsetof(ARARegionSequenceProperties, color) + sizeof(input->color)) result.color = input->color;
    return result;
}
ARAAudioSourceProperties properties(const ARAAudioSourceProperties* input) {
    if (!input || input->structSize < kARAAudioSourcePropertiesMinSize) throw std::invalid_argument("Invalid ARAAudioSourceProperties");
    ARAAudioSourceProperties result{}; result.structSize = sizeof(result);
    if (input->structSize >= offsetof(ARAAudioSourceProperties, name) + sizeof(input->name)) result.name = input->name;
    if (input->structSize >= offsetof(ARAAudioSourceProperties, persistentID) + sizeof(input->persistentID)) result.persistentID = input->persistentID;
    if (input->structSize >= offsetof(ARAAudioSourceProperties, sampleCount) + sizeof(input->sampleCount)) result.sampleCount = input->sampleCount;
    if (input->structSize >= offsetof(ARAAudioSourceProperties, sampleRate) + sizeof(input->sampleRate)) result.sampleRate = input->sampleRate;
    if (input->structSize >= offsetof(ARAAudioSourceProperties, channelCount) + sizeof(input->channelCount)) result.channelCount = input->channelCount;
    if (input->structSize >= offsetof(ARAAudioSourceProperties, merits64BitSamples) + sizeof(input->merits64BitSamples)) result.merits64BitSamples = input->merits64BitSamples;
    if (input->structSize >= offsetof(ARAAudioSourceProperties, channelArrangementDataType) + sizeof(input->channelArrangementDataType)) result.channelArrangementDataType = input->channelArrangementDataType;
    if (input->structSize >= offsetof(ARAAudioSourceProperties, channelArrangement) + sizeof(input->channelArrangement)) result.channelArrangement = input->channelArrangement;
    return result;
}
ARAAudioModificationProperties properties(const ARAAudioModificationProperties* input) {
    if (!input || input->structSize < kARAAudioModificationPropertiesMinSize) throw std::invalid_argument("Invalid ARAAudioModificationProperties");
    ARAAudioModificationProperties result{}; result.structSize = sizeof(result);
    if (input->structSize >= offsetof(ARAAudioModificationProperties, name) + sizeof(input->name)) result.name = input->name;
    if (input->structSize >= offsetof(ARAAudioModificationProperties, persistentID) + sizeof(input->persistentID)) result.persistentID = input->persistentID;
    return result;
}
ARAPlaybackRegionProperties properties(const ARAPlaybackRegionProperties* input) {
    if (!input || input->structSize < kARAPlaybackRegionPropertiesMinSize) throw std::invalid_argument("Invalid ARAPlaybackRegionProperties");
    ARAPlaybackRegionProperties result{}; result.structSize = sizeof(result);
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, transformationFlags) + sizeof(input->transformationFlags)) result.transformationFlags = input->transformationFlags;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, startInModificationTime) + sizeof(input->startInModificationTime)) result.startInModificationTime = input->startInModificationTime;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, durationInModificationTime) + sizeof(input->durationInModificationTime)) result.durationInModificationTime = input->durationInModificationTime;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, startInPlaybackTime) + sizeof(input->startInPlaybackTime)) result.startInPlaybackTime = input->startInPlaybackTime;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, durationInPlaybackTime) + sizeof(input->durationInPlaybackTime)) result.durationInPlaybackTime = input->durationInPlaybackTime;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, musicalContextRef) + sizeof(input->musicalContextRef)) result.musicalContextRef = input->musicalContextRef;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, regionSequenceRef) + sizeof(input->regionSequenceRef)) result.regionSequenceRef = input->regionSequenceRef;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, name) + sizeof(input->name)) result.name = input->name;
    if (input->structSize >= offsetof(ARAPlaybackRegionProperties, color) + sizeof(input->color)) result.color = input->color;
    return result;
}
void checkName(const char* v) { if (v && std::strlen(v) >= 256) throw std::length_error("ARA text exceeds 255 UTF-8 bytes"); }
size_t arrangementSize(const ARAAudioSourceProperties& p) {
    if (p.channelArrangementDataType == kARAChannelArrangementUndefined) return 0;
    if (!p.channelArrangement) throw std::invalid_argument("Missing ARA channel arrangement");
    switch (p.channelArrangementDataType) {
        case kARAChannelArrangementVST3SpeakerArrangement: return 8;
        case kARAChannelArrangementAAXStemFormat: return 4;
        case kARAChannelArrangementCLAPChannelMap: return p.channelCount;
        case kARAChannelArrangementCLAPAmbisonicInfo: return 8;
        default: throw std::invalid_argument("ARA channel arrangement is unsupported on Android");
    }
}
void ARA_CALL sdkAssertion(ARAAssertCategory category, const void*, const char* diagnosis) {
    std::fprintf(stderr, "ARAAAP SDK assertion %d: %s\n", int(category), diagnosis ? diagnosis : "");
    std::abort();
}
ARAAssertFunction sdkAssert = sdkAssertion;
struct FactoryUse { size_t count{}; ARAAPIGeneration generation{}; };
std::mutex factoryMutex;
std::map<const ARAFactory*, FactoryUse> factoryUses;
bool acquireFactory(const ARAFactory* f, ARAAPIGeneration g) {
    std::lock_guard<std::mutex> lock(factoryMutex);
    auto& use = factoryUses[f];
    if (use.count && use.generation != g) return false;
    if (!use.count) {
        ARAInterfaceConfiguration config{sizeof(config), g, &sdkAssert};
        f->initializeARAWithConfiguration(&config); use.generation = g;
    }
    ++use.count; return true;
}
void releaseFactory(const ARAFactory* f) {
    std::lock_guard<std::mutex> lock(factoryMutex);
    auto it = factoryUses.find(f);
    if (it != factoryUses.end() && --it->second.count == 0) { f->uninitializeARA(); factoryUses.erase(it); }
}
}

struct PlugIn::Impl {
    ModelThread& model = modelThread();
    aap_ara_extension_t localWire{};
    AndroidAudioPlugin* plugin;
    AndroidAudioPluginHost* host;
    const ARAAAPPlugInExtension* extension;
    const ARAFactory* factory;
    const ARADocumentControllerInstance* dc{};
    const ARAPlugInExtensionInstance* binding{};
    bool initialized{}, editing{};
    std::set<int64_t> playbackAssignments, editorAssignments, sequenceAssignments;
    std::map<int64_t, ARAMusicalContextRef> contexts;
    std::map<int64_t, ARARegionSequenceRef> sequences;
    std::map<int64_t, ARAAudioSourceRef> sources;
    std::map<int64_t, ARAAudioModificationRef> modifications;
    std::map<int64_t, ARAPlaybackRegionRef> regions;
    std::map<int64_t, ARAAudioSourceProperties> sourceProperties;
    std::map<int64_t, std::string> modificationIDs;
    std::vector<uint8_t> archive, filterBytes;
    std::map<int64_t, std::pair<ARAContentReaderRef, uint32_t>> contentReaders;
    int64_t nextReader = 1;
    struct Reader { int64_t source; bool wide; ARAChannelCount channels; };
    std::mutex audioMutex;
    aap_ara_extension_t wire{};
    ARAAudioAccessControllerInterface audio{};
    ARAArchivingControllerInterface archiving{};
    ARAContentAccessControllerInterface content{};
    ARAPlaybackControllerInterface transport{};
    ARAModelUpdateControllerInterface updates{};
    ARADocumentControllerHostInstance hostInstance{};
    std::recursive_mutex mutex;
    const ARADocumentControllerInterface* api() const { return dc->documentControllerInterface; }
    ARADocumentControllerRef ref() const { return dc->documentControllerRef; }
    aap_ara_host_extension_t* remoteHost() { return host->get_extension ? static_cast<aap_ara_host_extension_t*>(host->get_extension(host, AAP_ARA_EXTENSION_URI)) : nullptr; }
    static Impl& self(aap_ara_extension_t* e) { return *static_cast<Impl*>(e->aapxs_context); }
    template<class Ref> static Impl& controller(Ref r) { return *reinterpret_cast<Impl*>(r); }
    static ARAColor color(const aap_ara_color_t& c) { return {c.r, c.g, c.b}; }
    void notify(uint32_t kind, int64_t id, const ARAContentTimeRange* range, ARAContentUpdateFlags flags) {
        auto* h = remoteHost(); if (!h) return;
        aap_ara_host_capability_t cap{}; cap.struct_size = sizeof(cap); h->get_host_capability(h, host, &cap);
        if (!(cap.supported_model_updates & AAP_ARA_HOST_SUPPORTS_CONTENT_UPDATES) || !h->notify_content_changed) return;
        aap_ara_content_update_t update{sizeof(update), kind, id, static_cast<uint32_t>(flags), range != nullptr, range ? range->start : 0, range ? range->duration : 0};
        h->notify_content_changed(h, host, &update);
    }
    Impl(AndroidAudioPlugin* p, AndroidAudioPluginHost* h, const ARAAAPPlugInExtension* e) : plugin(p), host(h), extension(e), factory(e->getFactory(p)) {
        wire.aapxs_context = this;
        audio = {sizeof(audio),
            [](ARAAudioAccessControllerHostRef r, ARAAudioSourceHostRef source, ARABool wide) -> ARAAudioReaderHostRef {
                auto& s = controller(r); const std::lock_guard<std::recursive_mutex> lock(s.mutex); auto id = hostID(source);
                if (!s.sources.count(id)) return nullptr;
                return reinterpret_cast<ARAAudioReaderHostRef>(new Reader{id, wide != 0, s.sourceProperties.at(id).channelCount});
            },
            [](ARAAudioAccessControllerHostRef r, ARAAudioReaderHostRef reader, ARASamplePosition start, ARASampleCount count, void* const buffers[]) -> ARABool {
                auto& s = controller(r); const std::lock_guard<std::mutex> lock(s.audioMutex); auto* rr = reinterpret_cast<Reader*>(reader);
                if (!rr || start < 0 || count < 0 || !buffers) return kARAFalse;
                auto* h = s.remoteHost(); if (!h) return kARAFalse;
                auto channels = rr->channels;
                const size_t sampleBytes = rr->wide ? sizeof(double) : sizeof(float);
                // Keep each transfer within the AAPXS shared memory budget.
                const int64_t chunk = (0x100000 - 1024) / (sampleBytes * channels);
                for (int64_t done = 0; done < count;) {
                    auto frames = std::min<int64_t>(chunk, count - done);
                    aap_ara_audio_source_sample_range_t range{sizeof(range), start + done, frames, channels,
                        rr->wide ? AAP_ARA_SAMPLE_FORMAT_FLOAT64 : AAP_ARA_SAMPLE_FORMAT_FLOAT32};
                    aap_ara_audio_source_samples_buffer_t result{}; result.struct_size = sizeof(result);
                    h->read_audio_source_samples(h, s.host, rr->source, &range, &result);
                    if (!result.data || result.sample_count != frames || result.channel_count != channels || result.sample_format != range.sample_format || result.data_size < frames * channels * sampleBytes) return kARAFalse;
                    for (int c = 0; c < channels; ++c) {
                        if (!buffers[c]) return kARAFalse;
                        std::memcpy(static_cast<uint8_t*>(buffers[c]) + done * sampleBytes,
                            static_cast<uint8_t*>(result.data) + c * frames * sampleBytes, frames * sampleBytes);
                    }
                    done += frames;
                }
                return kARATrue;
            },
            [](ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef r) { delete reinterpret_cast<Reader*>(r); }};
        archiving = {sizeof(archiving),
            [](ARAArchivingControllerHostRef r, ARAArchiveReaderHostRef) -> ARASize { return controller(r).archive.size(); },
            [](ARAArchivingControllerHostRef r, ARAArchiveReaderHostRef, ARASize pos, ARASize count, ARAByte* out) -> ARABool {
                auto& b = controller(r).archive; if (pos > b.size() || count > b.size() - pos) return kARAFalse;
                if (count) std::memcpy(out, b.data() + pos, count); return kARATrue;
            },
            [](ARAArchivingControllerHostRef r, ARAArchiveWriterHostRef, ARASize pos, ARASize count, const ARAByte* in) -> ARABool {
                auto& b = controller(r).archive; if (count > b.max_size() - pos) return kARAFalse;
                try { if (b.size() < pos + count) b.resize(pos + count); } catch (...) { return kARAFalse; }
                if (count) std::memcpy(b.data() + pos, in, count); return kARATrue;
            },
            [](ARAArchivingControllerHostRef, float) {}, [](ARAArchivingControllerHostRef, float) {},
            [](ARAArchivingControllerHostRef r, ARAArchiveReaderHostRef) -> ARAPersistentID { return controller(r).factory->documentArchiveID; }};
        updates = {sizeof(updates),
            [](ARAModelUpdateControllerHostRef, ARAAudioSourceHostRef, ARAAnalysisProgressState, float) {},
            [](ARAModelUpdateControllerHostRef r, ARAAudioSourceHostRef source, const ARAContentTimeRange* range, ARAContentUpdateFlags flags) { controller(r).notify(AAP_ARA_CONTENT_AUDIO_SOURCE, hostID(source), range, flags); },
            [](ARAModelUpdateControllerHostRef r, ARAAudioModificationHostRef mod, const ARAContentTimeRange* range, ARAContentUpdateFlags flags) { controller(r).notify(AAP_ARA_CONTENT_AUDIO_MODIFICATION, hostID(mod), range, flags); },
            [](ARAModelUpdateControllerHostRef r, ARAPlaybackRegionHostRef region, const ARAContentTimeRange* range, ARAContentUpdateFlags flags) { controller(r).notify(AAP_ARA_CONTENT_PLAYBACK_REGION, hostID(region), range, flags); },
            [](ARAModelUpdateControllerHostRef r) { controller(r).notify(AAP_ARA_CONTENT_DOCUMENT, 0, nullptr, 0); }};
        hostInstance = {sizeof(hostInstance), reinterpret_cast<ARAAudioAccessControllerHostRef>(this), &audio,
            reinterpret_cast<ARAArchivingControllerHostRef>(this), &archiving, nullptr, nullptr,
            reinterpret_cast<ARAModelUpdateControllerHostRef>(this), &updates, nullptr, nullptr};
        installHostServices();
        installWire();
    }
    void clearDocument() {
        if (!dc) return;
        for (auto& entry : contentReaders) api()->destroyContentReader(ref(), entry.second.first); contentReaders.clear();
        if (binding) {
            for (auto id : playbackAssignments) binding->playbackRendererInterface->removePlaybackRegion(binding->playbackRendererRef, regions.at(id));
            for (auto id : editorAssignments) binding->editorRendererInterface->removePlaybackRegion(binding->editorRendererRef, regions.at(id));
            for (auto id : sequenceAssignments) binding->editorRendererInterface->removeRegionSequence(binding->editorRendererRef, sequences.at(id));
        }
        playbackAssignments.clear(); editorAssignments.clear(); sequenceAssignments.clear();
        if (!editing) api()->beginEditing(ref());
        for (auto& item : regions) api()->destroyPlaybackRegion(ref(), item.second);
        for (auto& item : modifications) api()->destroyAudioModification(ref(), item.second);
        for (auto& item : sources) { api()->enableAudioSourceSamplesAccess(ref(), item.second, kARAFalse); api()->destroyAudioSource(ref(), item.second); }
        for (auto& item : sequences) api()->destroyRegionSequence(ref(), item.second);
        for (auto& item : contexts) api()->destroyMusicalContext(ref(), item.second);
        api()->endEditing(ref()); editing = false; api()->destroyDocumentController(ref()); dc = nullptr;
        regions.clear(); modifications.clear(); sources.clear(); sequences.clear(); contexts.clear(); sourceProperties.clear(); modificationIDs.clear();
    }
    ~Impl() { model.run([this] { clearDocument(); if (initialized) releaseFactory(factory); }); }
    void installHostServices();
    bool hostCommand(Message& m) { auto* h = remoteHost(); return h && h->sdk_command && h->sdk_command(h, host, &m, sizeof(m)) && m.success; }
    void installWire();
    bool command(Message& m);
};

void PlugIn::Impl::installHostServices() {
    content.structSize = sizeof(content);
    content.isMusicalContextContentAvailable = [](ARAContentAccessControllerHostRef r, ARAMusicalContextHostRef ref, ARAContentType type) -> ARABool {
        auto& s = controller(r); Message m; m.command = ContentAvailable; m.kind = 0; m.id = hostID(ref); m.type = type;
        return s.hostCommand(m) ? static_cast<ARABool>(m.index) : static_cast<ARABool>(0);
    };
    content.getMusicalContextContentGrade = [](ARAContentAccessControllerHostRef r, ARAMusicalContextHostRef ref, ARAContentType type) -> ARAContentGrade {
        auto& s = controller(r); Message m; m.command = ContentGrade; m.kind = 0; m.id = hostID(ref); m.type = type;
        return s.hostCommand(m) ? static_cast<ARAContentGrade>(m.index) : static_cast<ARAContentGrade>(0);
    };
    content.createMusicalContextContentReader = [](ARAContentAccessControllerHostRef r, ARAMusicalContextHostRef ref, ARAContentType type, const ARAContentTimeRange* range) -> ARAContentReaderHostRef {
        auto& s = controller(r); Message m; m.command = CreateContentReader; m.kind = 0; m.id = hostID(ref); m.type = type;
        m.hasRange = range != nullptr; if (range) { m.start = range->start; m.duration = range->duration; }
        return s.hostCommand(m) && m.id ? reinterpret_cast<ARAContentReaderHostRef>(new RemoteReader{m.id, static_cast<uint32_t>(type), {}}) : nullptr;
    };
    content.isAudioSourceContentAvailable = [](ARAContentAccessControllerHostRef r, ARAAudioSourceHostRef ref, ARAContentType type) -> ARABool {
        auto& s = controller(r); Message m; m.command = ContentAvailable; m.kind = 1; m.id = hostID(ref); m.type = type;
        return s.hostCommand(m) ? static_cast<ARABool>(m.index) : static_cast<ARABool>(0);
    };
    content.getAudioSourceContentGrade = [](ARAContentAccessControllerHostRef r, ARAAudioSourceHostRef ref, ARAContentType type) -> ARAContentGrade {
        auto& s = controller(r); Message m; m.command = ContentGrade; m.kind = 1; m.id = hostID(ref); m.type = type;
        return s.hostCommand(m) ? static_cast<ARAContentGrade>(m.index) : static_cast<ARAContentGrade>(0);
    };
    content.createAudioSourceContentReader = [](ARAContentAccessControllerHostRef r, ARAAudioSourceHostRef ref, ARAContentType type, const ARAContentTimeRange* range) -> ARAContentReaderHostRef {
        auto& s = controller(r); Message m; m.command = CreateContentReader; m.kind = 1; m.id = hostID(ref); m.type = type;
        m.hasRange = range != nullptr; if (range) { m.start = range->start; m.duration = range->duration; }
        return s.hostCommand(m) && m.id ? reinterpret_cast<ARAContentReaderHostRef>(new RemoteReader{m.id, static_cast<uint32_t>(type), {}}) : nullptr;
    };
    content.getContentReaderEventCount = [](ARAContentAccessControllerHostRef r, ARAContentReaderHostRef reader) -> ARAInt32 {
        auto& s = controller(r); Message m; m.command = ContentCount; m.id = reinterpret_cast<RemoteReader*>(reader)->id;
        return s.hostCommand(m) ? static_cast<ARAInt32>(m.index) : 0;
    };
    content.getContentReaderDataForEvent = [](ARAContentAccessControllerHostRef r, ARAContentReaderHostRef reader, ARAInt32 index) -> const void* {
        auto& s = controller(r); auto* rr = reinterpret_cast<RemoteReader*>(reader); Message m; m.command = ContentEvent; m.id = rr->id; m.index = index;
        return s.hostCommand(m) ? rr->event.decode(rr->type, m.bytes) : nullptr;
    };
    content.destroyContentReader = [](ARAContentAccessControllerHostRef r, ARAContentReaderHostRef reader) {
        auto& s = controller(r); auto* rr = reinterpret_cast<RemoteReader*>(reader); Message m; m.command = DestroyContentReader; m.id = rr->id; s.hostCommand(m); delete rr;
    };
    transport = {sizeof(transport),
        [](ARAPlaybackControllerHostRef r) { auto& s = controller(r); Message m; m.command = HostPlayback; m.kind = 0; s.hostCommand(m); },
        [](ARAPlaybackControllerHostRef r) { auto& s = controller(r); Message m; m.command = HostPlayback; m.kind = 1; s.hostCommand(m); },
        [](ARAPlaybackControllerHostRef r, ARATimePosition position) { auto& s = controller(r); Message m; m.command = HostPlayback; m.kind = 2; m.start = position; s.hostCommand(m); },
        [](ARAPlaybackControllerHostRef r, ARATimePosition start, ARATimeDuration duration) { auto& s = controller(r); Message m; m.command = HostPlayback; m.kind = 3; m.start = start; m.duration = duration; s.hostCommand(m); },
        [](ARAPlaybackControllerHostRef r, ARABool enabled) { auto& s = controller(r); Message m; m.command = HostPlayback; m.kind = 4; m.flags = enabled; s.hostCommand(m); }};
    updates.notifyAudioSourceAnalysisProgress = [](ARAModelUpdateControllerHostRef r, ARAAudioSourceHostRef source, ARAAnalysisProgressState state, float value) {
        auto& s = controller(r); Message m; m.command = HostProgress; m.id = hostID(source); m.index = state; m.start = value; s.hostCommand(m);
    };
}

bool PlugIn::Impl::command(Message& m) {
    const std::lock_guard<std::recursive_mutex> lock(mutex);
    m.success = false;
    switch (m.command) {
        case Factory:
            copy(m.factoryID, factory->factoryID); copy(m.name, factory->plugInName); copy(m.manufacturer, factory->manufacturerName);
            copy(m.url, factory->informationURL); copy(m.version, factory->version); copy(m.archiveID, factory->documentArchiveID);
            m.lowest = factory->lowestSupportedApiGeneration; m.highest = factory->highestSupportedApiGeneration;
            m.transformations = factory->supportedPlaybackTransformationFlags;
            m.length = factory->compatibleDocumentArchiveIDsCount; m.index = factory->analyzeableContentTypesCount;
            m.flags = factory->structSize >= sizeof(*factory) && factory->supportsStoringAudioFileChunks; m.success = true; break;
        case FactoryDetail:
            if (m.kind == 0 && m.index < factory->compatibleDocumentArchiveIDsCount) { copy(m.archiveID, factory->compatibleDocumentArchiveIDs[m.index]); m.success = true; }
            else if (m.kind == 1 && m.index < factory->analyzeableContentTypesCount) { m.type = factory->analyzeableContentTypes[m.index]; m.success = true; }
            break;
        case Initialize:
            if (initialized || static_cast<int32_t>(m.generation) < factory->lowestSupportedApiGeneration || static_cast<int32_t>(m.generation) > factory->highestSupportedApiGeneration) break;
            hostInstance.contentAccessControllerHostRef = reinterpret_cast<ARAContentAccessControllerHostRef>(this);
            hostInstance.contentAccessControllerInterface = (m.flags & 1) ? &content : nullptr;
            hostInstance.playbackControllerHostRef = reinterpret_cast<ARAPlaybackControllerHostRef>(this);
            hostInstance.playbackControllerInterface = (m.flags & 2) ? &transport : nullptr;
            hostInstance.modelUpdateControllerInterface = (m.flags & 4) ? &updates : nullptr;
            initialized = acquireFactory(factory, static_cast<ARAAPIGeneration>(m.generation)); m.success = initialized; break;
        case Bind:
            if (!dc || binding || (m.assigned & ~m.known)) break;
            binding = extension->bindToDocumentController(plugin, ref(), m.known, m.assigned);
            m.success = binding != nullptr; break;
        case Poll:
            if (!dc || editing) break;
            api()->notifyModelUpdates(ref()); m.success = true; break;
        case Store: {
            if (!dc || editing) break;
            archive.clear();
            ARAStoreObjectsFilter filter{}; filter.structSize = sizeof(filter);
            std::vector<ARAAudioSourceRef> sourceRefs; std::vector<ARAAudioModificationRef> modRefs;
            if (m.flags) {
                Decoder d{filterBytes}; filter.documentData = d.value<uint32_t>();
                auto ns = d.value<uint32_t>(); for (uint32_t i = 0; i < ns; ++i) sourceRefs.push_back(sources.at(d.value<int64_t>()));
                auto nm = d.value<uint32_t>(); for (uint32_t i = 0; i < nm; ++i) modRefs.push_back(modifications.at(d.value<int64_t>())); d.finish();
                filter.audioSourceRefsCount = sourceRefs.size(); filter.audioSourceRefs = sourceRefs.data(); filter.audioModificationRefsCount = modRefs.size(); filter.audioModificationRefs = modRefs.data();
            } else if (m.id) { modRefs.push_back(modifications.at(m.id)); filter.audioModificationRefsCount = 1; filter.audioModificationRefs = modRefs.data(); }
            m.success = api()->storeObjectsToArchive(ref(), reinterpret_cast<ARAArchiveWriterHostRef>(this), (m.flags || m.id) ? &filter : nullptr);
            m.offset = archive.size(); break;
        }
        case StoreFileChunk: {
            if (!dc || editing || !factory->supportsStoringAudioFileChunks) break;
            archive.clear(); ARAPersistentID id{}; ARABool open{};
            m.success = api()->storeAudioSourceToAudioFileChunk(ref(), reinterpret_cast<ARAArchiveWriterHostRef>(this), sources.at(m.id), &id, &open);
            if (m.success) { copy(m.archiveID, id); m.flags = open; m.offset = archive.size(); } break;
        }
        case ReadArchive:
            if (m.offset < 0 || static_cast<uint64_t>(m.offset) > archive.size()) break;
            m.length = std::min<size_t>(sizeof(m.bytes), archive.size() - m.offset);
            if (m.length) std::memcpy(m.bytes, archive.data() + m.offset, m.length);
            m.success = true; break;
        case WriteArchive: {
            auto& target = m.flags ? filterBytes : archive;
            if (m.offset < 0 || m.length > sizeof(m.bytes) || (m.offset && static_cast<uint64_t>(m.offset) != target.size())) break;
            if (!m.offset) target.clear(); target.insert(target.end(), m.bytes, m.bytes + m.length); m.success = true; break;
        }
        case Restore: {
            if (!dc || !editing) break;
            ARARestoreObjectsFilter filter{}; filter.structSize = sizeof(filter);
            std::vector<std::string> sourceOld, sourceNew, modOld, modNew;
            std::vector<const char*> sourceOldPtrs, sourceNewPtrs, modOldPtrs, modNewPtrs;
            if (m.flags) {
                Decoder d{filterBytes}; filter.documentData = d.value<uint32_t>();
                auto ns = d.value<uint32_t>(); for (uint32_t i = 0; i < ns; ++i) { sourceOld.push_back(d.string()); sourceNew.push_back(d.string()); }
                auto nm = d.value<uint32_t>(); for (uint32_t i = 0; i < nm; ++i) { modOld.push_back(d.string()); modNew.push_back(d.string()); } d.finish();
            } else if (m.id) { modOld.push_back(m.archiveID[0] ? m.archiveID : modificationIDs.at(m.id)); modNew.push_back(modificationIDs.at(m.id)); }
            for (auto& v : sourceOld) sourceOldPtrs.push_back(v.c_str()); for (auto& v : sourceNew) sourceNewPtrs.push_back(v.c_str());
            for (auto& v : modOld) modOldPtrs.push_back(v.c_str()); for (auto& v : modNew) modNewPtrs.push_back(v.c_str());
            filter.audioSourceIDsCount = sourceOldPtrs.size(); filter.audioSourceArchiveIDs = sourceOldPtrs.data(); filter.audioSourceCurrentIDs = sourceNewPtrs.data();
            filter.audioModificationIDsCount = modOldPtrs.size(); filter.audioModificationArchiveIDs = modOldPtrs.data(); filter.audioModificationCurrentIDs = modNewPtrs.data();
            m.success = api()->restoreObjectsFromArchive(ref(), reinterpret_cast<ARAArchiveReaderHostRef>(this), (m.flags || m.id) ? &filter : nullptr); break;
        }
        case DocumentStatus: m.success = dc != nullptr; break;
        case Shutdown: clearDocument(); m.success = true; break;
        case DestroyDocument:
            if (!dc || editing || !contexts.empty() || !sequences.empty() || !sources.empty() || !modifications.empty() || !regions.empty() || !contentReaders.empty()) break;
            api()->destroyDocumentController(ref()); dc = nullptr; m.success = true; break;
        case ContextOrder: {
            if (!dc || !editing || !contexts.count(m.id)) break;
            ARAColor c{}; std::memcpy(&c, m.bytes, sizeof(c));
            ARAMusicalContextProperties p{sizeof(p), m.name, static_cast<int32_t>(m.index), m.hasRange ? &c : nullptr};
            api()->updateMusicalContextProperties(ref(), contexts.at(m.id), &p); m.success = true; break;
        }
        case UpdateContent: {
            if (!dc || !editing) break; ARAContentTimeRange range{m.start, m.duration};
            if (m.kind == 0) api()->updateMusicalContextContent(ref(), contexts.at(m.id), m.hasRange ? &range : nullptr, m.flags);
            else api()->updateAudioSourceContent(ref(), sources.at(m.id), m.hasRange ? &range : nullptr, m.flags);
            m.success = true; break;
        }
        case Deactivate:
            if (m.kind == 1) api()->deactivateAudioSourceForUndoHistory(ref(), sources.at(m.id), m.flags != 0);
            else api()->deactivateAudioModificationForUndoHistory(ref(), modifications.at(m.id), m.flags != 0);
            m.success = true; break;
        case CloneModification: {
            auto source = modifications.at(m.offset);
            ARAAudioModificationProperties p{sizeof(p), m.name, m.archiveID};
            modifications.emplace(m.id, api()->cloneAudioModification(ref(), source, hostRef<ARAAudioModificationHostRef>(m.id), &p)); modificationIDs[m.id] = m.archiveID;
            m.success = true; break;
        }
        case ContentAvailable: case ContentGrade: case CreateContentReader: {
            ARAContentTimeRange range{m.start, m.duration}; ARAContentReaderRef reader{};
            if (m.kind == 1) {
                auto object = sources.at(m.id);
                if (m.command == ContentAvailable) m.index = api()->isAudioSourceContentAvailable(ref(), object, m.type);
                else if (m.command == ContentGrade) m.index = api()->getAudioSourceContentGrade(ref(), object, m.type);
                else reader = api()->createAudioSourceContentReader(ref(), object, m.type, m.hasRange ? &range : nullptr);
            }
            if (m.kind == 2) {
                auto object = modifications.at(m.id);
                if (m.command == ContentAvailable) m.index = api()->isAudioModificationContentAvailable(ref(), object, m.type);
                else if (m.command == ContentGrade) m.index = api()->getAudioModificationContentGrade(ref(), object, m.type);
                else reader = api()->createAudioModificationContentReader(ref(), object, m.type, m.hasRange ? &range : nullptr);
            }
            if (m.kind == 3) {
                auto object = regions.at(m.id);
                if (m.command == ContentAvailable) m.index = api()->isPlaybackRegionContentAvailable(ref(), object, m.type);
                else if (m.command == ContentGrade) m.index = api()->getPlaybackRegionContentGrade(ref(), object, m.type);
                else reader = api()->createPlaybackRegionContentReader(ref(), object, m.type, m.hasRange ? &range : nullptr);
            }
            if (m.command == CreateContentReader) { m.id = reader ? nextReader++ : 0; if (reader) contentReaders[m.id] = {reader, m.type}; }
            m.success = true; break;
        }
        case ContentCount: m.index = api()->getContentReaderEventCount(ref(), contentReaders.at(m.id).first); m.success = true; break;
        case ContentEvent: { auto reader = contentReaders.at(m.id); m.success = Event::encode(reader.second, api()->getContentReaderDataForEvent(ref(), reader.first, m.index), m.bytes); break; }
        case DestroyContentReader: api()->destroyContentReader(ref(), contentReaders.at(m.id).first); contentReaders.erase(m.id); m.success = true; break;
        case AnalysisIncomplete: m.index = api()->isAudioSourceContentAnalysisIncomplete(ref(), sources.at(m.id), m.type); m.success = true; break;
        case RequestAnalysis: {
            if (m.length > sizeof(m.bytes) / sizeof(int32_t)) break;
            std::vector<ARAContentType> types(m.length); std::memcpy(types.data(), m.bytes, m.length * sizeof(int32_t));
            api()->requestAudioSourceContentAnalysis(ref(), sources.at(m.id), types.size(), types.data()); m.success = true; break;
        }
        case HeadTail: api()->getPlaybackRegionHeadAndTailTime(ref(), regions.at(m.id), &m.start, &m.duration); m.success = true; break;
        case AlgorithmsCount: m.index = api()->getProcessingAlgorithmsCount(ref()); m.success = true; break;
        case AlgorithmProperties: { auto* p = api()->getProcessingAlgorithmProperties(ref(), m.index); if (p) { copy(m.name, p->name); copy(m.archiveID, p->persistentID); m.success = true; } break; }
        case SourceAlgorithm: m.index = api()->getProcessingAlgorithmForAudioSource(ref(), sources.at(m.id)); m.success = true; break;
        case RequestAlgorithm: api()->requestProcessingAlgorithmForAudioSource(ref(), sources.at(m.id), m.index); m.success = true; break;
        case Licensed: {
            if (m.length > sizeof(m.bytes) / sizeof(int32_t)) break;
            std::vector<ARAContentType> types(m.length); std::memcpy(types.data(), m.bytes, m.length * sizeof(int32_t));
            m.index = api()->isLicensedForCapabilities(ref(), m.known != 0, types.size(), types.data(), m.flags); m.success = true; break;
        }
        case PreservesSignal: m.index = api()->isAudioModificationPreservingAudioSourceSignal(ref(), modifications.at(m.id)); m.success = true; break;
        case RoleSequenceAdd: case RoleSequenceRemove:
            if (!binding || !binding->editorRendererInterface) break;
            if (m.command == RoleSequenceAdd) binding->editorRendererInterface->addRegionSequence(binding->editorRendererRef, sequences.at(m.id));
            else binding->editorRendererInterface->removeRegionSequence(binding->editorRendererRef, sequences.at(m.id));
            if (m.command == RoleSequenceAdd) sequenceAssignments.insert(m.id); else sequenceAssignments.erase(m.id);
            m.success = true; break;
        case HideSequences: {
            if (!binding || !binding->editorViewInterface || m.length > sizeof(m.bytes) / sizeof(int64_t)) break;
            std::vector<ARARegionSequenceRef> refs; for (uint32_t i = 0; i < m.length; ++i) { int64_t id; std::memcpy(&id, m.bytes + i * sizeof(id), sizeof(id)); refs.push_back(sequences.at(id)); }
            binding->editorViewInterface->notifyHideRegionSequences(binding->editorViewRef, refs.size(), refs.data()); m.success = true; break;
        }
        case RoleAdd: case RoleRemove: {
            if (!binding || !regions.count(m.id)) break;
            auto region = regions.at(m.id);
            if (m.flags == kARAPlaybackRendererRole && binding->playbackRendererInterface) {
                if (m.command == RoleAdd) binding->playbackRendererInterface->addPlaybackRegion(binding->playbackRendererRef, region);
                else binding->playbackRendererInterface->removePlaybackRegion(binding->playbackRendererRef, region);
            } else if (m.flags == kARAEditorRendererRole && binding->editorRendererInterface) {
                if (m.command == RoleAdd) binding->editorRendererInterface->addPlaybackRegion(binding->editorRendererRef, region);
                else binding->editorRendererInterface->removePlaybackRegion(binding->editorRendererRef, region);
            } else break;
            auto& assigned = m.flags == kARAPlaybackRendererRole ? playbackAssignments : editorAssignments;
            if (m.command == RoleAdd) assigned.insert(m.id); else assigned.erase(m.id);
            m.success = true; break;
        }
        default: break;
    }
    return true;
}

void PlugIn::Impl::installWire() {
    localWire.get_factory_capability = [](aap_ara_extension_t* e, AndroidAudioPlugin*, aap_ara_factory_capability_t* c) {
        auto& s = self(e);
        *c = {sizeof(*c), AAP_ARA_API_GENERATION_2,
            AAP_ARA_ROLE_PLAYBACK_RENDERER | AAP_ARA_ROLE_EDITOR_RENDERER | AAP_ARA_ROLE_EDITOR_VIEW,
            static_cast<uint32_t>(s.factory->supportedPlaybackTransformationFlags),
            AAP_ARA_FEATURE_ARA_SDK | AAP_ARA_FEATURE_VIEW_SELECTION};
    };
    localWire.sdk_command = [](aap_ara_extension_t* e, AndroidAudioPlugin*, void* data, uint32_t size) {
        if (!data || size != sizeof(Message)) return false;
        Message m; std::memcpy(&m, data, sizeof(m)); if (m.size != sizeof(m)) return false;
        try { self(e).command(m); } catch (...) { m.success = false; }
        std::memcpy(data, &m, sizeof(m)); return true;
    };
    localWire.create_document = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t, const aap_ara_document_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.initialized || s.dc || !p) return;
        ARADocumentProperties props{sizeof(props), p->name};
        s.dc = s.factory->createDocumentControllerWithDocument(&s.hostInstance, &props);
    };
    localWire.update_document_properties = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t, const aap_ara_document_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        ARADocumentProperties props{sizeof(props), p->name}; s.api()->updateDocumentProperties(s.ref(), &props);
    };
    localWire.destroy_document = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || s.editing || !s.regions.empty() || !s.modifications.empty() || !s.sources.empty() || !s.sequences.empty() || !s.contexts.empty()) return;
        s.api()->destroyDocumentController(s.ref()); s.dc = nullptr;
    };
    localWire.begin_model_update = [](aap_ara_extension_t* e, AndroidAudioPlugin*, uint32_t) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (s.dc && !s.editing) { s.api()->beginEditing(s.ref()); s.editing = true; }
    };
    localWire.end_model_update = [](aap_ara_extension_t* e, AndroidAudioPlugin*) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (s.dc && s.editing) { s.api()->endEditing(s.ref()); s.editing = false; }
    };
    localWire.create_musical_context = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t, int64_t id, const aap_ara_musical_context_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        ARAColor c{}; if (p->color) c = color(*p->color);
        ARAMusicalContextProperties props{sizeof(props), p->name, p->order_index, p->color ? &c : nullptr};
        s.contexts.emplace(id, s.api()->createMusicalContext(s.ref(), hostRef<ARAMusicalContextHostRef>(id), &props)); 
    };
    localWire.update_musical_context_properties = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id, const aap_ara_musical_context_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        ARAColor c{}; if (p->color) c = color(*p->color);
        ARAMusicalContextProperties props{sizeof(props), p->name, p->order_index, p->color ? &c : nullptr};
        s.api()->updateMusicalContextProperties(s.ref(), s.contexts.at(id), &props); 
    };
    localWire.destroy_musical_context = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !s.contexts.count(id)) return;
        s.api()->destroyMusicalContext(s.ref(), s.contexts.at(id)); s.contexts.erase(id);
    };
    localWire.create_region_sequence = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t, int64_t id, const aap_ara_region_sequence_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        ARAColor c{}; if (p->color) c = color(*p->color);
        ARARegionSequenceProperties props{sizeof(props), p->name, p->order_index, s.contexts.at(p->musical_context_id), p->color ? &c : nullptr};
        s.sequences.emplace(id, s.api()->createRegionSequence(s.ref(), hostRef<ARARegionSequenceHostRef>(id), &props)); 
    };
    localWire.update_region_sequence_properties = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id, const aap_ara_region_sequence_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        ARAColor c{}; if (p->color) c = color(*p->color);
        ARARegionSequenceProperties props{sizeof(props), p->name, p->order_index, s.contexts.at(p->musical_context_id), p->color ? &c : nullptr};
        s.api()->updateRegionSequenceProperties(s.ref(), s.sequences.at(id), &props); 
    };
    localWire.destroy_region_sequence = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !s.sequences.count(id)) return;
        s.api()->destroyRegionSequence(s.ref(), s.sequences.at(id)); s.sequences.erase(id);
    };
    localWire.create_audio_source = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t, int64_t id, const aap_ara_audio_source_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        
        ARAAudioSourceProperties props{sizeof(props), p->name, p->persistent_id, p->sample_count, p->sample_rate, p->channel_count, p->merits_64_bit_samples ? kARATrue : kARAFalse, static_cast<ARAChannelArrangementDataType>(p->channel_arrangement_type), p->channel_arrangement};
        s.sources.emplace(id, s.api()->createAudioSource(s.ref(), hostRef<ARAAudioSourceHostRef>(id), &props)); s.sourceProperties[id] = props; s.sourceProperties[id].name = nullptr; s.sourceProperties[id].persistentID = nullptr;
    };
    localWire.update_audio_source_properties = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id, const aap_ara_audio_source_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        
        ARAAudioSourceProperties props{sizeof(props), p->name, p->persistent_id, p->sample_count, p->sample_rate, p->channel_count, p->merits_64_bit_samples ? kARATrue : kARAFalse, static_cast<ARAChannelArrangementDataType>(p->channel_arrangement_type), p->channel_arrangement};
        s.api()->updateAudioSourceProperties(s.ref(), s.sources.at(id), &props); s.sourceProperties[id] = props; s.sourceProperties[id].name = nullptr; s.sourceProperties[id].persistentID = nullptr;
    };
    localWire.destroy_audio_source = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !s.sources.count(id)) return;
        s.api()->destroyAudioSource(s.ref(), s.sources.at(id)); s.sources.erase(id);
    };
    localWire.create_audio_modification = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t parent, int64_t id, const aap_ara_audio_modification_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        
        ARAAudioModificationProperties props{sizeof(props), p->name, p->persistent_id};
        s.modifications.emplace(id, s.api()->createAudioModification(s.ref(), s.sources.at(parent), hostRef<ARAAudioModificationHostRef>(id), &props)); s.modificationIDs[id] = p->persistent_id;
    };
    localWire.update_audio_modification_properties = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id, const aap_ara_audio_modification_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        
        ARAAudioModificationProperties props{sizeof(props), p->name, p->persistent_id};
        s.api()->updateAudioModificationProperties(s.ref(), s.modifications.at(id), &props); s.modificationIDs[id] = p->persistent_id;
    };
    localWire.destroy_audio_modification = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !s.modifications.count(id)) return;
        s.api()->destroyAudioModification(s.ref(), s.modifications.at(id)); s.modifications.erase(id);
    };
    localWire.create_playback_region = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t parent, int64_t id, const aap_ara_playback_region_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        ARAColor c{}; if (p->color) c = color(*p->color);
        ARAPlaybackRegionProperties props{sizeof(props), static_cast<ARAPlaybackTransformationFlags>(p->transformation_flags), p->start_in_modification_time, p->duration_in_modification_time, p->start_in_playback_time, p->duration_in_playback_time, nullptr, s.sequences.at(p->region_sequence_id), p->name, p->color ? &c : nullptr};
        s.regions.emplace(id, s.api()->createPlaybackRegion(s.ref(), s.modifications.at(parent), hostRef<ARAPlaybackRegionHostRef>(id), &props)); 
    };
    localWire.update_playback_region_properties = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id, const aap_ara_playback_region_properties_t* p) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !p) return;
        ARAColor c{}; if (p->color) c = color(*p->color);
        ARAPlaybackRegionProperties props{sizeof(props), static_cast<ARAPlaybackTransformationFlags>(p->transformation_flags), p->start_in_modification_time, p->duration_in_modification_time, p->start_in_playback_time, p->duration_in_playback_time, nullptr, s.sequences.at(p->region_sequence_id), p->name, p->color ? &c : nullptr};
        s.api()->updatePlaybackRegionProperties(s.ref(), s.regions.at(id), &props); 
    };
    localWire.destroy_playback_region = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.dc || !s.editing || !s.regions.count(id)) return;
        s.api()->destroyPlaybackRegion(s.ref(), s.regions.at(id)); s.regions.erase(id);
    };
    localWire.enable_audio_source_samples_access = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id, bool enable) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        s.api()->enableAudioSourceSamplesAccess(s.ref(), s.sources.at(id), enable ? kARATrue : kARAFalse);
    };
    localWire.notify_audio_source_content_changed = [](aap_ara_extension_t* e, AndroidAudioPlugin*, int64_t id, int64_t start, int64_t count) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.editing) return;
        auto rate = s.sourceProperties.at(id).sampleRate;
        ARAContentTimeRange range{double(start) / rate, double(count) / rate};
        s.api()->updateAudioSourceContent(s.ref(), s.sources.at(id), &range, kARAContentUpdateEverythingChanged);
    };
    localWire.notify_selection = [](aap_ara_extension_t* e, AndroidAudioPlugin*, const aap_ara_view_selection_t* selection) {
        auto& s = self(e); const std::lock_guard<std::recursive_mutex> lock(s.mutex);
        if (!s.binding || !s.binding->editorViewInterface || s.editing) return false;
        std::vector<ARAPlaybackRegionRef> regions; std::vector<ARARegionSequenceRef> sequences;
        for (uint32_t i = 0; i < selection->playback_region_count; ++i) {
            auto it = s.regions.find(selection->playback_region_ids[i]); if (it == s.regions.end()) return false; regions.push_back(it->second);
        }
        for (uint32_t i = 0; i < selection->region_sequence_count; ++i) {
            auto it = s.sequences.find(selection->region_sequence_ids[i]); if (it == s.sequences.end()) return false; sequences.push_back(it->second);
        }
        ARAContentTimeRange range{selection->start, selection->duration};
        ARAViewSelection value{sizeof(value), regions.size(), regions.data(), sequences.size(), sequences.data(), selection->has_time_range ? &range : nullptr};
        s.binding->editorViewInterface->notifySelection(s.binding->editorViewRef, &value); return true;
    };

    wire.aapxs_context = this;
    localWire.aapxs_context = this;
    wire.get_factory_capability = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_factory_capability_t* destination) { auto& s = self(ext); return s.model.run([&] { return s.localWire.get_factory_capability(&s.localWire, plugin, destination); }); };
    wire.begin_model_update = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, uint32_t flags) { auto& s = self(ext); return s.model.run([&] { return s.localWire.begin_model_update(&s.localWire, plugin, flags); }); };
    wire.end_model_update = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin) { auto& s = self(ext); return s.model.run([&] { return s.localWire.end_model_update(&s.localWire, plugin); }); };
    wire.create_document = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t document_id, const aap_ara_document_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.create_document(&s.localWire, plugin, document_id, properties); }); };
    wire.update_document_properties = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t document_id, const aap_ara_document_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.update_document_properties(&s.localWire, plugin, document_id, properties); }); };
    wire.destroy_document = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t document_id) { auto& s = self(ext); return s.model.run([&] { return s.localWire.destroy_document(&s.localWire, plugin, document_id); }); };
    wire.create_musical_context = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t document_id, aap_ara_musical_context_id_t musical_context_id, const aap_ara_musical_context_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.create_musical_context(&s.localWire, plugin, document_id, musical_context_id, properties); }); };
    wire.update_musical_context_properties = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musical_context_id, const aap_ara_musical_context_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.update_musical_context_properties(&s.localWire, plugin, musical_context_id, properties); }); };
    wire.destroy_musical_context = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_musical_context_id_t musical_context_id) { auto& s = self(ext); return s.model.run([&] { return s.localWire.destroy_musical_context(&s.localWire, plugin, musical_context_id); }); };
    wire.create_region_sequence = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t document_id, aap_ara_region_sequence_id_t region_sequence_id, const aap_ara_region_sequence_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.create_region_sequence(&s.localWire, plugin, document_id, region_sequence_id, properties); }); };
    wire.update_region_sequence_properties = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t region_sequence_id, const aap_ara_region_sequence_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.update_region_sequence_properties(&s.localWire, plugin, region_sequence_id, properties); }); };
    wire.destroy_region_sequence = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_region_sequence_id_t region_sequence_id) { auto& s = self(ext); return s.model.run([&] { return s.localWire.destroy_region_sequence(&s.localWire, plugin, region_sequence_id); }); };
    wire.create_audio_source = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_document_id_t document_id, aap_ara_audio_source_id_t audio_source_id, const aap_ara_audio_source_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.create_audio_source(&s.localWire, plugin, document_id, audio_source_id, properties); }); };
    wire.update_audio_source_properties = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audio_source_id, const aap_ara_audio_source_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.update_audio_source_properties(&s.localWire, plugin, audio_source_id, properties); }); };
    wire.enable_audio_source_samples_access = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audio_source_id, bool enable) { auto& s = self(ext); return s.model.run([&] { return s.localWire.enable_audio_source_samples_access(&s.localWire, plugin, audio_source_id, enable); }); };
    wire.notify_audio_source_content_changed = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audio_source_id, aap_ara_sample_position_t changed_start_sample, aap_ara_sample_count_t changed_sample_count) { auto& s = self(ext); return s.model.run([&] { return s.localWire.notify_audio_source_content_changed(&s.localWire, plugin, audio_source_id, changed_start_sample, changed_sample_count); }); };
    wire.destroy_audio_source = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audio_source_id) { auto& s = self(ext); return s.model.run([&] { return s.localWire.destroy_audio_source(&s.localWire, plugin, audio_source_id); }); };
    wire.create_audio_modification = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_source_id_t audio_source_id, aap_ara_audio_modification_id_t audio_modification_id, const aap_ara_audio_modification_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.create_audio_modification(&s.localWire, plugin, audio_source_id, audio_modification_id, properties); }); };
    wire.update_audio_modification_properties = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audio_modification_id, const aap_ara_audio_modification_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.update_audio_modification_properties(&s.localWire, plugin, audio_modification_id, properties); }); };
    wire.destroy_audio_modification = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audio_modification_id) { auto& s = self(ext); return s.model.run([&] { return s.localWire.destroy_audio_modification(&s.localWire, plugin, audio_modification_id); }); };
    wire.create_playback_region = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_audio_modification_id_t audio_modification_id, aap_ara_playback_region_id_t playback_region_id, const aap_ara_playback_region_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.create_playback_region(&s.localWire, plugin, audio_modification_id, playback_region_id, properties); }); };
    wire.update_playback_region_properties = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_playback_region_id_t playback_region_id, const aap_ara_playback_region_properties_t* properties) { auto& s = self(ext); return s.model.run([&] { return s.localWire.update_playback_region_properties(&s.localWire, plugin, playback_region_id, properties); }); };
    wire.destroy_playback_region = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, aap_ara_playback_region_id_t playback_region_id) { auto& s = self(ext); return s.model.run([&] { return s.localWire.destroy_playback_region(&s.localWire, plugin, playback_region_id); }); };
    wire.notify_selection = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, const aap_ara_view_selection_t* selection) -> bool { auto& s = self(ext); return s.model.run([&] { return s.localWire.notify_selection(&s.localWire, plugin, selection); }); };
    wire.sdk_command = [](aap_ara_extension_t* ext, AndroidAudioPlugin* plugin, void* data, uint32_t size) -> bool { auto& s = self(ext); return s.model.run([&] { return s.localWire.sdk_command(&s.localWire, plugin, data, size); }); };
}

PlugIn::PlugIn(AndroidAudioPlugin* p, AndroidAudioPluginHost* h, const ARAAAPPlugInExtension* e) : impl(new Impl(p, h, e)) {}
PlugIn::~PlugIn() = default;
void PlugIn::prepareForRelease() { impl->model.run([this] { impl->clearDocument(); }); }
void PlugIn::synchronize(const std::function<void()>& action) { impl->model.run(action); }
aap_ara_extension_t* PlugIn::transportExtension() { return &impl->wire; }
void* PlugIn::getExtension(const char* uri) {
    if (!uri) return nullptr;
    if (!std::strcmp(uri, ARA_AAP_PLUGIN_EXTENSION_URI)) return const_cast<ARAAAPPlugInExtension*>(impl->extension);
    if (!std::strcmp(uri, ARA_AAP_TRANSPORT_EXTENSION_URI)) return &impl->wire;
    return nullptr;
}

struct Host::Impl {
    AndroidAudioPlugin* plugin;
    aap_ara_extension_t* wire;
    ARADocumentControllerHostInstance services{};
    ARAFactory factory{};
    Message metadata;
    std::vector<std::string> compatibleArchiveStrings;
    std::vector<const char*> compatibleArchives;
    std::vector<ARAContentType> analyzableTypes;
    std::string chunkArchiveID;
    ARADocumentControllerInterface api{};
    ARADocumentControllerInstance dc{};
    ARAPlugInExtensionInstance binding{};
    ARAPlaybackRendererInterface playback{};
    ARAEditorRendererInterface renderer{};
    ARAEditorViewInterface view{};
    std::map<int32_t, std::pair<std::string, std::string>> algorithmNames;
    std::map<int32_t, ARAProcessingAlgorithmProperties> algorithms;
    aap_ara_host_extension_t hostWire{};
    bool editing{}, bound{}, alive{};
    int64_t nextID = -1;
    struct Source { ARAAudioSourceHostRef ref; ARAChannelCount channels; ARASampleRate rate; };
    std::map<int64_t, Source> sources;
    std::vector<uint8_t> localSamples;
    std::map<int64_t, ARAMusicalContextHostRef> contextHosts;
    std::map<int64_t, std::pair<ARAContentReaderHostRef, uint32_t>> hostContentReaders;
    int64_t nextHostReader = 1;
    std::map<int64_t, ARAAudioModificationHostRef> modificationHosts;
    std::map<int64_t, ARAPlaybackRegionHostRef> regionHosts;
    std::map<int64_t, std::string> modificationIDs;
    std::recursive_mutex mutex;
    template<class R> static Impl& self(R r) { return *reinterpret_cast<Impl*>(r); }
    bool command(Message& message) {
        return wire->sdk_command && wire->sdk_command(wire, plugin, &message, sizeof(message)) && message.success;
    }
    Message query(uint32_t commandID, int64_t id = 0, uint32_t kind = 0, uint32_t type = 0, const ARAContentTimeRange* range = nullptr) {
        Message m; m.command = commandID; m.id = id; m.kind = kind; m.type = type;
        m.hasRange = range != nullptr; if (range) { m.start = range->start; m.duration = range->duration; }
        if (!command(m)) throw std::runtime_error("ARA SDK command failed: " + std::to_string(commandID)); return m;
    }
    void role(uint32_t op, int64_t id, uint32_t flags) { Message m; m.command = op; m.id = id; m.flags = flags; if (!command(m)) throw std::runtime_error("ARA role assignment failed"); }
    void poll() { Message m; m.command = Poll; if (!command(m)) throw std::runtime_error("ARA notifyModelUpdates failed"); }
    Impl(AndroidAudioPlugin* p, aap_ara_extension_t* w, const ARADocumentControllerHostInstance* h,
         const ARADocumentProperties* props, ARAAPIGeneration generation) : plugin(p), wire(w) {
        if (!h || h->structSize < kARADocumentControllerHostInstanceMinSize) throw std::invalid_argument("Invalid ARA host services");
        std::memcpy(&services, h, std::min<size_t>(h->structSize, sizeof(services)));
        if (!wire || !wire->get_factory_capability) throw std::runtime_error("Plugin has no ARAAAP transport");
        aap_ara_factory_capability_t capability{}; capability.struct_size = sizeof(capability);
        wire->get_factory_capability(wire, plugin, &capability);
        if (!(capability.supported_features & AAP_ARA_FEATURE_ARA_SDK)) throw std::runtime_error("Plugin has no ARA SDK integration");
        metadata.command = Factory;
        if (!command(metadata)) throw std::runtime_error("Could not query ARA factory");
        factory.structSize = sizeof(factory); factory.factoryID = metadata.factoryID;
        factory.plugInName = metadata.name; factory.manufacturerName = metadata.manufacturer;
        factory.informationURL = metadata.url; factory.version = metadata.version;
        factory.documentArchiveID = metadata.archiveID;
        factory.lowestSupportedApiGeneration = static_cast<ARAAPIGeneration>(metadata.lowest);
        factory.highestSupportedApiGeneration = static_cast<ARAAPIGeneration>(metadata.highest);
        factory.supportedPlaybackTransformationFlags = metadata.transformations;
        factory.supportsStoringAudioFileChunks = metadata.flags;
        for (uint32_t i = 0; i < metadata.length; ++i) { Message d; d.command = FactoryDetail; d.index = i; if (!command(d)) throw std::runtime_error("ARA compatible archive query failed"); compatibleArchiveStrings.emplace_back(d.archiveID); }
        for (auto& v : compatibleArchiveStrings) compatibleArchives.push_back(v.c_str());
        for (uint32_t i = 0; i < metadata.index; ++i) { Message d; d.command = FactoryDetail; d.kind = 1; d.index = i; if (!command(d)) throw std::runtime_error("ARA analyzable content query failed"); analyzableTypes.push_back(d.type); }
        factory.compatibleDocumentArchiveIDsCount = compatibleArchives.size(); factory.compatibleDocumentArchiveIDs = compatibleArchives.data();
        factory.analyzeableContentTypesCount = analyzableTypes.size(); factory.analyzeableContentTypes = analyzableTypes.data();
        Message m; m.command = Initialize; m.generation = generation;
        m.flags = (services.contentAccessControllerInterface ? 1u : 0u) | (services.playbackControllerInterface ? 2u : 0u) | (services.modelUpdateControllerInterface ? 4u : 0u);
        if (!command(m)) throw std::runtime_error("ARA API generation negotiation failed");
        install();
        auto normalized = properties(props); checkName(normalized.name);
        aap_ara_document_properties_t doc{sizeof(doc), normalized.name, nullptr};
        wire->create_document(wire, plugin, 1, &doc);
        query(DocumentStatus); alive = true;
        dc = {sizeof(dc), reinterpret_cast<ARADocumentControllerRef>(this), &api};
    }
    bool hostCommand(Message& m);
    void install();
    ~Impl() { if (alive) { Message m; m.command = Shutdown; command(m); } }
    void writeTransfer(const std::vector<uint8_t>& data, bool filter) {
        size_t offset = 0;
        do { Message m; m.command = WriteArchive; m.flags = filter; m.offset = offset; m.length = std::min<size_t>(sizeof(m.bytes), data.size() - offset);
            if (m.length) std::memcpy(m.bytes, data.data() + offset, m.length);
            if (!command(m)) throw std::runtime_error("ARA transfer failed"); offset += m.length;
        } while (offset < data.size());
    }
    std::vector<uint8_t> readArchive(size_t size) {
        Message m; std::vector<uint8_t> bytes(size);
        for (size_t offset = 0; offset < size;) {
            m = {}; m.command = ReadArchive; m.offset = offset;
            if (!command(m) || !m.length || m.length > sizeof(m.bytes) || m.length > size - offset) throw std::runtime_error("ARA archive transfer failed");
            std::memcpy(bytes.data() + offset, m.bytes, m.length); offset += m.length;
        } return bytes;
    }
    std::vector<uint8_t> store(int64_t id, const ARAStoreObjectsFilter* filter = nullptr) {
        if (filter) {
            Encoder e; e.value<uint32_t>(filter->documentData); e.value<uint32_t>(filter->audioSourceRefsCount);
            for (size_t i = 0; i < filter->audioSourceRefsCount; ++i) e.value<int64_t>(hostID(filter->audioSourceRefs[i]));
            e.value<uint32_t>(filter->audioModificationRefsCount); for (size_t i = 0; i < filter->audioModificationRefsCount; ++i) e.value<int64_t>(hostID(filter->audioModificationRefs[i])); writeTransfer(e.bytes, true);
        }
        Message m; m.command = Store; m.id = id; m.flags = filter != nullptr;
        if (!command(m) || m.offset < 0) throw std::runtime_error("ARA archive export failed");
        std::vector<uint8_t> bytes(static_cast<size_t>(m.offset));
        for (size_t offset = 0; offset < bytes.size();) {
            m = {}; m.command = ReadArchive; m.offset = offset;
            if (!command(m) || !m.length || m.length > sizeof(m.bytes) || m.length > bytes.size() - offset) throw std::runtime_error("ARA archive transfer failed");
            std::memcpy(bytes.data() + offset, m.bytes, m.length); offset += m.length;
        }
        return bytes;
    }
    bool restore(int64_t id, const void* data, size_t size, const char* archivedID = nullptr, const ARARestoreObjectsFilter* filter = nullptr) {
        if (filter) {
            Encoder e; e.value<uint32_t>(filter->documentData); e.value<uint32_t>(filter->audioSourceIDsCount);
            for (size_t i = 0; i < filter->audioSourceIDsCount; ++i) { e.string(filter->audioSourceArchiveIDs[i]); e.string(filter->audioSourceCurrentIDs ? filter->audioSourceCurrentIDs[i] : filter->audioSourceArchiveIDs[i]); }
            e.value<uint32_t>(filter->audioModificationIDsCount);
            for (size_t i = 0; i < filter->audioModificationIDsCount; ++i) { e.string(filter->audioModificationArchiveIDs[i]); e.string(filter->audioModificationCurrentIDs ? filter->audioModificationCurrentIDs[i] : filter->audioModificationArchiveIDs[i]); } writeTransfer(e.bytes, true);
        }
        if (!data && size) return false;
        Message m;
        for (size_t offset = 0;;) {
            m = {}; m.command = WriteArchive; m.offset = offset; m.length = std::min<size_t>(sizeof(m.bytes), size - offset);
            if (m.length) std::memcpy(m.bytes, static_cast<const uint8_t*>(data) + offset, m.length);
            if (!command(m)) return false;
            offset += m.length; if (offset == size) break;
        }
        m = {}; m.command = Restore; m.id = id; m.flags = filter != nullptr; if (archivedID) copy(m.archiveID, archivedID); return command(m);
    }
};

bool Host::Impl::hostCommand(Message& m) {
    m.success = false;
    auto* content = services.contentAccessControllerInterface; auto c = services.contentAccessControllerHostRef;
    switch (m.command) {
        case ContentAvailable: case ContentGrade: case CreateContentReader: {
            if (!content) break;
            ARAContentTimeRange range{m.start, m.duration}; ARAContentReaderHostRef reader{};
            if (m.kind == 0) {
                auto object = contextHosts.at(m.id);
                if (m.command == ContentAvailable) m.index = content->isMusicalContextContentAvailable(c, object, m.type);
                else if (m.command == ContentGrade) m.index = content->getMusicalContextContentGrade(c, object, m.type);
                else reader = content->createMusicalContextContentReader(c, object, m.type, m.hasRange ? &range : nullptr);
            }
            if (m.kind == 1) {
                auto object = sources.at(m.id).ref;
                if (m.command == ContentAvailable) m.index = content->isAudioSourceContentAvailable(c, object, m.type);
                else if (m.command == ContentGrade) m.index = content->getAudioSourceContentGrade(c, object, m.type);
                else reader = content->createAudioSourceContentReader(c, object, m.type, m.hasRange ? &range : nullptr);
            }
            if (m.command == CreateContentReader) { m.id = reader ? nextHostReader++ : 0; if (reader) hostContentReaders[m.id] = {reader, m.type}; }
            m.success = true; break;
        }
        case ContentCount: m.index = content->getContentReaderEventCount(c, hostContentReaders.at(m.id).first); m.success = true; break;
        case ContentEvent: { auto reader = hostContentReaders.at(m.id); m.success = Event::encode(reader.second, content->getContentReaderDataForEvent(c, reader.first, m.index), m.bytes); break; }
        case DestroyContentReader: content->destroyContentReader(c, hostContentReaders.at(m.id).first); hostContentReaders.erase(m.id); m.success = true; break;
        case HostProgress:
            if (services.modelUpdateControllerInterface && sources.count(m.id)) {
                services.modelUpdateControllerInterface->notifyAudioSourceAnalysisProgress(services.modelUpdateControllerHostRef, sources.at(m.id).ref, static_cast<ARAAnalysisProgressState>(m.index), m.start); m.success = true;
            } break;
        case HostPlayback: {
            auto* p = services.playbackControllerInterface; auto r = services.playbackControllerHostRef; if (!p) break;
            switch (m.kind) {
                case 0: p->requestStartPlayback(r); break; case 1: p->requestStopPlayback(r); break;
                case 2: p->requestSetPlaybackPosition(r, m.start); break;
                case 3: p->requestSetCycleRange(r, m.start, m.duration); break;
                case 4: p->requestEnableCycle(r, m.flags != 0); break;
                default: return true;
            } m.success = true; break;
        }
        default: break;
    }
    return true;
}

void Host::Impl::install() {
    api.structSize = sizeof(api);
    // The ARA 2 transport uses partial persistency. Deprecated ARA 1 archive
    // operations still need callable slots in the advertised interface; report
    // failure so hosts can use storeObjectsToArchive/restoreObjectsFromArchive.
    api.beginRestoringDocumentFromArchive = [](ARADocumentControllerRef, ARAArchiveReaderHostRef) -> ARABool { return kARAFalse; };
    api.endRestoringDocumentFromArchive = [](ARADocumentControllerRef, ARAArchiveReaderHostRef) -> ARABool { return kARAFalse; };
    api.storeDocumentToArchive = [](ARADocumentControllerRef, ARAArchiveWriterHostRef) -> ARABool { return kARAFalse; };
    api.destroyDocumentController = [](ARADocumentControllerRef r) { auto& s = self(r); s.query(DestroyDocument); s.alive = false; };
    api.getFactory = [](ARADocumentControllerRef r) -> const ARAFactory* { return &self(r).factory; };
    api.beginEditing = [](ARADocumentControllerRef r) { auto& s = self(r); s.wire->begin_model_update(s.wire, s.plugin, 0); s.editing = true; };
    api.endEditing = [](ARADocumentControllerRef r) { auto& s = self(r); s.wire->end_model_update(s.wire, s.plugin); s.editing = false; };
    api.notifyModelUpdates = [](ARADocumentControllerRef r) { self(r).poll(); };
    api.updateDocumentProperties = [](ARADocumentControllerRef r, const ARADocumentProperties* input) {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name); auto& s = self(r); aap_ara_document_properties_t v{sizeof(v), p->name, nullptr}; s.wire->update_document_properties(s.wire, s.plugin, 1, &v);
    };
    api.createMusicalContext = [](ARADocumentControllerRef r, ARAMusicalContextHostRef h, const ARAMusicalContextProperties* input) -> ARAMusicalContextRef {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(h); if (!id) id = s.nextID--; aap_ara_color_t c{}; const aap_ara_color_t* color{}; if (p->color) { c = {p->color->r, p->color->g, p->color->b}; color = &c; }
        aap_ara_musical_context_properties_t v{sizeof(v), p->name, color, nullptr, p->orderIndex}; { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.contextHosts[id] = h; }
        s.wire->create_musical_context(s.wire, s.plugin, 1, id, &v); return hostRef<ARAMusicalContextRef>(id);
    };
    api.updateMusicalContextProperties = [](ARADocumentControllerRef r, ARAMusicalContextRef ref, const ARAMusicalContextProperties* input) {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(ref); aap_ara_color_t c{}; const aap_ara_color_t* color{}; if (p->color) { c = {p->color->r, p->color->g, p->color->b}; color = &c; }
        aap_ara_musical_context_properties_t v{sizeof(v), p->name, color, nullptr, p->orderIndex}; 
        s.wire->update_musical_context_properties(s.wire, s.plugin, id, &v); 
    };
    api.destroyMusicalContext = [](ARADocumentControllerRef r, ARAMusicalContextRef ref) {
        auto& s = self(r); s.wire->destroy_musical_context(s.wire, s.plugin, hostID(ref));
    };
    api.createRegionSequence = [](ARADocumentControllerRef r, ARARegionSequenceHostRef h, const ARARegionSequenceProperties* input) -> ARARegionSequenceRef {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(h); if (!id) id = s.nextID--; aap_ara_color_t c{}; const aap_ara_color_t* color{}; if (p->color) { c = {p->color->r, p->color->g, p->color->b}; color = &c; }
        aap_ara_region_sequence_properties_t v{sizeof(v), p->name, p->orderIndex, hostID(p->musicalContextRef), color, nullptr}; 
        s.wire->create_region_sequence(s.wire, s.plugin, 1, id, &v); return hostRef<ARARegionSequenceRef>(id);
    };
    api.updateRegionSequenceProperties = [](ARADocumentControllerRef r, ARARegionSequenceRef ref, const ARARegionSequenceProperties* input) {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(ref); aap_ara_color_t c{}; const aap_ara_color_t* color{}; if (p->color) { c = {p->color->r, p->color->g, p->color->b}; color = &c; }
        aap_ara_region_sequence_properties_t v{sizeof(v), p->name, p->orderIndex, hostID(p->musicalContextRef), color, nullptr}; 
        s.wire->update_region_sequence_properties(s.wire, s.plugin, id, &v); 
    };
    api.destroyRegionSequence = [](ARADocumentControllerRef r, ARARegionSequenceRef ref) {
        auto& s = self(r); s.wire->destroy_region_sequence(s.wire, s.plugin, hostID(ref));
    };
    api.createAudioSource = [](ARADocumentControllerRef r, ARAAudioSourceHostRef h, const ARAAudioSourceProperties* input) -> ARAAudioSourceRef {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(h); if (!id) id = s.nextID--; 
        checkName(p->persistentID); if (p->channelCount <= 0 || arrangementSize(*p) > 4096) throw std::invalid_argument("Invalid ARA channel arrangement");
        aap_ara_audio_source_properties_t v{sizeof(v), p->name, p->persistentID, p->sampleCount, p->sampleRate, p->channelCount, p->merits64BitSamples != 0, nullptr, p->channelArrangementDataType, static_cast<uint32_t>(arrangementSize(*p)), p->channelArrangement}; { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.sources[id] = {h, p->channelCount, p->sampleRate}; }
        s.wire->create_audio_source(s.wire, s.plugin, 1, id, &v); return hostRef<ARAAudioSourceRef>(id);
    };
    api.updateAudioSourceProperties = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, const ARAAudioSourceProperties* input) {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(ref); 
        checkName(p->persistentID); if (p->channelCount <= 0 || arrangementSize(*p) > 4096) throw std::invalid_argument("Invalid ARA channel arrangement");
        aap_ara_audio_source_properties_t v{sizeof(v), p->name, p->persistentID, p->sampleCount, p->sampleRate, p->channelCount, p->merits64BitSamples != 0, nullptr, p->channelArrangementDataType, static_cast<uint32_t>(arrangementSize(*p)), p->channelArrangement}; { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.sources.at(id).channels = p->channelCount; } { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.sources.at(id).rate = p->sampleRate; }
        s.wire->update_audio_source_properties(s.wire, s.plugin, id, &v); 
    };
    api.destroyAudioSource = [](ARADocumentControllerRef r, ARAAudioSourceRef ref) {
        auto& s = self(r); s.wire->destroy_audio_source(s.wire, s.plugin, hostID(ref));
    };
    api.createAudioModification = [](ARADocumentControllerRef r, ARAAudioSourceRef parent, ARAAudioModificationHostRef h, const ARAAudioModificationProperties* input) -> ARAAudioModificationRef {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(h); if (!id) id = s.nextID--; 
        checkName(p->persistentID);
        aap_ara_audio_modification_properties_t v{sizeof(v), p->name, p->persistentID}; { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.modificationHosts[id] = h; } { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.modificationIDs[id] = p->persistentID; }
        s.wire->create_audio_modification(s.wire, s.plugin, hostID(parent), id, &v); return hostRef<ARAAudioModificationRef>(id);
    };
    api.updateAudioModificationProperties = [](ARADocumentControllerRef r, ARAAudioModificationRef ref, const ARAAudioModificationProperties* input) {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(ref); 
        checkName(p->persistentID);
        aap_ara_audio_modification_properties_t v{sizeof(v), p->name, p->persistentID}; { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.modificationIDs[id] = p->persistentID; }
        s.wire->update_audio_modification_properties(s.wire, s.plugin, id, &v); 
    };
    api.destroyAudioModification = [](ARADocumentControllerRef r, ARAAudioModificationRef ref) {
        auto& s = self(r); s.wire->destroy_audio_modification(s.wire, s.plugin, hostID(ref));
    };
    api.createPlaybackRegion = [](ARADocumentControllerRef r, ARAAudioModificationRef parent, ARAPlaybackRegionHostRef h, const ARAPlaybackRegionProperties* input) -> ARAPlaybackRegionRef {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(h); if (!id) id = s.nextID--; aap_ara_color_t c{}; const aap_ara_color_t* color{}; if (p->color) { c = {p->color->r, p->color->g, p->color->b}; color = &c; }
        aap_ara_playback_region_properties_t v{sizeof(v), static_cast<uint32_t>(p->transformationFlags), p->startInModificationTime, p->durationInModificationTime, p->startInPlaybackTime, p->durationInPlaybackTime, hostID(p->regionSequenceRef), p->name, color}; { const std::lock_guard<std::recursive_mutex> cache(s.mutex); s.regionHosts[id] = h; }
        s.wire->create_playback_region(s.wire, s.plugin, hostID(parent), id, &v); return hostRef<ARAPlaybackRegionRef>(id);
    };
    api.updatePlaybackRegionProperties = [](ARADocumentControllerRef r, ARAPlaybackRegionRef ref, const ARAPlaybackRegionProperties* input) {
        auto normalized = properties(input); auto* p = &normalized;
        checkName(p->name);
        auto& s = self(r); auto id = hostID(ref); aap_ara_color_t c{}; const aap_ara_color_t* color{}; if (p->color) { c = {p->color->r, p->color->g, p->color->b}; color = &c; }
        aap_ara_playback_region_properties_t v{sizeof(v), static_cast<uint32_t>(p->transformationFlags), p->startInModificationTime, p->durationInModificationTime, p->startInPlaybackTime, p->durationInPlaybackTime, hostID(p->regionSequenceRef), p->name, color}; 
        s.wire->update_playback_region_properties(s.wire, s.plugin, id, &v); 
    };
    api.destroyPlaybackRegion = [](ARADocumentControllerRef r, ARAPlaybackRegionRef ref) {
        auto& s = self(r); s.wire->destroy_playback_region(s.wire, s.plugin, hostID(ref));
    };
    api.enableAudioSourceSamplesAccess = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARABool enabled) {
        auto& s = self(r); s.wire->enable_audio_source_samples_access(s.wire, s.plugin, hostID(ref), enabled != 0);
    };
    api.updateAudioSourceContent = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, const ARAContentTimeRange* range, ARAContentUpdateFlags) {
        auto& s = self(r); auto rate = s.sources.at(hostID(ref)).rate;
        s.wire->notify_audio_source_content_changed(s.wire, s.plugin, hostID(ref), range ? int64_t(range->start * rate) : 0, range ? int64_t(range->duration * rate) : 0);
    };
    api.storeObjectsToArchive = [](ARADocumentControllerRef r, ARAArchiveWriterHostRef writer, const ARAStoreObjectsFilter* filter) -> ARABool {
        auto& s = self(r);
        try {
            auto bytes = s.store(0, filter);
            auto* a = s.services.archivingControllerInterface;
            return a && a->writeBytesToArchive(s.services.archivingControllerHostRef, writer, 0, bytes.size(), bytes.data());
        } catch (...) { return kARAFalse; }
    };
    api.restoreObjectsFromArchive = [](ARADocumentControllerRef r, ARAArchiveReaderHostRef reader, const ARARestoreObjectsFilter* filter) -> ARABool {
        auto& s = self(r);
        auto* a = s.services.archivingControllerInterface; if (!a) return kARAFalse;
        std::vector<uint8_t> bytes(a->getArchiveSize(s.services.archivingControllerHostRef, reader));
        if (!a->readBytesFromArchive(s.services.archivingControllerHostRef, reader, 0, bytes.size(), bytes.data())) return kARAFalse;
        return s.restore(0, bytes.data(), bytes.size(), nullptr, filter) ? kARATrue : kARAFalse;
    };
    api.isAudioSourceContentAvailable = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARAContentType type) -> ARABool { return static_cast<ARABool>(self(r).query(ContentAvailable, hostID(ref), 1, type).index); };
    api.getAudioSourceContentGrade = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARAContentType type) -> ARAContentGrade { return static_cast<ARAContentGrade>(self(r).query(ContentGrade, hostID(ref), 1, type).index); };
    api.createAudioSourceContentReader = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARAContentType type, const ARAContentTimeRange* range) -> ARAContentReaderRef {
        auto m = self(r).query(CreateContentReader, hostID(ref), 1, type, range);
        return m.id ? reinterpret_cast<ARAContentReaderRef>(new RemoteReader{m.id, static_cast<uint32_t>(type), {}}) : nullptr;
    };
    api.isAudioModificationContentAvailable = [](ARADocumentControllerRef r, ARAAudioModificationRef ref, ARAContentType type) -> ARABool { return static_cast<ARABool>(self(r).query(ContentAvailable, hostID(ref), 2, type).index); };
    api.getAudioModificationContentGrade = [](ARADocumentControllerRef r, ARAAudioModificationRef ref, ARAContentType type) -> ARAContentGrade { return static_cast<ARAContentGrade>(self(r).query(ContentGrade, hostID(ref), 2, type).index); };
    api.createAudioModificationContentReader = [](ARADocumentControllerRef r, ARAAudioModificationRef ref, ARAContentType type, const ARAContentTimeRange* range) -> ARAContentReaderRef {
        auto m = self(r).query(CreateContentReader, hostID(ref), 2, type, range);
        return m.id ? reinterpret_cast<ARAContentReaderRef>(new RemoteReader{m.id, static_cast<uint32_t>(type), {}}) : nullptr;
    };
    api.isPlaybackRegionContentAvailable = [](ARADocumentControllerRef r, ARAPlaybackRegionRef ref, ARAContentType type) -> ARABool { return static_cast<ARABool>(self(r).query(ContentAvailable, hostID(ref), 3, type).index); };
    api.getPlaybackRegionContentGrade = [](ARADocumentControllerRef r, ARAPlaybackRegionRef ref, ARAContentType type) -> ARAContentGrade { return static_cast<ARAContentGrade>(self(r).query(ContentGrade, hostID(ref), 3, type).index); };
    api.createPlaybackRegionContentReader = [](ARADocumentControllerRef r, ARAPlaybackRegionRef ref, ARAContentType type, const ARAContentTimeRange* range) -> ARAContentReaderRef {
        auto m = self(r).query(CreateContentReader, hostID(ref), 3, type, range);
        return m.id ? reinterpret_cast<ARAContentReaderRef>(new RemoteReader{m.id, static_cast<uint32_t>(type), {}}) : nullptr;
    };
    api.getContentReaderEventCount = [](ARADocumentControllerRef r, ARAContentReaderRef reader) -> ARAInt32 { return self(r).query(ContentCount, reinterpret_cast<RemoteReader*>(reader)->id).index; };
    api.getContentReaderDataForEvent = [](ARADocumentControllerRef r, ARAContentReaderRef reader, ARAInt32 index) -> const void* {
        auto& s = self(r); auto* rr = reinterpret_cast<RemoteReader*>(reader); Message m; m.command = ContentEvent; m.id = rr->id; m.index = index;
        return s.command(m) ? rr->event.decode(rr->type, m.bytes) : nullptr;
    };
    api.destroyContentReader = [](ARADocumentControllerRef r, ARAContentReaderRef reader) { auto* rr = reinterpret_cast<RemoteReader*>(reader); self(r).query(DestroyContentReader, rr->id); delete rr; };
    api.isAudioSourceContentAnalysisIncomplete = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARAContentType type) -> ARABool { return self(r).query(AnalysisIncomplete, hostID(ref), 1, type).index; };
    api.requestAudioSourceContentAnalysis = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARASize count, const ARAContentType* types) {
        Message m; m.command = RequestAnalysis; m.id = hostID(ref); if (count > sizeof(m.bytes) / sizeof(int32_t)) throw std::length_error("Too many ARA content types");
        m.length = count; if (count) std::memcpy(m.bytes, types, count * sizeof(int32_t)); if (!self(r).command(m)) throw std::runtime_error("ARA analysis request failed");
    };
    api.updateMusicalContextContent = [](ARADocumentControllerRef r, ARAMusicalContextRef ref, const ARAContentTimeRange* range, ARAContentUpdateFlags flags) {
        auto& s = self(r); Message m; m.command = UpdateContent; m.id = hostID(ref); m.flags = flags; m.hasRange = range != nullptr; if (range) { m.start = range->start; m.duration = range->duration; }
        if (!s.command(m)) throw std::runtime_error("ARA content update failed");
    };
    api.updateAudioSourceContent = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, const ARAContentTimeRange* range, ARAContentUpdateFlags flags) {
        auto& s = self(r); Message m; m.command = UpdateContent; m.id = hostID(ref); m.kind = 1; m.flags = flags; m.hasRange = range != nullptr; if (range) { m.start = range->start; m.duration = range->duration; }
        if (!s.command(m)) throw std::runtime_error("ARA content update failed");
    };
    api.deactivateAudioSourceForUndoHistory = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARABool deactivate) { Message m; m.command = Deactivate; m.kind = 1; m.id = hostID(ref); m.flags = deactivate; if (!self(r).command(m)) throw std::runtime_error("ARA deactivate failed"); };
    api.deactivateAudioModificationForUndoHistory = [](ARADocumentControllerRef r, ARAAudioModificationRef ref, ARABool deactivate) { Message m; m.command = Deactivate; m.kind = 2; m.id = hostID(ref); m.flags = deactivate; if (!self(r).command(m)) throw std::runtime_error("ARA deactivate failed"); };
    api.cloneAudioModification = [](ARADocumentControllerRef r, ARAAudioModificationRef ref, ARAAudioModificationHostRef h, const ARAAudioModificationProperties* p) -> ARAAudioModificationRef {
        auto& s = self(r); auto id = hostID(h); if (!id) id = s.nextID--;
        Message m; m.command = CloneModification; m.id = id; m.offset = hostID(ref); copy(m.name, p->name); copy(m.archiveID, p->persistentID);
        if (!s.command(m)) return nullptr; s.modificationHosts[id] = h; s.modificationIDs[id] = p->persistentID; return hostRef<ARAAudioModificationRef>(id);
    };
    api.getPlaybackRegionHeadAndTailTime = [](ARADocumentControllerRef r, ARAPlaybackRegionRef ref, ARATimeDuration* head, ARATimeDuration* tail) { auto m = self(r).query(HeadTail, hostID(ref)); if (head) *head = m.start; if (tail) *tail = m.duration; };
    api.getProcessingAlgorithmsCount = [](ARADocumentControllerRef r) -> ARAInt32 { return self(r).query(AlgorithmsCount).index; };
    api.getProcessingAlgorithmProperties = [](ARADocumentControllerRef r, ARAInt32 index) -> const ARAProcessingAlgorithmProperties* {
        auto& s = self(r); Message m; m.command = AlgorithmProperties; m.index = index; if (!s.command(m)) return nullptr;
        auto& names = s.algorithmNames[index]; names = {m.name, m.archiveID};
        auto& p = s.algorithms[index]; p = {sizeof(p), names.second.c_str(), names.first.c_str()}; return &p;
    };
    api.getProcessingAlgorithmForAudioSource = [](ARADocumentControllerRef r, ARAAudioSourceRef ref) -> ARAInt32 { return self(r).query(SourceAlgorithm, hostID(ref)).index; };
    api.requestProcessingAlgorithmForAudioSource = [](ARADocumentControllerRef r, ARAAudioSourceRef ref, ARAInt32 index) { Message m; m.command = RequestAlgorithm; m.id = hostID(ref); m.index = index; if (!self(r).command(m)) throw std::runtime_error("ARA processing algorithm request failed"); };
    api.isLicensedForCapabilities = [](ARADocumentControllerRef r, ARABool modal, ARASize count, const ARAContentType* types, ARAPlaybackTransformationFlags flags) -> ARABool {
        Message m; m.command = Licensed; m.known = modal; m.flags = flags; if (count > sizeof(m.bytes) / sizeof(int32_t)) return kARAFalse;
        m.length = count; if (count) std::memcpy(m.bytes, types, count * sizeof(int32_t)); return self(r).command(m) && m.index ? kARATrue : kARAFalse;
    };
    api.storeAudioSourceToAudioFileChunk = [](ARADocumentControllerRef r, ARAArchiveWriterHostRef writer, ARAAudioSourceRef source, ARAPersistentID* archiveID, ARABool* open) -> ARABool {
        auto& s = self(r); Message m; m.command = StoreFileChunk; m.id = hostID(source);
        if (!s.command(m) || m.offset < 0) return kARAFalse;
        auto data = s.readArchive(m.offset); auto* a = s.services.archivingControllerInterface;
        if (!a || !a->writeBytesToArchive(s.services.archivingControllerHostRef, writer, 0, data.size(), data.data())) return kARAFalse;
        s.chunkArchiveID = m.archiveID; if (archiveID) *archiveID = s.chunkArchiveID.c_str(); if (open) *open = m.flags; return kARATrue;
    };
    api.isAudioModificationPreservingAudioSourceSignal = [](ARADocumentControllerRef r, ARAAudioModificationRef ref) -> ARABool { return self(r).query(PreservesSignal, hostID(ref)).index; };
    playback = {sizeof(playback),
        [](ARAPlaybackRendererRef r, ARAPlaybackRegionRef region) { self(r).role(RoleAdd, hostID(region), kARAPlaybackRendererRole); },
        [](ARAPlaybackRendererRef r, ARAPlaybackRegionRef region) { self(r).role(RoleRemove, hostID(region), kARAPlaybackRendererRole); }};
    renderer = {sizeof(renderer),
        [](ARAEditorRendererRef r, ARAPlaybackRegionRef region) { self(r).role(RoleAdd, hostID(region), kARAEditorRendererRole); },
        [](ARAEditorRendererRef r, ARAPlaybackRegionRef region) { self(r).role(RoleRemove, hostID(region), kARAEditorRendererRole); },
        [](ARAEditorRendererRef r, ARARegionSequenceRef ref) { self(r).role(RoleSequenceAdd, hostID(ref), 0); },
        [](ARAEditorRendererRef r, ARARegionSequenceRef ref) { self(r).role(RoleSequenceRemove, hostID(ref), 0); }};
    view = {sizeof(view),
        [](ARAEditorViewRef r, const ARAViewSelection* selection) {
            auto& s = self(r); std::vector<int64_t> regions, sequences;
            for (size_t i = 0; i < selection->playbackRegionRefsCount; ++i) regions.push_back(hostID(selection->playbackRegionRefs[i]));
            for (size_t i = 0; i < selection->regionSequenceRefsCount; ++i) sequences.push_back(hostID(selection->regionSequenceRefs[i]));
            auto* range = selection->timeRange;
            aap_ara_view_selection_t v{sizeof(v), 1, static_cast<uint32_t>(regions.size()), regions.data(), static_cast<uint32_t>(sequences.size()), sequences.data(), range != nullptr, range ? range->start : 0, range ? range->duration : 0};
            if (!s.wire->notify_selection(s.wire, s.plugin, &v)) throw std::runtime_error("ARA selection rejected");
        }, [](ARAEditorViewRef r, ARASize count, const ARARegionSequenceRef* refs) {
            Message m; m.command = HideSequences; if (count > sizeof(m.bytes) / sizeof(int64_t)) throw std::length_error("Too many hidden ARA sequences"); m.length = count;
            for (size_t i = 0; i < count; ++i) { auto id = hostID(refs[i]); std::memcpy(m.bytes + i * sizeof(id), &id, sizeof(id)); }
            if (!self(r).command(m)) throw std::runtime_error("ARA hide sequences failed");
        }};
    hostWire = {this,
        [](aap_ara_host_extension_t* e, AndroidAudioPluginHost*, aap_ara_host_capability_t* c) {
            auto& s = *static_cast<Impl*>(e->aapxs_context);
            *c = {sizeof(*c), AAP_ARA_API_GENERATION_2, 7, AAP_ARA_SAMPLE_FORMAT_FLOAT32 | AAP_ARA_SAMPLE_FORMAT_FLOAT64,
                (s.services.modelUpdateControllerInterface ? AAP_ARA_HOST_SUPPORTS_CONTENT_UPDATES : 0u) | AAP_ARA_HOST_SUPPORTS_SDK};
        },
        [](aap_ara_host_extension_t* e, AndroidAudioPluginHost*, int64_t id, const aap_ara_audio_source_sample_range_t* range, aap_ara_audio_source_samples_buffer_t* d) {
            auto& s = *static_cast<Impl*>(e->aapxs_context); const std::lock_guard<std::recursive_mutex> cache(s.mutex); auto it = s.sources.find(id);
            if (it == s.sources.end()) { d->data_size = 0; return; }
            auto* a = s.services.audioAccessControllerInterface;
            bool wide = range->sample_format == AAP_ARA_SAMPLE_FORMAT_FLOAT64;
            size_t bytes = wide ? sizeof(double) : sizeof(float);
            if (!d->data && range->sample_count >= 0 && range->channel_count > 0 &&
                uint64_t(range->sample_count) <= (0x100000u - 1024u) / bytes / range->channel_count) {
                s.localSamples.resize(range->sample_count * range->channel_count * bytes);
                d->data = s.localSamples.data(); d->data_size = s.localSamples.size();
            }
            if (!a || range->sample_count < 0 || range->channel_count != it->second.channels ||
                size_t(range->sample_count) > d->data_size / bytes / range->channel_count) { d->data_size = 0; return; }
            auto reader = a->createAudioReaderForSource(s.services.audioAccessControllerHostRef, it->second.ref, wide ? kARATrue : kARAFalse);
            if (!reader) { d->data_size = 0; return; }
            std::vector<void*> buffers(range->channel_count);
            for (int c = 0; c < range->channel_count; ++c) buffers[c] = static_cast<uint8_t*>(d->data) + c * range->sample_count * bytes;
            auto ok = a->readAudioSamples(s.services.audioAccessControllerHostRef, reader, range->start_sample, range->sample_count, buffers.data());
            a->destroyAudioReader(s.services.audioAccessControllerHostRef, reader);
            d->data_size = ok ? range->sample_count * range->channel_count * bytes : 0;
            d->sample_count = ok ? range->sample_count : 0; d->channel_count = range->channel_count; d->sample_format = range->sample_format;
        },
        [](aap_ara_host_extension_t* e, AndroidAudioPluginHost*, const aap_ara_content_update_t* u) {
            auto& s = *static_cast<Impl*>(e->aapxs_context); const std::lock_guard<std::recursive_mutex> cache(s.mutex); auto* a = s.services.modelUpdateControllerInterface; if (!a) return;
            auto r = s.services.modelUpdateControllerHostRef; ARAContentTimeRange range{u->start, u->duration};
            switch (u->kind) {
                case AAP_ARA_CONTENT_AUDIO_SOURCE: if (s.sources.count(u->object_id)) a->notifyAudioSourceContentChanged(r, s.sources.at(u->object_id).ref, u->has_time_range ? &range : nullptr, u->flags); break;
                case AAP_ARA_CONTENT_AUDIO_MODIFICATION: if (s.modificationHosts.count(u->object_id)) a->notifyAudioModificationContentChanged(r, s.modificationHosts.at(u->object_id), u->has_time_range ? &range : nullptr, u->flags); break;
                case AAP_ARA_CONTENT_PLAYBACK_REGION: if (s.regionHosts.count(u->object_id)) a->notifyPlaybackRegionContentChanged(r, s.regionHosts.at(u->object_id), u->has_time_range ? &range : nullptr, u->flags); break;
                case AAP_ARA_CONTENT_DOCUMENT: if (a->structSize >= sizeof(*a) && a->notifyDocumentDataChanged) a->notifyDocumentDataChanged(r); break;
            }
        }, [](aap_ara_host_extension_t* e, AndroidAudioPluginHost*, void* data, uint32_t size) {
            if (!data || size != sizeof(Message)) return false; Message m; std::memcpy(&m, data, size); if (m.size != size) return false;
            auto& s = *static_cast<Impl*>(e->aapxs_context);
            try { s.hostCommand(m); } catch (...) { m.success = false; }
            std::memcpy(data, &m, size); return true;
        }};
}

Host::Host(AndroidAudioPlugin* p, const ARADocumentControllerHostInstance* h, const ARADocumentProperties* props, ARAAPIGeneration g)
    : Host(p, p && p->get_extension ? static_cast<aap_ara_extension_t*>(p->get_extension(p, ARA_AAP_TRANSPORT_EXTENSION_URI)) : nullptr, h, props, g) {}
Host::Host(AndroidAudioPlugin* p, aap_ara_extension_t* w, const ARADocumentControllerHostInstance* h, const ARADocumentProperties* props, ARAAPIGeneration g) : impl(new Impl(p, w, h, props, g)) {}
Host::~Host() = default;
const ARAFactory* Host::factory() const { return &impl->factory; }
const ARADocumentControllerInstance* Host::documentController() const { return &impl->dc; }
aap_ara_host_extension_t* Host::transportExtension() { return &impl->hostWire; }
const ARAPlugInExtensionInstance* Host::bind(ARAPlugInInstanceRoleFlags known, ARAPlugInInstanceRoleFlags assigned) {
    if (impl->bound) throw std::runtime_error("AAP instance already bound to ARA");
    Message m; m.command = Bind; m.known = known; m.assigned = assigned;
    if (!impl->command(m)) throw std::runtime_error("ARA binding failed");
    impl->bound = true;
    auto& b = impl->binding; b.structSize = sizeof(b);
    if (assigned & kARAPlaybackRendererRole) { b.playbackRendererRef = reinterpret_cast<ARAPlaybackRendererRef>(impl.get()); b.playbackRendererInterface = &impl->playback; }
    if (assigned & kARAEditorRendererRole) { b.editorRendererRef = reinterpret_cast<ARAEditorRendererRef>(impl.get()); b.editorRendererInterface = &impl->renderer; }
    if (assigned & kARAEditorViewRole) { b.editorViewRef = reinterpret_cast<ARAEditorViewRef>(impl.get()); b.editorViewInterface = &impl->view; }
    return &b;
}
bool Host::storeModification(ARAAudioModificationRef ref, std::string& bytes) {
    try {
        auto data = impl->store(hostID(ref)); Encoder envelope;
        envelope.value<uint32_t>(0x32415041); envelope.string(impl->modificationIDs.at(hostID(ref)).c_str());
        envelope.bytes.insert(envelope.bytes.end(), data.begin(), data.end()); bytes.assign(reinterpret_cast<const char*>(envelope.bytes.data()), envelope.bytes.size()); return true;
    } catch (...) { return false; }
}
bool Host::restoreModification(ARAAudioModificationRef ref, const void* data, size_t size) {
    try {
        if (!size) return impl->restore(hostID(ref), nullptr, 0);
        if (!data) return false;
        auto* p = static_cast<const uint8_t*>(data); std::vector<uint8_t> bytes(p, p + size); Decoder d{bytes};
        // The envelope adds ID remapping to archives exported by this helper.
        // Unwrapped SDK archives still belong to the plug-in's archive reader,
        // which decides whether it can restore or import their format.
        if (size < sizeof(uint32_t) || d.value<uint32_t>() != 0x32415041)
            return impl->restore(hostID(ref), data, size);
        auto archivedID = d.string();
        return impl->restore(hostID(ref), bytes.data() + d.offset, bytes.size() - d.offset, archivedID.c_str());
    } catch (...) { return false; }
}
} // namespace aap::ara
