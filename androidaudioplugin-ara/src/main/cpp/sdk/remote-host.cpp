#include "aap/ara/remote-host.h"
#include "aap/ara/ARAAAP.h"
#include <cstring>
using namespace aap::ara;

ScopedRemotePluginInstanceARAHostContext::ScopedRemotePluginInstanceARAHostContext(
        aap_ara_host_extension_t* extension, aap::RemotePluginInstance& instance_)
        : instance(&instance_), previous_callback(instance_.getHostExtension) {
    auto previous = previous_callback;
    instance_.getHostExtension = [extension, previous](aap::RemotePluginInstance* instance, uint8_t urid, const char* uri) -> void* {
        if (uri && strcmp(uri, ARA_AAP_TRANSPORT_EXTENSION_URI) == 0) return extension;
        return previous ? previous(instance, urid, uri) : nullptr;
    };
}

ScopedRemotePluginInstanceARAHostContext::~ScopedRemotePluginInstanceARAHostContext() {
    if (instance)
        instance->getHostExtension = std::move(previous_callback);
}
