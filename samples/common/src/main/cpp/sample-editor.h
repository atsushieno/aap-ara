#pragma once

#include <ARA_API/ARAInterface.h>
#include <cstdint>
#include <string>
#include <vector>

namespace aap {
class PluginClientConnectionList;
class PluginListSnapshot;
}

// Call on one serialized worker thread. The connection list and plugin list
// must outlive the scenario/editor session; each frontend supplies its own.
namespace sample_editor {
std::string runScenario(aap::PluginClientConnectionList* connections,
        aap::PluginListSnapshot& plugins, const std::string& pluginId, int sampleRate);
std::string openEditor(aap::PluginClientConnectionList* connections,
        aap::PluginListSnapshot& plugins, const std::string& pluginId, int sampleRate);
int editorInstanceId();
void closeEditor();
// Existing host/editor code uses these standard SDK interfaces directly.
const ARA::ARADocumentControllerInstance* documentController();
const ARA::ARAPlugInExtensionInstance* plugInExtension();
ARA::ARASampleRate sampleRate();
ARA::ARARegionSequenceRef regionSequenceRef(ARA::ARARegionSequenceHostRef hostRef);
ARA::ARAPlaybackRegionRef playbackRegionRef(ARA::ARAPlaybackRegionHostRef hostRef);
std::string notifyEditorSelection(const ARA::ARAViewSelection& selection);
// Alternating clip ID / hex archive strings, drained on the editor worker.
std::vector<std::string> drainPluginUpdates();
std::string verifyEditorAudio(int clipId, bool postChange, double modificationGain);
}
