#include "aap-ara-plugin-sample.h"
#include <aap/android-audio-plugin.h>
#include <memory>
#include <aap/ara/araaap.hpp>
#include <ARA_Library/PlugIn/ARAPlug.h>
#include <ARA_Library/Debug/ARAContentLogger.h>
#include <iomanip>
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
#include <vector>

struct SamplePluginContext;

namespace {
std::map<uint64_t, std::shared_ptr<SamplePluginContext>> editable_contexts;
struct PublishedModel {
    std::mutex mutex;
    std::string json = "{}", selection;
    uint64_t revision = 0;
};
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

using namespace ARA;
using namespace ARA::PlugIn;
using aap::ara::hostID;

class SampleDocumentController;

struct SamplePluginContext {
    std::unique_ptr<ARA::PlugIn::PlugInExtension> binding = std::make_unique<ARA::PlugIn::PlugInExtension>();
    std::unique_ptr<aap::ara::PlugIn> ara;
    std::recursive_mutex model_mutex;
    bool released = false;
    bool reverse_updates_supported = false;
    ARAAudioSourceHostRef analyzed_source{};
    std::atomic<double> diagnostic_gain{1.0};
    AndroidAudioPluginHost host;
    std::string plugin_id;
    std::array<int32_t, 2> audio_output_ports{0, 1};
    bool model_update_active{};
    std::array<float, 4> pre_change_output_l{};
    std::array<float, 4> pre_change_output_r{};
    std::array<float, 4> post_change_output_l{};
    std::array<float, 4> post_change_output_r{};
    std::string analysis_summary;
    std::mutex analysis_mutex;
    std::shared_ptr<PublishedModel> published = std::make_shared<PublishedModel>();
    uint64_t content_changes = 0;
    uint64_t selection_changes = 0;
    explicit SamplePluginContext(AndroidAudioPluginHost* host_, const char* id) : host(*host_), plugin_id(id ? id : "") {}
};

static SamplePluginContext* get_context(AndroidAudioPlugin* plugin) {
    return static_cast<std::shared_ptr<SamplePluginContext>*>(plugin->plugin_specific)->get();
}

struct SampleSource : AudioSource {
    using AudioSource::AudioSource;
    double rms{};
    int algorithm{};
    bool contentAvailable{};
    ARAContentGrade contentGrade = kARAContentGradeDetected;
    std::vector<ARAContentNote> notes;
    ARASamplePosition changedStart{};
    ARASampleCount changedCount{};
};
struct SampleModification : AudioModification {
    double gain = 1.0;
    SampleModification(AudioSource* source, ARAAudioModificationHostRef host, const AudioModification* clone) noexcept
        : AudioModification(source, host, clone), gain(clone ? static_cast<const SampleModification*>(clone)->gain : 1.0) {}
};
// Readers own their events, including transformed playback-region coordinates.
class SampleNoteReader : public ARA::PlugIn::ContentReader {
public:
    std::vector<ARAContentNote> notes;
    ARAInt32 getEventCount() noexcept override { return static_cast<ARAInt32>(notes.size()); }
    const void* getDataForEvent(ARAInt32 index) noexcept override {
        return index >= 0 && static_cast<size_t>(index) < notes.size() ? &notes[index] : nullptr;
    }
};
class SampleView;
class SampleDocumentController : public DocumentController {
public:
    std::weak_ptr<SamplePluginContext> companion;
    explicit SampleDocumentController(const PlugInEntry* entry, const ARADocumentControllerHostInstance* host) noexcept : DocumentController(entry, host) {}
    // The SDK document is the only model. JSON is a read-only projection for
    // Compose; an existing native editor can access the same SDK document.
    void sync(bool changed = true, bool documentDestroyed = false, bool selectionNotified = false) noexcept {
        auto c = companion.lock();
        if (!c) return;
        auto* document = documentDestroyed ? nullptr : getDocument();
        auto* view = c->binding->getEditorView();
        const auto* selection = document && view ? &view->getViewSelection() : nullptr;
        c->reverse_updates_supported = getHostModelUpdateController() != nullptr;
        auto& snapshot = *c->published;
        std::lock_guard<std::mutex> lock(snapshot.mutex);
        std::ostringstream selected;
        selected.precision(17);
        selected << "{\"clips\":[";
        bool comma = false;
        if (selection) for (auto* region : selection->getPlaybackRegions()) {
            if (comma) selected << ',';
            comma = true;
            selected << hostID(region->getHostRef());
        }
        selected << "],\"tracks\":[";
        comma = false;
        if (selection) for (auto* sequence : selection->getRegionSequences()) {
            if (comma) selected << ',';
            comma = true;
            selected << hostID(sequence->getHostRef());
        }
        selected << "],\"range\":";
        const ARAContentTimeRange* range = selection ? selection->getTimeRange() : nullptr;
        if (range) selected << "{\"start\":" << range->start << ",\"duration\":" << range->duration << '}';
        else selected << "null";
        selected << '}';
        if (selectionNotified || (!snapshot.selection.empty() && snapshot.selection != selected.str()))
            ++c->selection_changes;
        snapshot.selection = selected.str();
        if (changed) ++snapshot.revision;
        std::ostringstream out;
        out.precision(17);
        out << "{\"canEdit\":" << (c->reverse_updates_supported ? "true" : "false")
            << ",\"revision\":" << snapshot.revision << ",\"contentChanges\":" << c->content_changes
            << ",\"selectionChanges\":" << c->selection_changes << ",\"selection\":" << snapshot.selection
            << ",\"title\":" << json_string(document ? text(document->getName()) : "No ARA document");
        out << ",\"tracks\":[";
        comma = false;
        if (document) for (auto* sequence : document->getRegionSequences()) {
            if (comma) out << ',';
            comma = true;
            out << "{\"id\":" << hostID(sequence->getHostRef()) << ",\"name\":" << json_string(text(sequence->getName()))
                << ",\"order\":" << sequence->getOrderIndex() << '}';
        }
        out << "],\"clips\":[";
        comma = false;
        if (document) for (auto* sequence : document->getRegionSequences()) for (auto* region : sequence->getPlaybackRegions()) {
            if (comma) out << ',';
            comma = true;
            auto* modification = region->getAudioModification<SampleModification>();
            auto* source = modification->getAudioSource<SampleSource>();
            out << "{\"id\":" << hostID(region->getHostRef()) << ",\"track\":" << hostID(sequence->getHostRef())
                << ",\"name\":" << json_string(text(region->getName()))
                << ",\"start\":" << region->getStartInPlaybackTime() << ",\"offset\":" << region->getStartInAudioModificationTime()
                << ",\"duration\":" << region->getDurationInPlaybackTime() << ",\"sourceId\":" << hostID(source->getHostRef())
                << ",\"modificationId\":" << hostID(modification->getHostRef()) << ",\"modificationGain\":" << modification->gain
                << ",\"processedRms\":" << source->rms * modification->gain << '}';
        }
        out << "],\"sources\":[";
        comma = false;
        if (document) for (auto* source : document->getAudioSources<SampleSource>()) {
            if (comma) out << ',';
            comma = true;
            out << "{\"id\":" << hostID(source->getHostRef()) << ",\"name\":" << json_string(text(source->getName()))
                << ",\"sampleCount\":" << source->getSampleCount() << ",\"sampleRate\":" << source->getSampleRate()
                << ",\"access\":" << (source->isSampleAccessEnabled() ? "true" : "false")
                << ",\"changedStart\":" << source->changedStart << ",\"changedCount\":" << source->changedCount << '}';
            if (source->getHostRef() == c->analyzed_source && !source->getAudioModifications().empty())
                c->diagnostic_gain.store(source->getAudioModifications<SampleModification>().front()->gain);
        }
        std::lock_guard<std::mutex> analysis_lock(c->analysis_mutex);
        out << "],\"analysis\":" << json_string(c->analysis_summary) << '}';
        snapshot.json = out.str();
    }
    static const char* text(const OptionalProperty<ARAUtf8String>& value) { return value ? static_cast<const char*>(value) : ""; }
    bool setGain(ARAAudioModificationHostRef ref, double gain) {
        if (!getDocument()) return false;
        for (auto* source : getDocument()->getAudioSources()) for (auto* m : source->getAudioModifications<SampleModification>()) {
            if (m->getHostRef() != ref) continue;
            if (m->gain == gain) return true;
            m->gain = gain;
            // The SDK queues this until the host calls notifyModelUpdates().
            notifyAudioModificationContentChanged(m, ContentUpdateScopes(kARAContentUpdateEverythingChanged));
            if (auto c = companion.lock()) ++c->content_changes;
            sync(); return true;
        }
        return false;
    }
protected:
    void willBeginEditing() noexcept override { if (auto c = companion.lock()) c->model_update_active = true; }
    void didEndEditing() noexcept override { if (auto c = companion.lock()) c->model_update_active = false; sync(); }
    void willDestroyDocument(Document*) noexcept override {
        sync(true, true);
    }
    AudioSource* doCreateAudioSource(Document* doc, ARAAudioSourceHostRef host) noexcept override { return new SampleSource(doc, host); }
    AudioModification* doCreateAudioModification(AudioSource* source, ARAAudioModificationHostRef host, const AudioModification* clone) noexcept override { return new SampleModification(source, host, clone); }
    void doUpdateMusicalContextContent(MusicalContext* context, const ARAContentTimeRange* range, ContentUpdateScopes scopes) noexcept override {
        if (getHostContentAccessController())
            ARA::ContentLogger::logUpdatedContent(*getHostContentAccessController(), context->getHostRef(), range, scopes);
    }
    void doUpdateAudioSourceContent(AudioSource* source, const ARAContentTimeRange* range, ContentUpdateScopes scopes) noexcept override {
        auto* s = static_cast<SampleSource*>(source);
        if (scopes.affectNotes() || scopes.affectSamples()) { s->contentAvailable = false; s->notes.clear(); }
        s->changedStart = range ? std::llround(range->start * s->getSampleRate()) : 0;
        s->changedCount = range ? std::llround(range->duration * s->getSampleRate()) : s->getSampleCount();
        if (auto c = companion.lock()) ++c->content_changes;
        if (s->isSampleAccessEnabled()) analyze(s, true);
        copyHostNotes(s);
    }
    void willUpdateAudioSourceProperties(AudioSource* source, PropertiesPtr<ARAAudioSourceProperties> properties) noexcept override {
        if (source->getSampleCount() != properties->sampleCount || source->getSampleRate() != properties->sampleRate ||
                source->getChannelCount() != properties->channelCount) {
            auto* s = static_cast<SampleSource*>(source); s->contentAvailable = false; s->notes.clear();
        }
    }
    void willEnableAudioSourceSamplesAccess(AudioSource*, bool) noexcept override {}
    void didEnableAudioSourceSamplesAccess(AudioSource* source, bool enabled) noexcept override {
        if (enabled) { analyze(static_cast<SampleSource*>(source), false, true); copyHostNotes(static_cast<SampleSource*>(source)); }
        sync();
    }
    bool doIsAudioSourceContentAvailable(const AudioSource* source, ARAContentType type) noexcept override {
        return type == kARAContentTypeNotes && static_cast<const SampleSource*>(source)->contentAvailable;
    }
    ARAContentGrade doGetAudioSourceContentGrade(const AudioSource* source, ARAContentType) noexcept override {
        return static_cast<const SampleSource*>(source)->contentGrade;
    }
    ARA::PlugIn::ContentReader* doCreateAudioSourceContentReader(AudioSource* source, ARAContentType, const ARAContentTimeRange* range) noexcept override {
        return noteReader(static_cast<SampleSource*>(source), 1, nullptr, range);
    }
    bool doIsAudioModificationContentAvailable(const AudioModification* m, ARAContentType type) noexcept override {
        return doIsAudioSourceContentAvailable(m->getAudioSource(), type);
    }
    ARAContentGrade doGetAudioModificationContentGrade(const AudioModification* m, ARAContentType type) noexcept override {
        return doGetAudioSourceContentGrade(m->getAudioSource(), type);
    }
    ARA::PlugIn::ContentReader* doCreateAudioModificationContentReader(AudioModification* m, ARAContentType, const ARAContentTimeRange* range) noexcept override {
        return noteReader(static_cast<SampleSource*>(m->getAudioSource()), static_cast<SampleModification*>(m)->gain, nullptr, range);
    }
    bool doIsPlaybackRegionContentAvailable(const PlaybackRegion* r, ARAContentType type) noexcept override {
        return doIsAudioModificationContentAvailable(r->getAudioModification(), type);
    }
    ARAContentGrade doGetPlaybackRegionContentGrade(const PlaybackRegion* r, ARAContentType type) noexcept override {
        return doGetAudioModificationContentGrade(r->getAudioModification(), type);
    }
    ARA::PlugIn::ContentReader* doCreatePlaybackRegionContentReader(PlaybackRegion* r, ARAContentType, const ARAContentTimeRange* range) noexcept override {
        auto* m = static_cast<SampleModification*>(r->getAudioModification());
        return noteReader(static_cast<SampleSource*>(m->getAudioSource()), m->gain, r, range);
    }
    bool doIsAudioSourceContentAnalysisIncomplete(const AudioSource*, ARAContentType) noexcept override { return false; }
    void doRequestAudioSourceContentAnalysis(AudioSource* source, const std::vector<ARAContentType>&) noexcept override {
        auto* s = static_cast<SampleSource*>(source);
        if (s->isSampleAccessEnabled()) analyze(s, false);
        copyHostNotes(s);
        notifyAudioSourceContentChanged(s, ContentUpdateScopes::notesAreAffected());
    }
    ARAInt32 doGetProcessingAlgorithmsCount() noexcept override { return 2; }
    const ARAProcessingAlgorithmProperties* doGetProcessingAlgorithmProperties(ARAInt32 index) noexcept override {
        static const ARAProcessingAlgorithmProperties algorithms[]{
            {sizeof(ARAProcessingAlgorithmProperties), "org.androidaudioplugin.ara.sample.rms", "RMS level"},
            {sizeof(ARAProcessingAlgorithmProperties), "org.androidaudioplugin.ara.sample.peak", "Peak level"}};
        return &algorithms[index];
    }
    ARAInt32 doGetProcessingAlgorithmForAudioSource(const AudioSource* source) noexcept override { return static_cast<const SampleSource*>(source)->algorithm; }
    void doRequestProcessingAlgorithmForAudioSource(AudioSource* source, ARAInt32 index) noexcept override {
        auto* s = static_cast<SampleSource*>(source); s->algorithm = index;
        s->contentAvailable = false; s->notes.clear();
        if (s->isSampleAccessEnabled()) analyze(s, false);
        copyHostNotes(s);
        notifyAudioSourceContentChanged(s, ContentUpdateScopes::notesAreAffected());
    }
    bool doStoreObjectsToArchive(HostArchiveWriter* writer, const StoreObjectsFilter* filter) noexcept override {
        std::ostringstream out; out.precision(17); out << "ARAAAP-state-v3\n";
        for (auto* m : filter->getAudioModificationsToStore<SampleModification>())
            out << "M " << std::quoted(m->getPersistentID()) << ' ' << m->gain << '\n';
        for (auto* source : filter->getAudioSourcesToStore<SampleSource>()) {
            out << "S " << std::quoted(source->getPersistentID()) << ' ' << source->algorithm << ' '
                << int(source->contentGrade) << ' ' << source->contentAvailable << ' ' << source->notes.size() << '\n';
            for (auto& note : source->notes)
                out << note.frequency << ' ' << note.pitchNumber << ' ' << note.volume << ' ' << note.startPosition << ' '
                    << note.attackDuration << ' ' << note.noteDuration << ' ' << note.signalDuration << '\n';
        }
        auto bytes = out.str(); return writer->writeBytesToArchive(0, bytes.size(), reinterpret_cast<const ARAByte*>(bytes.data()));
    }
    bool doRestoreObjectsFromArchive(HostArchiveReader* reader, const RestoreObjectsFilter* filter) noexcept override {
        auto size = reader->getArchiveSize();
        if (!size) { // The sample's empty archive means default gain for the selected modification(s).
            for (auto* source : getDocument()->getAudioSources()) for (auto* m : source->getAudioModifications<SampleModification>())
                if (filter->getAudioModificationToRestoreStateWithID(m->getPersistentID().c_str())) m->gain = 1.0;
            sync(); return true;
        }
        std::string bytes(size, '\0'); if (!reader->readBytesFromArchive(0, size, reinterpret_cast<ARAByte*>(bytes.data()))) return false;
        // Projects saved before the SDK migration contain one opaque gain value,
        // without an SDK archive envelope or persistent ID. Import it through the
        // SDK's restore filter so the host's arrangement and saved edit both load.
        if (bytes.compare(0, 8, "gain-v1:") == 0) {
            char* end{};
            const char* start = bytes.c_str() + 8;
            const auto gain = std::strtod(start, &end);
            if (end == start || end != bytes.c_str() + bytes.size() || !std::isfinite(gain) || gain < 0 || gain > 2)
                return false;
            for (auto* source : getDocument()->getAudioSources())
                for (auto* m : source->getAudioModifications<SampleModification>())
                    if (filter->getAudioModificationToRestoreStateWithID(m->getPersistentID().c_str())) m->gain = gain;
            sync(); return true;
        }
        std::istringstream in(bytes); std::string header, line; std::getline(in, header);
        if (header == "ARAAAP-state-v3") return restoreState(in, filter);
        if (header != "ARAAAP-gain-v2") return false;
        std::vector<std::pair<SampleModification*, double>> changes;
        while (std::getline(in, line)) {
            auto tab = line.find('\t'); if (tab == std::string::npos) return false;
            char* end{}; auto gain = std::strtod(line.c_str() + tab + 1, &end);
            if (end != line.c_str() + line.size() || !std::isfinite(gain) || gain < 0 || gain > 2) return false;
            auto* m = filter->getAudioModificationToRestoreStateWithID<SampleModification>(line.substr(0, tab).c_str());
            if (m) changes.emplace_back(m, gain);
        }
        for (auto& entry : changes) entry.first->gain = entry.second;
        sync(); return true;
    }
    EditorView* doCreateEditorView() noexcept override;
private:
    bool restoreState(std::istream& in, const RestoreObjectsFilter* filter) {
        struct SourceChange { SampleSource* source; int algorithm, grade; bool available; std::vector<ARAContentNote> notes; };
        std::vector<SourceChange> sources;
        std::vector<std::pair<SampleModification*, double>> modifications;
        // Parse and validate before applying anything, including filtered-out objects.
        std::string tag, id;
        while (in >> tag) {
            if (!(in >> std::quoted(id))) return false;
            if (tag == "M") {
                double gain;
                if (!(in >> gain) || !std::isfinite(gain) || gain < 0 || gain > 2) return false;
                if (auto* m = filter->getAudioModificationToRestoreStateWithID<SampleModification>(id.c_str())) modifications.emplace_back(m, gain);
            } else if (tag == "S") {
                int algorithm, grade, available; size_t count;
                if (!(in >> algorithm >> grade >> available >> count) || algorithm < 0 || algorithm >= 2 ||
                        grade < kARAContentGradeInitial || grade > kARAContentGradeApproved || available < 0 || available > 1 || count > 1000000) return false;
                SourceChange state{filter->getAudioSourceToRestoreStateWithID<SampleSource>(id.c_str()), algorithm, grade, available != 0, {}};
                for (size_t i = 0; i < count; ++i) {
                    ARAContentNote n{};
                    if (!(in >> n.frequency >> n.pitchNumber >> n.volume >> n.startPosition >> n.attackDuration >> n.noteDuration >> n.signalDuration) ||
                        !std::isfinite(n.frequency) || !std::isfinite(n.volume) || !std::isfinite(n.startPosition) || !std::isfinite(n.attackDuration) ||
                        !std::isfinite(n.noteDuration) || !std::isfinite(n.signalDuration) || n.volume < 0 || n.volume > 1 ||
                        n.attackDuration < 0 || n.noteDuration < n.attackDuration || n.signalDuration < n.noteDuration) return false;
                    state.notes.push_back(n);
                }
                sources.push_back(std::move(state));
            } else return false;
        }
        if (!in.eof()) return false;
        for (auto& state : sources) if (state.source) {
            state.source->algorithm = state.algorithm; state.source->contentGrade = ARAContentGrade(state.grade);
            state.source->contentAvailable = state.available; state.source->notes = std::move(state.notes);
        }
        for (auto& change : modifications) change.first->gain = change.second;
        sync(); return true;
    }
    void copyHostNotes(SampleSource* source) noexcept {
        if (!getHostContentAccessController()) return;
        HostContentReader<kARAContentTypeNotes> reader(source);
        if (!reader || reader.getGrade() == kARAContentGradeInitial) return;
        source->notes.assign(reader.begin(), reader.end());
        source->contentGrade = reader.getGrade(); source->contentAvailable = true;
    }
    ARA::PlugIn::ContentReader* noteReader(const SampleSource* source, double gain, const PlaybackRegion* region, const ARAContentTimeRange* range) {
        auto* reader = new SampleNoteReader;
        for (auto note : source->notes) {
            if (region) {
                const auto sourceStart = region->getStartInAudioModificationTime();
                const auto start = std::max(note.startPosition, sourceStart);
                const auto end = std::min(note.startPosition + note.signalDuration, sourceStart + region->getDurationInAudioModificationTime());
                if (end <= start) continue;
                const auto scale = region->getDurationInPlaybackTime() / region->getDurationInAudioModificationTime();
                const auto noteEnd = std::min(note.startPosition + note.noteDuration, end);
                note.startPosition = region->getStartInPlaybackTime() + (start - sourceStart) * scale;
                note.attackDuration = 0; note.noteDuration = std::max(0.0, noteEnd - start) * scale;
                note.signalDuration = (end - start) * scale;
            }
            note.volume = std::clamp(float(note.volume * gain), 0.0f, 1.0f);
            if (!range || (note.startPosition < range->start + range->duration && note.startPosition + note.signalDuration > range->start))
                reader->notes.push_back(note);
        }
        return reader;
    }
    void analyze(SampleSource* source, bool post, bool preserveContent = false) noexcept {
        auto c = companion.lock();
        const auto count = std::min<ARASampleCount>(128, source->getSampleCount() - 64);
        if (count <= 0) return;
        std::array<float, 128> left{}, right{}; void* buffers[]{left.data(), right.data()};
        HostAudioReader reader(source);
        if (!reader.readAudioSamples(64, count, buffers)) {
            if (c) { std::lock_guard<std::mutex> lock(c->analysis_mutex); c->analysis_summary = "SDK audio reader failed"; }
            return;
        }
        auto metrics = [count](const auto& samples) {
            double sum = 0; float peak = 0;
            for (int i = 0; i < count; ++i) { auto v = samples[i]; sum += double(v) * v; peak = std::max(peak, std::fabs(v)); }
            return std::array<float, 4>{samples[0], samples[1], float(std::sqrt(sum / count)), peak};
        };
        auto l = metrics(left), r = metrics(right); source->rms = l[2];
        const double duration = source->getSampleCount() / source->getSampleRate();
        if (!preserveContent || !source->contentAvailable) {
            source->notes = {{kARAInvalidFrequency, kARAInvalidPitchNumber, std::clamp(l[source->algorithm == 0 ? 2 : 3], 0.0f, 1.0f), 0, 0, duration, duration}};
            source->contentGrade = kARAContentGradeDetected; source->contentAvailable = true;
        }
        if (!c) return; // ARA documents can exist before a companion instance is bound.
        c->analyzed_source = source->getHostRef();
        std::lock_guard<std::mutex> lock(c->analysis_mutex);
        if (post) { c->post_change_output_l = l; c->post_change_output_r = r; } else { c->pre_change_output_l = l; c->pre_change_output_r = r; }
        c->analysis_summary = std::string("ARA SDK ") + (post ? "post-change" : "pre-change") + " sample read";
    }
};
class SampleView : public EditorView {
public:
    using EditorView::EditorView;
protected:
    void doNotifySelection(const ViewSelection*) noexcept override {
        // EditorView owns and prunes the standard SDK ViewSelection.
        getDocumentController<SampleDocumentController>()->sync(false, false, true);
    }
};
EditorView* SampleDocumentController::doCreateEditorView() noexcept { return new SampleView(this); }
class SampleFactoryConfig : public FactoryConfig {
public:
    ARAAPIGeneration getLowestSupportedApiGeneration() const noexcept override { return kARAAPIGeneration_2_0_Final; }
    ARAAPIGeneration getHighestSupportedApiGeneration() const noexcept override { return kARAAPIGeneration_2_3_Final; }
    const char* getFactoryID() const noexcept override { return "org.androidaudioplugin.ara.sample.factory"; }
    const char* getPlugInName() const noexcept override { return "AAP ARA SDK Sample"; }
    const char* getManufacturerName() const noexcept override { return "AAP"; }
    const char* getInformationURL() const noexcept override { return "https://github.com/atsushieno/aap-ara"; }
    const char* getVersion() const noexcept override { return "0.1.0"; }
    const char* getDocumentArchiveID() const noexcept override { return "org.androidaudioplugin.ara.sample.archive/3"; }
    ARASize getCompatibleDocumentArchiveIDsCount() const noexcept override { return 1; }
    const ARAPersistentID* getCompatibleDocumentArchiveIDs() const noexcept override {
        static const ARAPersistentID ids[]{"org.androidaudioplugin.ara.sample.archive/2"}; return ids;
    }
    ARASize getAnalyzeableContentTypesCount() const noexcept override { return 1; }
    const ARAContentType* getAnalyzeableContentTypes() const noexcept override {
        static const ARAContentType types[]{kARAContentTypeNotes}; return types;
    }
};
static ARAAAPPlugInExtension sdkExtension{
    [](AndroidAudioPlugin*) -> const ARAFactory* { return PlugInEntry::getPlugInEntry<SampleFactoryConfig, SampleDocumentController>()->getFactory(); },
    [](AndroidAudioPlugin* plugin, ARADocumentControllerRef ref, ARAPlugInInstanceRoleFlags known, ARAPlugInInstanceRoleFlags assigned) {
        auto context = *static_cast<std::shared_ptr<SamplePluginContext>*>(plugin->plugin_specific);
        auto* result = context->binding->bindToARA(ref, known, assigned);
        auto* dc = context->binding->getDocumentController<SampleDocumentController>(); dc->companion = context;
        if (auto* view = context->binding->getEditorView()) view->setEditorOpen(true);
        dc->sync(false); return result;
    }
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

static void* sample_get_extension(AndroidAudioPlugin* plugin, const char* uri) {
    return get_context(plugin)->ara->getExtension(uri);
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
    context->ara = std::make_unique<aap::ara::PlugIn>(plugin, &context->host, &sdkExtension);
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
    context->ara->prepareForRelease();
    context->ara->synchronize([&] { context->binding.reset(); });
    context->ara.reset();
    delete static_cast<std::shared_ptr<SamplePluginContext>*>(instance->plugin_specific);
    delete instance;
}

static AndroidAudioPluginFactory factory{
        sample_instantiate,
        sample_release,
        nullptr
};

extern "C" AndroidAudioPluginFactory* GetAndroidAudioPluginFactory() {
    return &factory;
}


uint64_t sample_plugin::resolveInstance(AndroidAudioPlugin* plugin) {
    std::lock_guard<std::mutex> lock(snapshots_mutex);
    auto found = plugin_snapshot_tokens.find(plugin);
    return found == plugin_snapshot_tokens.end() ? 0 : found->second;
}

std::string sample_plugin::snapshot(uint64_t token) {
    std::shared_ptr<PublishedModel> model;
    {
        std::lock_guard<std::mutex> lock(snapshots_mutex);
        auto found = snapshots.find(token);
        if (found == snapshots.end()) return "null";
        model = found->second;
    }
    std::lock_guard<std::mutex> lock(model->mutex);
    return model->json;
}

std::string sample_plugin::setModificationGain(uint64_t token, ARAAudioModificationHostRef ref, double gain) {
    std::shared_ptr<SamplePluginContext> context;
    { const std::lock_guard<std::mutex> lock(snapshots_mutex); auto found = editable_contexts.find(token); if (found == editable_contexts.end()) return "The host has released this instance."; context = found->second; }
    const std::lock_guard<std::recursive_mutex> lifetime(context->model_mutex);
    if (context->released || !context->ara) return "The host has released this instance.";
    std::string error;
    context->ara->synchronize([&] {
        if (context->released) { error = "The host has released this instance."; return; }
        if (context->model_update_active || !std::isfinite(gain) || gain < 0 || gain > 2) { error = "Invalid modification edit"; return; }
        auto* dc = context->binding->getDocumentController<SampleDocumentController>();
        if (!dc || !context->reverse_updates_supported || !dc->setGain(ref, gain)) error = "Modification unavailable";
    });
    return error;
}

bool sample_plugin::withDocument(uint64_t token,
        const std::function<void(const ARA::PlugIn::Document&)>& action) {
    std::shared_ptr<SamplePluginContext> context;
    {
        const std::lock_guard<std::mutex> lock(snapshots_mutex);
        auto found = editable_contexts.find(token);
        if (found == editable_contexts.end()) return false;
        context = found->second;
    }
    const std::lock_guard<std::recursive_mutex> lifetime(context->model_mutex);
    if (context->released || !context->ara) return false;
    bool available = false;
    context->ara->synchronize([&] {
        auto* controller = context->binding->getDocumentController<SampleDocumentController>();
        if (!controller || !controller->getDocument()) return;
        action(*controller->getDocument());
        available = true;
    });
    return available;
}
