# ARAAAP: ARA SDK integration for Audio Plugins For Android (AI slop)

ARAAAP is the AAP companion integration for Celemony's ARA SDK. It exposes the
upstream `ARAFactory`, `ARADocumentControllerInstance`, and
`ARAPlugInExtensionInstance` contracts, with AAPXS adapters for Android's separate
host and plug-in processes. Applications implement the ARA SDK interfaces; they
do not implement a second document-model API.

The SDK is a recursive submodule at `external/ARA_SDK`, pinned to the public
`releases/2.3.0` tag (`0005c53`). Its `ARA_API`, `ARA_Library`, and `ARA_Examples`
submodules retain their upstream licenses. We use the released ARA 2 interfaces.
The SDK's experimental IPC code is not part of this tagged release, so ARAAAP
provides its own Android transport adapters.

## Getting the SDK and building

```sh
git submodule update --init --recursive
./gradlew :samples:aaparahostsample:assembleDebug :samples:aaparapluginsample:assembleDebug
```

The native modules include `cmake/ARASDK.cmake`, which adds the SDK's CMake targets.
`-DARA_SDK_ROOT=/absolute/path/to/ARA_SDK` can select an existing SDK checkout.
CMake never downloads a moving branch. The Android package builds `arm64-v8a` and
`x86_64`: the upstream SDK does not define a 32-bit ARM ABI.

The AAR's Prefab module exports ARAAAP headers, `ARA_API/ARAInterface.h`, and the
upstream API license. A C API implementation can consume those headers directly:

```cmake
find_package(androidaudioplugin REQUIRED CONFIG)
find_package(androidaudioplugin-ara REQUIRED CONFIG)
target_link_libraries(my_plugin PRIVATE
    androidaudioplugin::androidaudioplugin
    androidaudioplugin-ara::androidaudioplugin-ara)
```

For the SDK's C++ implementation helpers, also add your SDK checkout and link its
plug-in library, as the sample does:

```cmake
set(ARA_API_DIR "${ARA_SDK_ROOT}/ARA_API")
add_subdirectory("${ARA_SDK_ROOT}/ARA_Library" "${CMAKE_BINARY_DIR}/ARA_SDK")
target_link_libraries(my_plugin PRIVATE ARA_PlugIn_Library)
```

Do not include an unrelated copy of `ARAInterface.h` with a different ABI or
configuration. ARA SDK release upgrades must be tested with ARAAAP.

## Plug-in implementation

`include/aap/ara/ARAAAP.h` defines the companion entry point, analogous to the SDK's
ARACLAP/ARAVST3 integrations. `ARAAAPPlugInExtension` provides:

- `getFactory(plugin)`: the plug-in's ordinary, static SDK `ARAFactory`;
- `bindToDocumentController(plugin, ref, knownRoles, assignedRoles)`: one-time
  instance binding, returning the SDK's `ARAPlugInExtensionInstance`.

A plug-in using the C++ library can implement these with
`ARA::PlugIn::PlugInEntry::getPlugInEntry<Config, Controller>()` and `ARA::PlugIn::PlugInExtension::bindToARA`.
The sample implements `SampleDocumentController`, `SampleSource`, and
`SampleModification` using the SDK's model classes. It reads audio through
`ARA::PlugIn::HostAudioReader` and implements the SDK's archive delegate hooks.
Its Compose snapshot is a display cache derived from SDK objects.

Construct `aap::ara::PlugIn` with the AAP instance, its host, and this companion
extension. Delegate AAP `get_extension` to `adapter.getExtension(uri)`. This
returns the local companion entry point for `ARA_AAP_PLUGIN_EXTENSION_URI` and
the process-safe transport for `ARA_AAP_TRANSPORT_EXTENSION_URI`. Advertise the
transport URI (`urn://androidaudioplugin.org/extensions/ara/v2`) in AAP metadata.

All SDK model calls run on a shared model thread inside the plug-in process.
Binder dispatch may change threads between requests, so serializing calls alone
is insufficient. Route plug-in UI edits and binding teardown through
`adapter.synchronize(...)` as well. Publish immutable data for audio/UI threads.

Before releasing an AAP instance:

1. call `adapter.prepareForRelease()` while its SDK binding is alive;
2. destroy the application's SDK binding through `adapter.synchronize(...)`;
3. destroy the adapter and AAP instance.

This also handles the host destroying its controller first. The adapter removes
outstanding role assignments and destroys model dependencies in order. Factory
initialization is reference-counted per SDK factory and uninitialization follows
controller/binding teardown.

## Host implementation

Provide the actual SDK `ARADocumentControllerHostInstance` callbacks for audio,
archives, and optional content, model updates, and playback requests. Construct
`aap::ara::Host` with the AAP instance, host services, and
`ARADocumentProperties`. It negotiates the requested API generation with the real
remote factory, initializes it, and creates its SDK controller.

```cpp
aap::ara::Host ara(plugin, &hostServices, &documentProperties);
auto* dc = ara.documentController();
auto* api = dc->documentControllerInterface;
auto ref = dc->documentControllerRef;
auto* roles = ara.bind(
    ARA::kARAPlaybackRendererRole | ARA::kARAEditorViewRole,
    ARA::kARAPlaybackRendererRole | ARA::kARAEditorViewRole);

api->beginEditing(ref);
auto source = api->createAudioSource(ref, sourceHostRef, &sourceProperties);
// Create modifications, sequences, and regions using SDK types and references.
api->endEditing(ref);
api->enableAudioSourceSamplesAccess(ref, source, ARA::kARATrue);
```

