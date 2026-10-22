#pragma once

#include <ARA_API/ARAInterface.h>
#include <string>
#include <utility>
#include <vector>

// Compose project synchronization only. Existing native ARA host code can use
// sample_editor::documentController() and plugInExtension() without this helper.
namespace sample_project {
using RegionSequence = std::pair<ARA::ARARegionSequenceHostRef, ARA::ARARegionSequenceProperties>;
struct PlaybackRegion {
    ARA::ARAPlaybackRegionHostRef hostRef;
    ARA::ARARegionSequenceHostRef sequenceHostRef;
    ARA::ARAAudioSourceProperties source;
    ARA::ARAAudioModificationProperties modification;
    ARA::ARAPlaybackRegionProperties properties;
    // Sample-owned generated audio and opaque archive, outside the ARA model.
    double frequency, gain;
    std::string pluginState;
};
// Property pointers (names, persistent IDs, etc.) are borrowed only for this call.
std::string sync(const ARA::ARADocumentProperties& document,
        const std::vector<RegionSequence>& sequences, const std::vector<PlaybackRegion>& regions);
}
