#pragma once
#include <string>
namespace aap { class PluginClientConnectionList; }
namespace sample_scenarios {
std::string runUpstreamScenarios(aap::PluginClientConnectionList* connections, const std::string& pluginID);
}
