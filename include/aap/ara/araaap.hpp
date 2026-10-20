#ifndef AAP_ARA_SDK_INTEGRATION_HPP
#define AAP_ARA_SDK_INTEGRATION_HPP
#include "ARAAAP.h"
#include <aap/android-audio-plugin.h>
struct aap_ara_extension_t;
struct aap_ara_host_extension_t;
#include <functional>
#include <memory>
#include <string>

namespace aap::ara {
// Wire identities are distinct from SDK references. These helpers are useful
// for the sample UI's stable integer identities; adapters never dereference them.
template<class Ref> Ref hostRef(int64_t id) { return reinterpret_cast<Ref>(static_cast<intptr_t>(id)); }
template<class Ref> int64_t hostID(Ref ref) { return static_cast<int64_t>(reinterpret_cast<intptr_t>(ref)); }

// Plug-in process adapter. The application supplies its ordinary SDK factory and
// companion binding callback. All model behavior belongs to that SDK controller.
class PlugIn {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    PlugIn(AndroidAudioPlugin* plugin, AndroidAudioPluginHost* host, const ARAAAPPlugInExtension* extension);
    ~PlugIn();
    // Call while the application binding is alive, then destroy that binding
    // through synchronize(), then release the adapter. Safe after host teardown.
    void prepareForRelease();
    PlugIn(const PlugIn&) = delete;
    PlugIn& operator=(const PlugIn&) = delete;
    void* getExtension(const char* uri);
    aap_ara_extension_t* transportExtension();
    void synchronize(const std::function<void()>& action);
};

// Host process adapter. Factory metadata and SDK interfaces are local proxies;
// no SDK pointer is sent over Binder. The supplied host services are borrowed
// until destruction. Model calls must be serialized on the host's model worker.
class Host {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Host(AndroidAudioPlugin* plugin, const ARA::ARADocumentControllerHostInstance* host,
         const ARA::ARADocumentProperties* properties,
         ARA::ARAAPIGeneration generation = ARA::kARAAPIGeneration_2_3_Final);
    // Explicit transport overload for a worker-owned AAPXS proxy or local tests.
    Host(AndroidAudioPlugin* plugin, aap_ara_extension_t* transport,
         const ARA::ARADocumentControllerHostInstance* host,
         const ARA::ARADocumentProperties* properties,
         ARA::ARAAPIGeneration generation = ARA::kARAAPIGeneration_2_3_Final);
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;
    const ARA::ARAFactory* factory() const;
    const ARA::ARADocumentControllerInstance* documentController() const;
    const ARA::ARAPlugInExtensionInstance* bind(ARA::ARAPlugInInstanceRoleFlags known, ARA::ARAPlugInInstanceRoleFlags assigned);
    aap_ara_host_extension_t* transportExtension();
    // Sample convenience: uses SDK store/restore filters and the actual SDK
    // archive callbacks. Empty state restores the application's default state.
    bool storeModification(ARA::ARAAudioModificationRef ref, std::string& bytes);
    bool restoreModification(ARA::ARAAudioModificationRef ref, const void* data, size_t size);
};
}
#endif
