#include "existing-ara-host.h"
#include <aap/ara/araaap.hpp>
#include "TestPlugIn/ARATestDocumentController.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>

using namespace ARA;

int main() {
    const auto* factory = ARATestDocumentController::getARAFactory();
    // Establish the baseline with an ordinary in-process ARA companion binding.
    {
        ExistingARAHostServices services;
        ARAInterfaceConfiguration configuration{sizeof(configuration), kARAAPIGeneration_2_3_Final, nullptr};
        factory->initializeARAWithConfiguration(&configuration);
        ARADocumentProperties properties{sizeof(properties), "Existing ARA host"};
        const auto* controller = factory->createDocumentControllerWithDocument(&services.instance, &properties);
        {
            PlugIn::PlugInExtension binding;
            const auto* roles = binding.bindToARA(controller->documentControllerRef,
                    kARAPlaybackRendererRole, kARAPlaybackRendererRole);
            exerciseExistingARAHost(controller, roles, services);
        }
        controller->documentControllerInterface->destroyDocumentController(controller->documentControllerRef);
        factory->uninitializeARA();
    }

    // The upstream ARATestDocumentController sources are compiled without edits.
    // Only the companion binding changes; the host workflow above is reused as-is.
    ExistingARAHostServices services;
    AndroidAudioPlugin plugin{};
    AndroidAudioPluginHost companionHost{};
    auto binding = std::make_unique<PlugIn::PlugInExtension>();
    plugin.plugin_specific = &binding;
    ARAAAPPlugInExtension extension{
        [](AndroidAudioPlugin*) { return ARATestDocumentController::getARAFactory(); },
        [](AndroidAudioPlugin* p, ARADocumentControllerRef ref, ARAPlugInInstanceRoleFlags known, ARAPlugInInstanceRoleFlags assigned) {
            return static_cast<std::unique_ptr<PlugIn::PlugInExtension>*>(p->plugin_specific)->get()->bindToARA(ref, known, assigned);
        }};
    aap::ara::Host* activeHost{};
    companionHost.context = &activeHost;
    companionHost.get_extension = [](AndroidAudioPluginHost* host, const char* uri) -> void* {
        auto* active = *static_cast<aap::ara::Host**>(host->context);
        return active && !std::strcmp(uri, ARA_AAP_TRANSPORT_EXTENSION_URI) ? active->transportExtension() : nullptr;
    };
    aap::ara::PlugIn adapter(&plugin, &companionHost, &extension);
    {
        ARADocumentProperties properties{sizeof(properties), "Existing ARA host over AAP"};
        aap::ara::Host host(&plugin, adapter.transportExtension(), &services.instance, &properties);
        activeHost = &host;
        exerciseExistingARAHost(host.documentController(),
                host.bind(kARAPlaybackRendererRole, kARAPlaybackRendererRole), services);
    }
    activeHost = nullptr;
    adapter.prepareForRelease();
    adapter.synchronize([&] { binding.reset(); });
    std::cout << "Unmodified upstream ARA plugin and identical SDK host code passed direct and AAP bindings\n";
}
