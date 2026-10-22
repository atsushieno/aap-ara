# aap-ara (AI slop)

![samples sshot](docs/images/samples.jpg)

ARA SDK integration for Audio Plugins For Android, with an ARAAAP companion API and Android process adapters.

This repository is intentionally separate from `aap-core` so that:

- the AAP-facing ARA API can evolve without changing the core repository layout,
- Prefab/native packaging can be shipped as its own AAR,
- the upstream ARA SDK dependency and its integration remain outside `aap-core`.

## Modules

- `androidaudioplugin-ara`: AAR + Prefab package containing ARAAAP and upstream API headers, SDK adapters, and AAPXS transport
- `samples:aaparahostsample`: project editor using SDK document/host interfaces through `aap::ara::Host`, with the original verification scenario available alongside editing
- `samples:aaparapluginsample`: plug-in app implemented with the ARA SDK C++ library, SDK model objects, audio readers, and archives

## Current Sample Scope

The current sample pair is intended to prove the minimum ARA-specific path that
matters for AAP hosts:

- the host exposes the ARA host extension,
- the plug-in exposes the ARA plug-in extension,
- the host creates and destroys the ARA object graph,
- the plug-in requests prefetched audio source samples from the host outside `process()`,
- the host can notify content changes and the plug-in can reread updated samples,
- plugin-owned content edits notify the host and round-trip through modification archives.

That scope is intentionally narrower than a production DAW integration. It is
meant to validate the host/plug-in contract first.

## Sample Roadmap

The remaining example milestones are tracked in
[`docs/SAMPLE_HOST_PLAN.md`](docs/SAMPLE_HOST_PLAN.md).

## Local Development

Initialize the pinned ARA SDK 2.3.0 and its nested submodules:

```sh
git submodule update --init --recursive
```

The modules build for arm64-v8a and x86_64. See
[`docs/ARA.md`](docs/ARA.md) for ARAAAP app integration, CMake dependencies,
threading, and native SDK tests. The Prefab package exports the upstream API
header; plug-ins using the SDK C++ helpers also link `ARA_PlugIn_Library`.

Install both sample apps from the same build. Updating only the plug-in leaves
an older host sending the incompatible `/ara/v1` protocol:

```sh
./gradlew :samples:aaparahostsample:installDebug :samples:aaparapluginsample:installDebug
```

To use the current `../aap-core` checkout, enable the included build explicitly:

```sh
./gradlew -PuseLocalAapCoreBuild=true :samples:aaparahostsample:assembleDebug :samples:aaparapluginsample:assembleDebug
```

Otherwise, dependencies are resolved via Maven repositories (including Maven Local).

The latest device verification is recorded in
[`docs/DEVICE_VERIFICATION.md`](docs/DEVICE_VERIFICATION.md).

## Shared sample editor

`samples/common` owns the editor runtime, Compose UI/model, Android JNI adapter,
and shared editor tests. Both sample apps consume this directory independently;
neither app includes sources from the other app. Each app keeps its own launcher,
and the plugin app owns its plugin implementation, plugin JNI adapter, and service UI.

The common CMake module defines `aap-ara-sample-editor` (the JNI-free native core)
and `aap-ara-sample-editor-jni` (the Android adapter). A different frontend, such as
SDL, can link the core and call `sample_editor` with native `Track` and `Clip` rows.
Supply an AAP connection list and plugin snapshot, keep them alive for the editor
session, and serialize host calls on one worker thread. Java strings/arrays,
Binder setup, and Java-side connection lookup stay in `android/sample-editor-jni.cpp`.
The shared Kotlin API is `org.androidaudioplugin.arasample.SampleEditorNative`.

The plugin's own `aap-ara-plugin-sample.cpp` exposes a JNI-free `sample_plugin`
interface through its adjacent header. Its `aap-ara-plugin-sample-jni.cpp` adapter
resolves the service instance and converts Java values. The core resolves opaque
snapshot tokens, reads JSON snapshots, and applies modification gain edits with
lifetime checks and synchronization.

## Project editor

