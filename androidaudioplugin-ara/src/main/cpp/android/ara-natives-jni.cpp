#include <jni.h>
#include "aap/ara/registry.h"
#include "aap/core/aapxs/standard-extensions.h"

extern "C"
JNIEXPORT void JNICALL
Java_org_androidaudioplugin_ara_AraAudioPluginNatives_installAraExtensions(JNIEnv*, jclass) {
    aap::ara::addToRegistry(*aap::xs::AAPXSDefinitionRegistry::getStandardExtensions());
}
