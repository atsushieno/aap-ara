#pragma once
#include <aap/ara/araaap.hpp>
#include <aap/ara/registry.h>
#include <aap/ara/remote-host.h>
#include <aap/core/aapxs/ara-aapxs.h>
#include <aap/core/host/plugin-host.h>
#include "model-thread-transport.h"
#include "TestHost.h"
#include <map>
#include <stdexcept>

namespace sample_scenarios {
struct RemoteDocument {
    aap::PluginClient& client;
    aap::RemotePluginInstance* instance{};
    std::unique_ptr<aap::xs::AraClientAAPXS> transport;
    std::unique_ptr<ModelThreadTransport> modelTransport;
    std::unique_ptr<aap::ara::Host> sdk;
    std::unique_ptr<aap::ara::ScopedRemotePluginInstanceARAHostContext> bridge;
    ARA::ARADocumentControllerInterface api{};
    ARA::ARADocumentControllerInstance controller{};
    explicit RemoteDocument(aap::PluginClient& client) : client(client) {}
    ~RemoteDocument() {
        sdk.reset(); bridge.reset(); transport.reset();
        if (instance) client.destroyInstance(instance);
    }
};
// All native host operations run on the single Kotlin worker, including destruction.
inline thread_local std::map<ARA::ARADocumentControllerRef, const ARA::ARAFactory*> factories;
class AAPEntry : public PlugInEntry {
    std::string pluginID;
    aap::PluginListSnapshot list = aap::PluginListSnapshot::queryServices();
    std::unique_ptr<aap::PluginClient> client;
    std::vector<std::unique_ptr<RemoteDocument>> documents;
public:
    AAPEntry(aap::PluginClientConnectionList* connections, std::string pluginID) : PlugInEntry("Android AAP"), pluginID(std::move(pluginID)) {
        auto* registry = aap::xs::AAPXSDefinitionRegistry::getStandardExtensions();
        aap::ara::addToRegistry(*registry);
        client = std::make_unique<aap::PluginClient>(connections, &list, registry);
        // Use the real upstream host to obtain factory metadata and validate our binding.
        TestHost bootstrap;
        bootstrap.addDocument("Factory probe", this);
    }
    ~AAPEntry() override {
        for (auto& d : documents) factories.erase(d->controller.documentControllerRef);
    }
    bool usesIPC() const override { return true; }
    void initializeARA(ARA::ARAAssertFunction*) override {}
    void uninitializeARA() override {}
    const ARA::ARADocumentControllerInstance* createDocumentControllerWithDocument(
            const ARA::ARADocumentControllerHostInstance* services, const ARA::ARADocumentProperties* properties) override {
        auto d = std::make_unique<RemoteDocument>(*client);
        auto result = client->createInstance(pluginID, true);
        if (!result.error.empty()) throw std::runtime_error(result.error);
        d->instance = dynamic_cast<aap::RemotePluginInstance*>(client->getInstanceById(result.value));
        if (!d->instance) throw std::runtime_error("No AAP instance");
        auto* initiator = d->instance->getAAPXSDispatcher().getPluginAAPXSByUri(AAP_ARA_EXTENSION_URI);
        if (!initiator) throw std::runtime_error("Missing ARA extension");
        d->transport = std::make_unique<aap::xs::AraClientAAPXS>(initiator, initiator->serialization);
        d->modelTransport = std::make_unique<ModelThreadTransport>(d->transport->asPluginExtension());
        d->sdk = std::make_unique<aap::ara::Host>(d->instance->getPlugin(), &d->modelTransport->wire, services, properties);
        d->modelTransport->attach(d->sdk->transportExtension());
        d->bridge = std::make_unique<aap::ara::ScopedRemotePluginInstanceARAHostContext>(&d->modelTransport->reverse, *d->instance);
        if (!getARAFactory()) validateAndSetFactory(d->sdk->factory());
        // ARA factories describe a binary, while AAP transport proxies belong to each
        // instance. Normalize factory identity for the upstream host's equality check.
        d->api = *d->sdk->documentController()->documentControllerInterface;
        d->api.getFactory = [](ARA::ARADocumentControllerRef ref) { return factories.at(ref); };
        d->controller = *d->sdk->documentController(); d->controller.documentControllerInterface = &d->api;
        factories[d->controller.documentControllerRef] = getARAFactory();
        auto* resultController = &d->controller;
        documents.push_back(std::move(d));
        return resultController;
    }
    std::unique_ptr<PlugInInstance> createPlugInInstance() override {
        throw std::runtime_error("Use the document's AAP binding for Android rendering and views");
    }
    RemoteDocument& remote(Document* document, TestHost& host) {
        auto ref = host.getDocumentController(document)->getDocumentController()->getRef();
        for (auto& d : documents) if (d->controller.documentControllerRef == ref) return *d;
        throw std::runtime_error("Unknown document");
    }
};
}