Launch the standalone host sample, tap **Connect**, then **Show plugin UI** to see
the host editor and its hosted plugin together. The plugin view displays the ARA
document received by that same instance. Add, rename, move, or remove tracks and
clips in the host and observe the plugin's timeline and revision update automatically.
Source-content edits also update the content-change counter and sample-read diagnostics.
Drag a clip freely along the timeline or into another track. Positions are not
quantized; dragging previews the move and releasing commits one undoable edit.
In the hosted plugin, select a clip and choose **Edit sound** to change its
non-destructive gain. The plugin notifies the host through the reverse ARA
content-update callback. The host captures its opaque modification archive;
**Save**, **Load**, **Undo**, **Redo**, and clip duplication preserve these edits.
Completed model transactions are published as thread-safe snapshots; closing the
plugin view does not destroy the host's document or plugin instance.

The embedded UI is a Compose view served through `AudioPluginViewService` and
SurfaceControl. The sound-edit form stays inside that surface, so it works
without an activity window token. Other AAP hosts such as UAPMD and aaphostsample
can show it too; it remains empty until that host sends an ARA document. Sound editing is enabled
when the host advertises content-update support. ARA keeps arrangement properties
host-owned: the host moves clips, while the plugin changes modification content.
This follows the direction of the upstream
[ARA model update controller](https://github.com/Celemony/ARA_API/blob/main/ARAInterface.h).

The plugin app's launcher still provides a separate Compose project editor for
standalone use. Its private Binder endpoint protects its sample-read callbacks
from external hosts. The first launch provides a small two-track demo;
**New** clears it. Editing also works offline. Tap a timeline clip
to select it for editing, duplication or removal. Clip properties include the
start time, source offset, duration, destination track, tone frequency and gain.
Tracks map to ARA region sequences; each clip owns an audio source, modification
and playback region. Stable IDs survive edits and undo/redo. Source-content edits
notify the plugin to reread samples; removals destroy dependents before parents.

**Save** and **Load** use one JSON project in the host app's private storage.
Unsaved edits survive activity recreation. **Run Scenario** retains the original
pre/post-content-change sample-read verification on a separate plugin instance.
Native operations run serially off the UI thread; editing is paused while they run.
Reverse notifications enqueue work on the host; archives are fetched after the
callback returns. Host restoration never echoes a plugin-originated notification.
The samples now use the upstream SDK model-update and archiving interfaces.
The host polls `notifyModelUpdates()` for queued plug-in edits. Archives use SDK
object filters and persistent-ID remapping, transferred in chunks without the
previous 4 KiB total limit. The incompatible SDK-backed transport is `/ara/v2`;
the sample SDK archive reader also imports the previous `gain-v1` sample state,
so saved projects retain their initial arrangement and gain edits after updating.

This is a document-model editor: sources are generated stereo tones with a
60-second source length. File import, audible timeline playback and time stretching
are not implemented. Plugin audio output continues to carry diagnostic metrics so
existing host verification remains compatible. Plugin gain affects the processed
RMS display and diagnostic output; default gain preserves the original scenario.

Instrumentation tests in `ProjectEditorTest` cover JSON identity and reference
validation, multi-source content rereads after deleting another source, model
mutation/removal, and the original scenario while an editor session remains open.
Install the plugin APK before running those connected tests.

The UI uses Compose Material 3, including track lanes, clip selection, property
forms and diagnostics. `ProjectEditorComposeTest` exercises track creation,
source editing and clip deletion through the UI in both sample apps.
It also checks a free drag between tracks, gesture cancellation, and restoration.
`ProjectHostedEditorTest` requests the preferred size and attaches the embedded
surface of a real editor instance through the same service used by AAP hosts.
`ReceivedDocumentTest` checks that the plugin's Compose view receives host additions,
renames, timing edits, content changes and removal, remains attached to the same
instance while the original scenario runs, and safely handles instance destruction.
It also verifies plugin-to-host gain updates, changed diagnostic output, persisted
archives and host restoration without notification loops.
Host selection is sent separately from document edits. Use **Select multiple**,
**Select all clips**, **Time range**, and **Clear selection** in either app's
project editor. The hosted plugin highlights the selected clips, tracks and
explicit range; it follows the primary host-selected clip for sound editing.
Selection survives moves, is pruned on deletion, and is resent after document
synchronization and editor reopening. It is not saved or recorded in undo history,
and changes its own notification counter without advancing the model revision.
Older plugins without the selection capability remain usable for document editing.
The native view test uses a non-activity context to cover hosted sound forms.
`PluginTransportTest` verifies
that processing writes only audio output ports, preserving host-injected MIDI/AAPXS
transport buffers even when they precede or separate the audio ports.
The plugin-only `PrivateWorkspaceConnectionTest` verifies that the workspace and
exported service can run their verification scenarios independently after binding
the external connection.
