#include "existing-ara-host.h"
#include <cassert>
#include <chrono>
#include <cstring>
#include <thread>

using namespace ARA;

ARAAudioReaderHostRef ExistingARAHostServices::createAudioReaderForSource(ARAAudioSourceHostRef ref, bool wide) noexcept {
    assert(ref == reinterpret_cast<ARAAudioSourceHostRef>(&sourceIdentity));
    assert(!wide);
    return reinterpret_cast<ARAAudioReaderHostRef>(this);
}
bool ExistingARAHostServices::readAudioSamples(ARAAudioReaderHostRef ref, ARASamplePosition start,
        ARASampleCount count, void* const channels[]) noexcept {
    assert(ref == reinterpret_cast<ARAAudioReaderHostRef>(this));
    ++audioReads;
    for (int c = 0; c < 2; ++c) for (ARASampleCount i = 0; i < count; ++i)
        static_cast<float*>(channels[c])[i] = start + i < 2400 ? 0.5f : 0.0f;
    return true;
}
void ExistingARAHostServices::destroyAudioReader(ARAAudioReaderHostRef ref) noexcept {
    assert(ref == reinterpret_cast<ARAAudioReaderHostRef>(this));
}
ARASize ExistingARAHostServices::getArchiveSize(ARAArchiveReaderHostRef) noexcept { return archive.size(); }
bool ExistingARAHostServices::readBytesFromArchive(ARAArchiveReaderHostRef, ARASize position,
        ARASize length, ARAByte data[]) noexcept {
    if (position > archive.size() || length > archive.size() - position) return false;
    std::memcpy(data, archive.data() + position, length);
    return true;
}
bool ExistingARAHostServices::writeBytesToArchive(ARAArchiveWriterHostRef, ARASize position,
        ARASize length, const ARAByte data[]) noexcept {
    archive.resize(position + length);
    std::memcpy(archive.data() + position, data, length);
    return true;
}

// The exact same SDK host code runs against both companion bindings.
void exerciseExistingARAHost(const ARADocumentControllerInstance* instance,
        const ARAPlugInExtensionInstance* binding, ExistingARAHostServices& services) {
    Host::DocumentController controller(instance);
    Host::PlaybackRenderer renderer(binding);
    assert(std::strcmp(controller.getFactory()->plugInName, "ARATestPlugIn") == 0);
    services.archiveID = controller.getFactory()->documentArchiveID;
    controller.beginEditing();
    ARAMusicalContextProperties contextProperties{sizeof(contextProperties), "Existing song", 0, nullptr};
    auto context = controller.createMusicalContext(reinterpret_cast<ARAMusicalContextHostRef>(&contextProperties), &contextProperties);
    ARARegionSequenceProperties sequenceProperties{sizeof(sequenceProperties), "Existing track", 0, context, nullptr};
    auto sequence = controller.createRegionSequence(reinterpret_cast<ARARegionSequenceHostRef>(&sequenceProperties), &sequenceProperties);
    ARAAudioSourceProperties sourceProperties{sizeof(sourceProperties), "Existing source", "existing-source", 4800,
            48000, 2, kARAFalse, kARAChannelArrangementUndefined, nullptr};
    auto source = controller.createAudioSource(reinterpret_cast<ARAAudioSourceHostRef>(&services.sourceIdentity), &sourceProperties);
    ARAAudioModificationProperties modificationProperties{sizeof(modificationProperties), "Existing edit", "existing-edit"};
    auto modification = controller.createAudioModification(source,
            reinterpret_cast<ARAAudioModificationHostRef>(&modificationProperties), &modificationProperties);
    ARAPlaybackRegionProperties regionProperties{sizeof(regionProperties), kARAPlaybackTransformationNoChanges,
            0, 0.1, 1, 0.1, nullptr, sequence, "Existing region", nullptr};
    auto region = controller.createPlaybackRegion(modification,
            reinterpret_cast<ARAPlaybackRegionHostRef>(&regionProperties), &regionProperties);
    controller.endEditing();
    renderer.addPlaybackRegion(region);
    controller.enableAudioSourceSamplesAccess(source, true);
    ARAContentType type = kARAContentTypeNotes;
    controller.requestAudioSourceContentAnalysis(source, 1, &type);
    for (int attempt = 0; attempt < 200 && controller.isAudioSourceContentAnalysisIncomplete(source, type); ++attempt) {
        controller.notifyModelUpdates();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(!controller.isAudioSourceContentAnalysisIncomplete(source, type));
    assert(services.audioReads > 0);
    assert(controller.isAudioSourceContentAvailable(source, type));
    auto reader = controller.createAudioSourceContentReader(source, type, nullptr);
    assert(reader && controller.getContentReaderEventCount(reader) > 0);
    auto* note = static_cast<const ARAContentNote*>(controller.getContentReaderDataForEvent(reader, 0));
    assert(note && note->volume > 0);
    controller.destroyContentReader(reader);
    assert(controller.storeObjectsToArchive(reinterpret_cast<ARAArchiveWriterHostRef>(&services.archive), nullptr));
    assert(!services.archive.empty());
    controller.beginEditing();
    assert(controller.restoreObjectsFromArchive(reinterpret_cast<ARAArchiveReaderHostRef>(&services.archive), nullptr));
    regionProperties.startInPlaybackTime = 2;
    controller.updatePlaybackRegionProperties(region, &regionProperties);
    controller.endEditing();
    renderer.removePlaybackRegion(region);
    controller.enableAudioSourceSamplesAccess(source, false);
    controller.beginEditing();
    controller.destroyPlaybackRegion(region);
    controller.destroyAudioModification(modification);
    controller.destroyAudioSource(source);
    controller.destroyRegionSequence(sequence);
    controller.destroyMusicalContext(context);
    controller.endEditing();
}
