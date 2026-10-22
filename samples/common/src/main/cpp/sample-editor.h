#pragma once

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
struct Track { int32_t id; std::string name; };
struct Clip {
    int32_t id, track;
    std::string name;
    double start, offset, duration, frequency, gain;
    std::string pluginState;
};
std::string runScenario(aap::PluginClientConnectionList* connections,
        aap::PluginListSnapshot& plugins, const std::string& pluginId, int sampleRate);
std::string openEditor(aap::PluginClientConnectionList* connections,
        aap::PluginListSnapshot& plugins, const std::string& pluginId, int sampleRate);
int editorInstanceId();
void closeEditor();
std::string notifyEditorSelection(const std::vector<int32_t>& clips,
        const std::vector<int32_t>& tracks, bool hasRange, double start, double duration);
// Alternating clip ID / hex archive strings, drained on the editor worker.
std::vector<std::string> drainPluginUpdates();
std::string syncEditor(const std::string& title, const std::vector<Track>& tracks,
        const std::vector<Clip>& clips);
std::string verifyEditorAudio(int clipId, bool postChange, double modificationGain);
}