Install `ara.transportExtension()` as the AAP host's transport extension before
binding or creating model objects that can call back. With aap-core,
`ScopedRemotePluginInstanceARAHostContext` installs this callback and preserves
other host extensions. Register the AAPXS definition with `addToRegistry()`;
the Android library's startup initializer does this for standard registries.

`factory()` returns a local metadata copy of the remote SDK factory. Its
initialization and controller-creation function pointers are intentionally absent:
those operations are performed by the `Host` adapter. The document-controller and
role interfaces are callable SDK proxies. Never send SDK pointers across Binder.
Each AAP instance adapter owns one controller and binds that AAP instance once.
Sharing one remote controller across multiple AAP instances is not provided by
this adapter. Destroy the host adapter and scoped host context before releasing
the corresponding remote AAP instance; their proxies borrow that connection.

The sample host supplies SDK audio-reader callbacks and creates, edits, and
removes SDK objects. It explicitly assigns playback regions to its SDK renderer
and sends `ARAViewSelection` through the SDK editor-view interface.

## Content, archives, and notifications

The adapters carry SDK content availability/grade queries, content analysis,
content readers, undo deactivation, cloning, processing algorithms, licensing
queries, renderer region/sequence assignments, selection, and hidden sequences.
Optional host tempo/content and playback services are exposed only when supplied.
Content events serialize scalar fields and strings, accounting for SDK struct
packing differences between ARM and x86; pointers are reconstructed locally.
Supported event types are notes, tempo entries, bar signatures, tuning, keys, and
chords. The reserved beatmarker type has no released event representation here.

Audio reads use chunked shared-memory transfers outside `process()`. SDK readers
receive the requested planar float32 or float64 data. The adapter copies returned
shared-memory data before another call can reuse the buffer.

Archives use the real `storeObjectsToArchive`/`restoreObjectsFromArchive` and host
archiving callbacks, including object filters and persistent-ID remapping.
Archive/filter transfers are chunked; there is no 4 KiB total archive limit.
Audio-file chunks are exposed when the remote factory advertises them.
`storeModification`/`restoreModification` are sample convenience methods that
wrap an SDK archive with its original modification ID, allowing clip duplication
to restore the archive into a new modification. The host treats the bytes as
opaque. The sample JSON uses hex encoding and limits one clip's encoded state to
64 MiB. Restoration also accepts unwrapped archives and lets the SDK plug-in
validate/import their format. The sample reader imports the old `gain-v1` state
through the SDK restore filter, preserving saved edits during the initial project
sync. Malformed, non-finite and out-of-range legacy gains are rejected without
changing the existing modification or echoing a plug-in update.

Plug-in edits call the SDK's queued notification methods. The host must poll
`notifyModelUpdates()` outside edit transactions. Host callbacks enqueue work;
fetch archives only after the callback has returned. Restoring host state must
not echo a plug-in edit. Selection is transient and does not modify archives or
undo history.

## Transport boundaries and current limits

`include/aap/ext/ara.h` now forwards to the SDK companion API. The former custom
graph types are private transport records under `aap/ara/internal/wire.h`.
The incompatible transport uses `/ara/v2`, preventing older custom-protocol
plug-ins from being mistaken for SDK-backed implementations.

Control messages use a 1 MiB AAPXS buffer. Factory/property text is limited to
255 UTF-8 bytes; content-event names to 2047 bytes; channel arrangements to
4096 bytes. Oversized SDK-facing properties fail instead of being truncated.
Android supports undefined layouts and the SDK's VST3, AAX, CLAP map/ambisonic
layout representations; Core Audio layouts require an Apple platform.
Model calls belong on a serialized host worker, outside the audio thread.
The C++ host adapter reports failed control queries as exceptions; hosts must
handle those at their application boundary.

The samples remain document editors with generated audio and diagnostic output;
they do not implement audible timeline playback, file import, or time stretching.
Their document objects, integration, sample reads, selection, and archives now
exercise the upstream SDK.

## Verification

```sh
cmake -S tests -B build/native-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/native-tests
ctest --test-dir build/native-tests --output-on-failure
```

Set `AAP_CORE_INCLUDE_DIR` when aap-core is not in the sibling checkout. The native
suite uses the real SDK with debug API validation enabled and covers factory
metadata, model-thread affinity, host tempo content, audio and note readers,
roles/selection, cloning, archives larger than 4 KiB, ID remapping, and queued
notifications. Android instrumentation tests exercise the sample editor and
AAPXS/Binder path. Install the plug-in APK before running the host's tests.

Verified on 2026-10-20: both samples and the AAR build in Debug and Release for
arm64-v8a and x86_64. The native SDK suite passes with debug API validation and
with UndefinedBehaviorSanitizer. The Galaxy SM-X230 passes all six standalone
host tests and ten plug-in tests (the host-only test is skipped in the plug-in
runner), including hosted Compose editing, private endpoints, SDK archive
restoration/ID remapping, legacy gain import, malformed archive rejection, and
selection. The standalone host regression verifies initial content and restored
gain through the plug-in's separate embedded surface, and also passes on the
Pixel 11 Pro Fold. The SDK integration test also passed on the Pixel without a
foreground display. The Galaxy's existing saved project was verified with both
tracks, all three clips, host selection, and its preserved 0.52× gain edit.
Release AAR/APK inspection confirms the exported API headers and packaged SDK
license/notices.
