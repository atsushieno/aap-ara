#ifndef AAP_ARA_REGISTRY_H
#define AAP_ARA_REGISTRY_H

#include "aap/core/aapxs/aapxs-hosting-runtime.h"

namespace aap::ara {
    aap::xs::AAPXSDefinitionRegistry* getRegistry();
    void addToRegistry(aap::xs::AAPXSDefinitionRegistry& registry);
}

#endif // AAP_ARA_REGISTRY_H
