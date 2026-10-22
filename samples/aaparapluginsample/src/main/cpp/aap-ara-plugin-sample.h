#pragma once

#include <ARA_API/ARAInterface.h>
#include <cstdint>
#include <functional>
#include <string>

struct AndroidAudioPlugin;
namespace ARA::PlugIn { class Document; }

namespace sample_plugin {
// An instance token identifies the companion binding, not a second ARA model.
uint64_t resolveInstance(AndroidAudioPlugin* plugin);
// Native editors read the ordinary SDK document on its model thread.
// Do not retain its objects after the callback returns.
bool withDocument(uint64_t token, const std::function<void(const ARA::PlugIn::Document&)>& action);
std::string setModificationGain(uint64_t token, ARA::ARAAudioModificationHostRef ref, double gain);
// Read-only JSON projection for the Compose frontend.
std::string snapshot(uint64_t token);
}
