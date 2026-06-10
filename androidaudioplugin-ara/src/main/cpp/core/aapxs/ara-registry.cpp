#include "aap/ara/registry.h"
#include "aap/core/aapxs/ara-aapxs.h"

namespace {
aap::xs::AAPXSDefinition_Ara ara_definition;
aap::xs::AAPXSDefinitionRegistry ara_registry{
        std::make_unique<aap::xs::UridMapping>(),
        std::vector<AAPXSDefinition>({ara_definition.asPublic()})
};
}

aap::xs::AAPXSDefinitionRegistry* aap::ara::getRegistry() {
    return &ara_registry;
}

void aap::ara::addToRegistry(aap::xs::AAPXSDefinitionRegistry& registry) {
    registry.add(ara_definition.asPublic(), ara_definition.asPublic().uri);
}
