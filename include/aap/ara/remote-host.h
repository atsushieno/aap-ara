#ifndef AAP_ARA_REMOTE_HOST_H
#define AAP_ARA_REMOTE_HOST_H
#include <functional>
#include "aap/core/host/plugin-instance.h"
struct aap_ara_host_extension_t;
namespace aap::ara {
class ScopedRemotePluginInstanceARAHostContext {
    aap::RemotePluginInstance* instance{};
    std::function<void*(aap::RemotePluginInstance*, uint8_t, const char*)> previous_callback{};
public:
    ScopedRemotePluginInstanceARAHostContext(aap_ara_host_extension_t* extension, aap::RemotePluginInstance& instance);
    ~ScopedRemotePluginInstanceARAHostContext();
    ScopedRemotePluginInstanceARAHostContext(const ScopedRemotePluginInstanceARAHostContext&) = delete;
    ScopedRemotePluginInstanceARAHostContext& operator=(const ScopedRemotePluginInstanceARAHostContext&) = delete;
};
}
#endif
