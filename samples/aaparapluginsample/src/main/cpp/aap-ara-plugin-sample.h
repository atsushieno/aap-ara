#pragma once

#include <cstdint>
#include <string>

struct AndroidAudioPlugin;

namespace sample_plugin {
// Tokens are opaque and become unavailable when the host releases the instance.
uint64_t resolveSnapshot(AndroidAudioPlugin* plugin);
std::string snapshot(uint64_t token);
std::string setModificationGain(uint64_t token, int64_t id, double gain);
}
