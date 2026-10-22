#include "sample-host-services.h"
#include <atomic>
#include "aap-ara-plugin-sample.h"
#include <aap/ara/araaap.hpp>
#include <ARA_Library/PlugIn/ARAPlug.h>
#include <cassert>
#include <cstring>
#include <iostream>
#include <cmath>

using namespace ARA;

class GeneratedSource : public SampleAudioSource {
public:
    std::atomic<int> reads{};
    ARAChannelCount channelCount() const override { return 2; }
    ARASampleRate sampleRate() const override { return 48000; }
    ARASampleCount sampleCount() const override { return 48000; }
    bool readSamples(ARASamplePosition, ARASampleCount count, void* const buffers[], bool wide) override {
        ++reads;
        for (int c = 0; c < 2; ++c) for (ARASampleCount i = 0; i < count; ++i) {
            if (wide) static_cast<double*>(buffers[c])[i] = i % 2 ? 0.75 : 0.25;
            else static_cast<float*>(buffers[c])[i] = i % 2 ? 0.75f : 0.25f;
        }
        return true;
    }
};
class Services : public SampleHostServices, public ARA::Host::ArchivingControllerInterface {
    std::vector<ARAByte> archive;
public:
    ARASize getArchiveSize(ARAArchiveReaderHostRef) noexcept override { return archive.size(); }
    bool readBytesFromArchive(ARAArchiveReaderHostRef, ARASize position, ARASize count, ARAByte* data) noexcept override {
        if (position > archive.size() || count > archive.size() - position) return false;
        std::memcpy(data, archive.data() + position, count); return true;
    }
    bool writeBytesToArchive(ARAArchiveWriterHostRef, ARASize position, ARASize count, const ARAByte* data) noexcept override {
        archive.resize(position + count); std::memcpy(archive.data() + position, data, count); return true;
    }
    void notifyDocumentArchivingProgress(float) noexcept override {}
    void notifyDocumentUnarchivingProgress(float) noexcept override {}
    ARAPersistentID getDocumentArchiveID(ARAArchiveReaderHostRef) noexcept override { return "org.androidaudioplugin.ara.sample.archive/3"; }
public:
    int sourceIdentity{};
    int modificationChanges{};
    std::shared_ptr<GeneratedSource> provider = std::make_shared<GeneratedSource>();
    Services() { instance = Host::DocumentControllerHostInstance{this, this, nullptr, this}; registerAudioSource(reinterpret_cast<ARAAudioSourceHostRef>(&sourceIdentity), provider); }
    void notifyAudioModificationContentChanged(ARAAudioModificationHostRef ref,
            const ARAContentTimeRange* range, ContentUpdateScopes scopes) noexcept override {
        ++modificationChanges;
        SampleHostServices::notifyAudioModificationContentChanged(ref, range, scopes);
    }
};

