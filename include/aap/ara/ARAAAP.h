/* ARA companion API for Audio Plugins For Android.
 * Copyright 2026 Atsushi Eno. SPDX-License-Identifier: MIT
 * The ARA API included below is licensed separately under Apache-2.0.
 */
#ifndef ARA_AAP_H_INCLUDED
#define ARA_AAP_H_INCLUDED
#include <ARA_API/ARAInterface.h>
typedef struct AndroidAudioPlugin AndroidAudioPlugin;

#ifdef __cplusplus
extern "C" {
#endif

#define ARA_AAP_TRANSPORT_EXTENSION_URI "urn://androidaudioplugin.org/extensions/ara/v2"
#define ARA_AAP_PLUGIN_EXTENSION_URI "urn://androidaudioplugin.org/extensions/ara/plugin/2"

/* Returned by AndroidAudioPlugin.get_extension(ARA_AAP_PLUGIN_EXTENSION_URI).
 * These pointers are local to the plug-in process. Across Binder, use the
 * ARAAAP Host/PlugIn adapters, which translate references, never pointers.
 * The factory remains valid for the lifetime of the plug-in binary.
 * Bind once, before prepare/activate or opening an editor. The controller and
 * AAP instance may be destroyed in either order, as in the other companion APIs.
 */
typedef struct ARAAAPPlugInExtension {
    const ARA_NAMESPACE ARAFactory* (*getFactory)(AndroidAudioPlugin* plugin);
    const ARA_NAMESPACE ARAPlugInExtensionInstance* (*bindToDocumentController)(
        AndroidAudioPlugin* plugin,
        ARA_NAMESPACE ARADocumentControllerRef documentControllerRef,
        ARA_NAMESPACE ARAPlugInInstanceRoleFlags knownRoles,
        ARA_NAMESPACE ARAPlugInInstanceRoleFlags assignedRoles);
} ARAAAPPlugInExtension;

#ifdef __cplusplus
}
#endif
#endif
