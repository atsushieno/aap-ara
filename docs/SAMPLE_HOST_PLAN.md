# ARA Sample Host Plan (AI slop)

## Purpose

The sample host should demonstrate real ARA host behavior, not merely that an
AAP extension can be discovered.

For AAP, that means the host sample must own the ARA-side responsibilities that
an editor/render-capable host would normally provide:

- ARA document-model lifecycle,
- random-access audio source reads outside realtime `process()`,
- content update notifications,
- host capability advertisement,
- deterministic validation that the plug-in received and used host-provided
  audio data.

## Responsibility split

### `aap-core`

`aap-core` should only provide generic transport and callback routing needed for
host extensions to work across process boundaries. It should not embed ARA host
policy or pretend every host instance supports ARA features.

### `androidaudioplugin-ara`

`androidaudioplugin-ara` provides ARAAAP, backed by the upstream ARA SDK:

- `aap::ara::Host` exposes SDK document-controller and role proxies;
- `aap::ara::PlugIn` dispatches to an application's real SDK factory/controller;
- SDK host callbacks provide audio, archives, optional musical content and playback;
- AAPXS carries flattened calls and chunked audio/archive data across Binder.

See [ARA.md](ARA.md) for the SDK dependency, application API, and transport limits.

### `samples:aaparahostsample`

The host sample should stay small and consume those helpers. It should supply
its own audio sources and scenario logic, but not reimplement the reusable ARA
dispatch machinery.

## What the sample must prove

The sample host and sample plug-in together should demonstrate the following
observable behaviors.

### Milestone 1: graph lifecycle

- host queries plug-in ARA capabilities,
- host creates document, musical context, region sequence, audio source, audio
  modification, and playback region objects,
- host updates properties,
- host destroys the graph cleanly.

This proves the upstream SDK ARA object model can be driven across the host/plugin
boundary.

### Milestone 2: prefetched audio source access

- host registers an audio source with deterministic PCM content,
- plug-in enables source sample access,
- plug-in requests a known sample range outside `process()`,
- host copies requested audio into shared memory,
- plug-in analyzes the returned audio and exposes the result in a measurable
  form,
- host verifies the observed metrics against the expected source data.

This is the first milestone that proves ARA host-provided audio access.

### Milestone 3: content invalidation and reread

- host swaps or mutates the backing source content,
- host sends content-changed notification,
- plug-in rereads the same source range,
- plug-in emits new analysis values,
- host verifies the before/after difference.

This demonstrates that the plug-in is not using stale cached data.

## What is currently implemented

The current implementation covers milestones 1 through 3 and a small project editor:

- `aap::ara::Host` wraps real SDK host services and document-controller calls,
- the sample host registers a deterministic stereo source provider,
- the sample plug-in reads host-provided samples and computes verification
  metrics,
- the sample host compares expected and observed metrics before and after a
  content change notification,
- multiple tracks and clips retain stable ARA identities across model edits,
- free clip dragging updates playback-region timing and sequence membership,
- the host embeds the UI of the same plugin instance receiving its model,
- plugin gain edits notify the host, which archives their state for project
  save/load and undo/redo without notification loops.

This is the minimum viable ARA-prefetch demonstration for AAP.

## Next milestones after the current sample

### Archive persistence

The integration implements the SDK archive-controller interfaces, object filters,
and ID remapping with chunked transfers. The sample persists per-modification
SDK archives in its project JSON; its UI does not expose whole-document archives.

### Musical content and timeline synchronization

The integration carries SDK tempo, meter and musical content readers as well as
arrangement and bidirectional content updates. The native SDK tests exercise host
tempo and plug-in note readers; a musical-content editor remains sample UI work.

### Multiple audio sources and playback regions

The editor already maps multiple sources, modifications and playback regions.
Extend audible rendering and file import beyond the current generated tones and
diagnostic output.

### Host UI/editor integration

The same-instance hosted Compose UI and capability-negotiated host selection are
implemented. Both sample workspaces send ordered clip/track selection and an
optional explicit project-time range. The received-document view highlights them
without changing model revision or archives. Selection is transient and is pruned
on deletion; undo does not reselect removed objects. Further editor-focus APIs
remain future work.

## Acceptance criteria for the sample

The sample must use the upstream SDK factory, document controller, types, and
companion binding, and must demonstrate all of the following:

- the host provides the required ARA services itself,
- the plug-in performs non-realtime source sample reads through the ARA host
  extension,
- sample data is copied through the negotiated shared-memory mechanism,
- the host can prove that plug-in-visible results changed after host-side source
  content changed.

Anything less is scaffolding, not an ARA sample.