int main() {
    Services services;
    aap::ara::Host* activeHost{};
    AndroidAudioPluginHost companionHost{};
    companionHost.context = &activeHost;
    companionHost.get_extension = [](AndroidAudioPluginHost* host, const char* uri) -> void* {
        auto* active = *static_cast<aap::ara::Host**>(host->context);
        return active && !std::strcmp(uri, ARA_AAP_TRANSPORT_EXTENSION_URI) ? active->transportExtension() : nullptr;
    };
    auto* factory = GetAndroidAudioPluginFactory();
    auto* plugin = factory->instantiate(factory, "sample-model-test", &companionHost);
    const auto token = sample_plugin::resolveInstance(plugin);
    assert(token);
    {
        auto* transport = static_cast<aap_ara_extension_t*>(plugin->get_extension(plugin, ARA_AAP_TRANSPORT_EXTENSION_URI));
        ARADocumentProperties properties{sizeof(properties), "SDK document"};
        aap::ara::Host host(plugin, transport, &services.instance, &properties);
        activeHost = &host;
        auto* roles = host.bind(kARAPlaybackRendererRole | kARAEditorViewRole, kARAPlaybackRendererRole | kARAEditorViewRole);
        Host::DocumentController controller(host.documentController());
        controller.beginEditing();
        ARAMusicalContextProperties contextProperties{sizeof(contextProperties), "Song", 0, nullptr};
        auto context = controller.createMusicalContext(reinterpret_cast<ARAMusicalContextHostRef>(&contextProperties), &contextProperties);
        ARARegionSequenceProperties sequenceProperties{sizeof(sequenceProperties), "SDK sequence", 0, context, nullptr};
        auto sequence = controller.createRegionSequence(reinterpret_cast<ARARegionSequenceHostRef>(&sequenceProperties), &sequenceProperties);
        ARAAudioSourceProperties sourceProperties{sizeof(sourceProperties), "SDK source", "sdk-source", 48000,
                48000, 2, kARAFalse, kARAChannelArrangementUndefined, nullptr};
        auto source = controller.createAudioSource(reinterpret_cast<ARAAudioSourceHostRef>(&services.sourceIdentity), &sourceProperties);
        ARAAudioModificationProperties modificationProperties{sizeof(modificationProperties), "SDK modification", "sdk-edit"};
        auto modificationHostRef = reinterpret_cast<ARAAudioModificationHostRef>(&modificationProperties);
        auto modification = controller.createAudioModification(source, modificationHostRef, &modificationProperties);
        ARAPlaybackRegionProperties regionProperties{sizeof(regionProperties), kARAPlaybackTransformationNoChanges,
                0, 0.1, 1, 0.1, nullptr, sequence, "SDK region", nullptr};
        auto regionHostRef = reinterpret_cast<ARAPlaybackRegionHostRef>(&regionProperties);
        auto region = controller.createPlaybackRegion(modification, regionHostRef, &regionProperties);
        controller.endEditing();
        controller.enableAudioSourceSamplesAccess(source, true);
        assert(services.provider->reads > 0);
        assert(host.factory()->analyzeableContentTypesCount == 1);
        assert(host.factory()->analyzeableContentTypes[0] == kARAContentTypeNotes);
        assert(controller.getProcessingAlgorithmsCount() == 2);
        auto firstVolume = [&](auto ref) {
            assert(controller.isAudioSourceContentAvailable(source, kARAContentTypeNotes));
            auto reader = controller.createAudioSourceContentReader(ref, kARAContentTypeNotes, nullptr);
            assert(reader && controller.getContentReaderEventCount(reader) == 1);
            auto note = *static_cast<const ARAContentNote*>(controller.getContentReaderDataForEvent(reader, 0));
            controller.destroyContentReader(reader);
            return note.volume;
        };
        assert(std::fabs(firstVolume(source) - std::sqrt(0.3125)) < 0.00001);
        controller.beginEditing(); controller.requestProcessingAlgorithmForAudioSource(source, 1); controller.endEditing();
        assert(controller.getProcessingAlgorithmForAudioSource(source) == 1);
        assert(std::fabs(firstVolume(source) - 0.75) < 0.00001);
        controller.enableAudioSourceSamplesAccess(source, false);
        controller.beginEditing();
        controller.updateAudioSourceContent(source, nullptr, kARAContentUpdateEverythingChanged);
        controller.endEditing();
        assert(!controller.isAudioSourceContentAvailable(source, kARAContentTypeNotes));
        controller.enableAudioSourceSamplesAccess(source, true);
        assert(std::fabs(firstVolume(source) - 0.75) < 0.00001);
        controller.notifyModelUpdates();

        // A native frontend sees the SDK's objects, with the host's actual refs.
        assert(sample_plugin::withDocument(token, [&](const PlugIn::Document& document) {
            assert(document.getRegionSequences().size() == 1);
            const auto* sdkSequence = document.getRegionSequences().front();
            assert(sdkSequence->getPlaybackRegions().size() == 1);
            const auto* sdkRegion = sdkSequence->getPlaybackRegions().front();
            assert(sdkRegion->getHostRef() == regionHostRef);
            assert(sdkRegion->getStartInPlaybackTime() == 1);
            assert(sdkRegion->getAudioModification()->getHostRef() == modificationHostRef);
        }));
        ARAViewSelection selection{sizeof(selection), 1, &region, 1, &sequence, nullptr};
        roles->editorViewInterface->notifySelection(roles->editorViewRef, &selection);
        auto snapshot = sample_plugin::snapshot(token);
        assert(snapshot.find("SDK region") != std::string::npos);
        assert(snapshot.find("\"selectionChanges\":1") != std::string::npos);
        roles->editorViewInterface->notifySelection(roles->editorViewRef, &selection);
        assert(sample_plugin::snapshot(token).find("\"selectionChanges\":2") != std::string::npos);
        assert(sample_plugin::setModificationGain(token, modificationHostRef, 0.5).empty());
        assert(services.modificationChanges == 0);
        bool receivedTypedUpdate = false;
        services.onUpdate = [&](SampleContentUpdate update) {
            auto* ref = std::get_if<ARAAudioModificationHostRef>(&update);
            receivedTypedUpdate = ref && *ref == modificationHostRef;
        };
        controller.notifyModelUpdates();
        assert(services.modificationChanges == 1 && receivedTypedUpdate);
        services.onUpdate = {};
        assert(sample_plugin::snapshot(token).find("\"modificationGain\":0.5") != std::string::npos);
        // Source-only persistency must preserve analysis before sample access,
        // and filtered restoration must map both source and modification IDs.
        const ARAStoreObjectsFilter store{sizeof(store), kARATrue, 1, &source, 1, &modification};
        assert(controller.storeObjectsToArchive(reinterpret_cast<ARAArchiveWriterHostRef>(&services), &store));
        controller.beginEditing();
        auto newSourceProperties = sourceProperties; newSourceProperties.persistentID = "remapped-source";
        auto restoredSource = controller.createAudioSource(reinterpret_cast<ARAAudioSourceHostRef>(&newSourceProperties), &newSourceProperties);
        auto newModificationProperties = modificationProperties; newModificationProperties.persistentID = "remapped-edit";
        auto restoredModification = controller.createAudioModification(restoredSource,
            reinterpret_cast<ARAAudioModificationHostRef>(&newModificationProperties), &newModificationProperties);
        const char* oldSourceID = "sdk-source"; const char* newSourceID = "remapped-source";
        const char* oldEditID = "sdk-edit"; const char* newEditID = "remapped-edit";
        const ARARestoreObjectsFilter restore{sizeof(restore), kARATrue, 1, &oldSourceID, &newSourceID, 1, &oldEditID, &newEditID};
        assert(controller.restoreObjectsFromArchive(reinterpret_cast<ARAArchiveReaderHostRef>(&services), &restore));
        controller.endEditing();
        assert(controller.getProcessingAlgorithmForAudioSource(restoredSource) == 1);
        assert(std::fabs(firstVolume(restoredSource) - 0.75) < 0.00001);
        auto noteReader = controller.createAudioModificationContentReader(restoredModification, kARAContentTypeNotes, nullptr);
        assert(controller.getContentReaderEventCount(noteReader) == 1);
        assert(std::fabs(static_cast<const ARAContentNote*>(controller.getContentReaderDataForEvent(noteReader, 0))->volume - 0.375) < 0.00001);
        controller.destroyContentReader(noteReader);
        controller.beginEditing(); controller.destroyAudioModification(restoredModification); controller.destroyAudioSource(restoredSource); controller.endEditing();

        controller.beginEditing();
        regionProperties.startInPlaybackTime = 3;
        controller.updatePlaybackRegionProperties(region, &regionProperties);
        controller.endEditing();
        assert(sample_plugin::snapshot(token).find("\"start\":3") != std::string::npos);
        controller.enableAudioSourceSamplesAccess(source, false);
        controller.beginEditing();
        controller.destroyPlaybackRegion(region);
        controller.destroyAudioModification(modification);
        controller.destroyAudioSource(source);
        controller.destroyRegionSequence(sequence);
        controller.destroyMusicalContext(context);
        controller.endEditing();
        snapshot = sample_plugin::snapshot(token);
        assert(snapshot.find("\"clips\":[]") != std::string::npos);
        assert(snapshot.find("\"sources\":[]") != std::string::npos);
        assert(snapshot.find("SDK region") == std::string::npos);
        assert(sample_plugin::withDocument(token, [](const PlugIn::Document& document) {
            assert(document.getRegionSequences().empty());
            assert(document.getAudioSources().empty());
        }));
    }
    activeHost = nullptr;
    factory->release(factory, plugin);
    assert(sample_plugin::snapshot(token) == "null");
    assert(!sample_plugin::withDocument(token, [](const PlugIn::Document&) { assert(false); }));
    assert(!sample_plugin::setModificationGain(token, nullptr, 1).empty());
    std::cout << "Sample native editor and Compose projection use the same SDK document\n";
}
