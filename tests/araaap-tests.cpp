#include <aap/ara/araaap.hpp>
#include <ARA_Library/PlugIn/ARAPlug.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <csignal>
#include <execinfo.h>
#include <unistd.h>
using namespace ARA;
using namespace ARA::PlugIn;
using aap::ara::hostID;
using aap::ara::hostRef;

namespace {
std::thread::id modelThread;
bool sawAudio{}, sawTempo{}, sawRestore{}, sawClone{}, sawSelection{};
int contentNotifications{};
std::vector<uint8_t> expectedArchive(25000, 0xAB);
class Notes : public ARA::PlugIn::ContentReader {
    ARAContentNote note{440, 69, 0.8f, 2.0, 0.01, 1.0, 1.2};
public:
    ARAInt32 getEventCount() noexcept override { return 1; }
    const void* getDataForEvent(ARAInt32 index) noexcept override { assert(index == 0); return &note; }
};
class View : public EditorView {
public:
    using EditorView::EditorView;
    void doNotifySelection(const ViewSelection* selection) noexcept override { sawSelection = selection->getPlaybackRegions().size() == 1; }
};
class Controller : public DocumentController {
public:
    Controller(const PlugInEntry* e, const ARADocumentControllerHostInstance* h) noexcept : DocumentController(e, h) { modelThread = std::this_thread::get_id(); }
    void checkThread() { assert(modelThread == std::this_thread::get_id()); }
    void didEndEditing() noexcept override { checkThread(); }
    void didUpdateMusicalContextProperties(MusicalContext* context) noexcept override {
        checkThread(); assert(context->getOrderIndex() == 7);
        auto* c = getHostContentAccessController(); assert(c);
        assert(c->isMusicalContextContentAvailable(context->getHostRef(), kARAContentTypeTempoEntries));
        auto reader = c->createMusicalContextContentReader(context->getHostRef(), kARAContentTypeTempoEntries, nullptr);
        assert(reader); assert(c->getContentReaderEventCount(reader) == 2);
        auto* tempo = static_cast<const ARAContentTempoEntry*>(c->getContentReaderDataForEvent(reader, 1));
        sawTempo = tempo && tempo->timePosition == 2 && tempo->quarterPosition == 4; c->destroyContentReader(reader);
    }
    void doUpdateMusicalContextContent(MusicalContext*, const ARAContentTimeRange*, ContentUpdateScopes) noexcept override { checkThread(); }
    void doUpdateAudioSourceContent(AudioSource*, const ARAContentTimeRange*, ContentUpdateScopes) noexcept override { checkThread(); }
    void willEnableAudioSourceSamplesAccess(AudioSource*, bool) noexcept override { checkThread(); }
    void didEnableAudioSourceSamplesAccess(AudioSource* source, bool enabled) noexcept override {
        checkThread(); if (!enabled) return;
        HostAudioReader reader(source); float l[4]{}, r[4]{}; void* buffers[]{l, r};
        sawAudio = reader.readAudioSamples(3, 4, buffers) && l[0] == 3 && r[3] == 16;
    }
    AudioModification* doCreateAudioModification(AudioSource* source, ARAAudioModificationHostRef h, const AudioModification* clone) noexcept override {
        sawClone = sawClone || clone; return new AudioModification(source, h, clone);
    }
    bool doStoreObjectsToArchive(HostArchiveWriter* writer, const StoreObjectsFilter* filter) noexcept override {
        checkThread(); assert(!filter->getAudioModificationsToStore().empty());
        return writer->writeBytesToArchive(0, expectedArchive.size(), expectedArchive.data());
    }
    bool doRestoreObjectsFromArchive(HostArchiveReader* reader, const RestoreObjectsFilter* filter) noexcept override {
        checkThread(); assert(filter->getAudioModificationToRestoreStateWithID("original"));
        std::vector<uint8_t> data(reader->getArchiveSize());
        sawRestore = reader->readBytesFromArchive(0, data.size(), data.data()) && data == expectedArchive; return sawRestore;
    }
    bool doIsAudioSourceContentAvailable(const AudioSource*, ARAContentType type) noexcept override { return type == kARAContentTypeNotes; }
    ARAContentGrade doGetAudioSourceContentGrade(const AudioSource*, ARAContentType) noexcept override { return kARAContentGradeAdjusted; }
    ARA::PlugIn::ContentReader* doCreateAudioSourceContentReader(AudioSource*, ARAContentType, const ARAContentTimeRange*) noexcept override { return new Notes; }
    EditorView* doCreateEditorView() noexcept override { return new View(this); }
};
class Config : public FactoryConfig {
public:
    ARAAPIGeneration getHighestSupportedApiGeneration() const noexcept override { return kARAAPIGeneration_2_3_Final; }
    ARASize getAnalyzeableContentTypesCount() const noexcept override { return 1; }
    const ARAContentType* getAnalyzeableContentTypes() const noexcept override { static const ARAContentType types[]{kARAContentTypeNotes}; return types; }
    const char* getFactoryID() const noexcept override { return "org.aap.ara.test"; }
    const char* getPlugInName() const noexcept override { return "SDK integration test"; }
    const char* getManufacturerName() const noexcept override { return "AAP"; }
    const char* getInformationURL() const noexcept override { return "https://github.com/atsushieno/aap-ara"; }
    const char* getVersion() const noexcept override { return "1"; }
    const char* getDocumentArchiveID() const noexcept override { return "org.aap.ara.test.archive"; }
};
struct Environment {
    std::unique_ptr<ARA::PlugIn::PlugInExtension> binding = std::make_unique<ARA::PlugIn::PlugInExtension>();
    std::unique_ptr<aap::ara::PlugIn> adapter;
    aap::ara::Host* host{};
    AndroidAudioPlugin plugin{};
    AndroidAudioPluginHost companionHost{};
    ARAAAPPlugInExtension extension{
        [](AndroidAudioPlugin*) -> const ARAFactory* { return PlugInEntry::getPlugInEntry<Config, Controller>()->getFactory(); },
        [](AndroidAudioPlugin* p, ARADocumentControllerRef ref, ARAPlugInInstanceRoleFlags known, ARAPlugInInstanceRoleFlags assigned) {
            auto& e = *static_cast<Environment*>(p->plugin_specific); auto* result = e.binding->bindToARA(ref, known, assigned);
            e.binding->getEditorView()->setEditorOpen(true); return result;
        }};
    Environment() {
        plugin.plugin_specific = this; companionHost.context = this;
        companionHost.get_extension = [](AndroidAudioPluginHost* h, const char* uri) -> void* {
            auto& e = *static_cast<Environment*>(h->context);
            return std::strcmp(uri, ARA_AAP_TRANSPORT_EXTENSION_URI) == 0 && e.host ? e.host->transportExtension() : nullptr;
        };
        adapter = std::make_unique<aap::ara::PlugIn>(&plugin, &companionHost, &extension);
    }
    ~Environment() { adapter->prepareForRelease(); adapter->synchronize([this] { binding.reset(); }); }
};
}
int main() {
    std::signal(SIGABRT, [](int) { void* frames[40]; int n = backtrace(frames, 40); backtrace_symbols_fd(frames, n, STDERR_FILENO); _exit(1); });
    Environment env;
    ARAAudioAccessControllerInterface audio{sizeof(audio),
        [](ARAAudioAccessControllerHostRef, ARAAudioSourceHostRef source, ARABool wide) -> ARAAudioReaderHostRef { assert(hostID(source) == 30 && !wide); return hostRef<ARAAudioReaderHostRef>(1); },
        [](ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef, ARASamplePosition start, ARASampleCount count, void* const channels[]) -> ARABool {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < count; ++i) static_cast<float*>(channels[c])[i] = start + i + c * 10; return kARATrue;
        }, [](ARAAudioAccessControllerHostRef, ARAAudioReaderHostRef) {}};
    ARAContentAccessControllerInterface content{}; content.structSize = sizeof(content);
    content.isMusicalContextContentAvailable = [](ARAContentAccessControllerHostRef, ARAMusicalContextHostRef h, ARAContentType type) -> ARABool { assert(hostID(h) == 10); return type == kARAContentTypeTempoEntries; };
    content.getMusicalContextContentGrade = [](ARAContentAccessControllerHostRef, ARAMusicalContextHostRef, ARAContentType) -> ARAContentGrade { return kARAContentGradeAdjusted; };
    content.createMusicalContextContentReader = [](ARAContentAccessControllerHostRef, ARAMusicalContextHostRef, ARAContentType, const ARAContentTimeRange*) -> ARAContentReaderHostRef { return hostRef<ARAContentReaderHostRef>(77); };
    content.isAudioSourceContentAvailable = [](ARAContentAccessControllerHostRef, ARAAudioSourceHostRef, ARAContentType) -> ARABool { return kARAFalse; };
    content.getAudioSourceContentGrade = [](ARAContentAccessControllerHostRef, ARAAudioSourceHostRef, ARAContentType) -> ARAContentGrade { return kARAContentGradeInitial; };
    content.createAudioSourceContentReader = [](ARAContentAccessControllerHostRef, ARAAudioSourceHostRef, ARAContentType, const ARAContentTimeRange*) -> ARAContentReaderHostRef { return nullptr; };
    content.getContentReaderEventCount = [](ARAContentAccessControllerHostRef, ARAContentReaderHostRef) -> ARAInt32 { return 2; };
    content.getContentReaderDataForEvent = [](ARAContentAccessControllerHostRef, ARAContentReaderHostRef, ARAInt32 i) -> const void* { static ARAContentTempoEntry entries[]{{0, 0}, {2, 4}}; return &entries[i]; };
    content.destroyContentReader = [](ARAContentAccessControllerHostRef, ARAContentReaderHostRef) {};
    ARAModelUpdateControllerInterface updates{}; updates.structSize = sizeof(updates);
    updates.notifyAudioSourceAnalysisProgress = [](ARAModelUpdateControllerHostRef, ARAAudioSourceHostRef, ARAAnalysisProgressState, float) {};
    updates.notifyAudioSourceContentChanged = [](ARAModelUpdateControllerHostRef, ARAAudioSourceHostRef, const ARAContentTimeRange*, ARAContentUpdateFlags) {};
    updates.notifyAudioModificationContentChanged = [](ARAModelUpdateControllerHostRef, ARAAudioModificationHostRef h, const ARAContentTimeRange*, ARAContentUpdateFlags) { assert(hostID(h) == 40); ++contentNotifications; };
    updates.notifyPlaybackRegionContentChanged = [](ARAModelUpdateControllerHostRef, ARAPlaybackRegionHostRef, const ARAContentTimeRange*, ARAContentUpdateFlags) {};
    updates.notifyDocumentDataChanged = [](ARAModelUpdateControllerHostRef) {};
    ARADocumentControllerHostInstance services{sizeof(services), hostRef<ARAAudioAccessControllerHostRef>(1), &audio, nullptr, nullptr,
        hostRef<ARAContentAccessControllerHostRef>(1), &content, hostRef<ARAModelUpdateControllerHostRef>(1), &updates, nullptr, nullptr};
    ARADocumentProperties document{sizeof(document), "Real SDK test"};
    aap::ara::Host host(&env.plugin, env.adapter->transportExtension(), &services, &document); env.host = &host;
    assert(!std::strcmp(host.factory()->factoryID, "org.aap.ara.test"));
    auto* dc = host.documentController(); auto* api = dc->documentControllerInterface; auto ref = dc->documentControllerRef;
    auto roles = host.bind(kARAPlaybackRendererRole | kARAEditorViewRole, kARAPlaybackRendererRole | kARAEditorViewRole);
    api->beginEditing(ref);
    ARAMusicalContextProperties mc{sizeof(mc), "Song", 7, nullptr};
    auto context = api->createMusicalContext(ref, hostRef<ARAMusicalContextHostRef>(10), &mc);
    ARARegionSequenceProperties seq{sizeof(seq), "Track", 0, context, nullptr};
    auto sequence = api->createRegionSequence(ref, hostRef<ARARegionSequenceHostRef>(20), &seq);
    ARAAudioSourceProperties sp{sizeof(sp), "Source", "source", 128, 48000, 2, kARAFalse, kARAChannelArrangementUndefined, nullptr};
    auto source = api->createAudioSource(ref, hostRef<ARAAudioSourceHostRef>(30), &sp);
    ARAAudioModificationProperties mp{sizeof(mp), "Modification", "original"};
    auto modification = api->createAudioModification(ref, source, hostRef<ARAAudioModificationHostRef>(40), &mp);
    ARAPlaybackRegionProperties rp{sizeof(rp), 0, 0, 0.001, 0, 0.001, nullptr, sequence, "Region", nullptr};
    auto region = api->createPlaybackRegion(ref, modification, hostRef<ARAPlaybackRegionHostRef>(50), &rp);
    mp.persistentID = "duplicate";
    auto duplicate = api->cloneAudioModification(ref, modification, hostRef<ARAAudioModificationHostRef>(41), &mp);
    api->endEditing(ref);
    assert(sawTempo && sawClone);
    // Calls arriving on another Binder thread still reach the same SDK model thread.
    std::thread another([&] { api->enableAudioSourceSamplesAccess(ref, source, kARATrue); }); another.join(); assert(sawAudio);
    roles->playbackRendererInterface->addPlaybackRegion(roles->playbackRendererRef, region);
    ARAViewSelection selection{sizeof(selection), 1, &region, 0, nullptr, nullptr}; roles->editorViewInterface->notifySelection(roles->editorViewRef, &selection); assert(sawSelection);
    assert(api->isAudioSourceContentAvailable(ref, source, kARAContentTypeNotes));
    auto reader = api->createAudioSourceContentReader(ref, source, kARAContentTypeNotes, nullptr); assert(api->getContentReaderEventCount(ref, reader) == 1);
    auto* note = static_cast<const ARAContentNote*>(api->getContentReaderDataForEvent(ref, reader, 0)); assert(note && note->frequency == 440 && note->pitchNumber == 69 && note->startPosition == 2);
    api->destroyContentReader(ref, reader);
    std::string archive; assert(host.storeModification(modification, archive)); assert(archive.size() > 4096);
    api->beginEditing(ref); assert(host.restoreModification(duplicate, archive.data(), archive.size())); api->endEditing(ref); assert(sawRestore);
    // Archives without the helper's remapping envelope must reach the SDK reader.
    api->beginEditing(ref); assert(host.restoreModification(modification, expectedArchive.data(), expectedArchive.size())); api->endEditing(ref);
    env.adapter->synchronize([&] { auto* c = env.binding->getDocumentController<Controller>(); c->notifyAudioModificationContentChanged(c->getDocument()->getAudioSources()[0]->getAudioModifications()[0], ContentUpdateScopes::everythingIsAffected()); });
    assert(contentNotifications == 0); api->notifyModelUpdates(ref); assert(contentNotifications == 1);
    roles->playbackRendererInterface->removePlaybackRegion(roles->playbackRendererRef, region);
    api->enableAudioSourceSamplesAccess(ref, source, kARAFalse);
    api->beginEditing(ref); api->destroyPlaybackRegion(ref, region); api->destroyAudioModification(ref, duplicate); api->destroyAudioModification(ref, modification); api->destroyAudioSource(ref, source); api->destroyRegionSequence(ref, sequence); api->destroyMusicalContext(ref, context); api->endEditing(ref);
    std::cout << "ARA SDK factory, model thread, audio/content readers, roles, archives and updates passed\n";
}
