# ARA Sample Host Plan

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

`androidaudioplugin-ara` should provide the reusable ARA-specific host-side C++
helper surface. The first piece of that is `aap::ara::HostRuntime`, which owns:

- host capability callbacks,
- host-side audio-source registration,
- host-side sample-read dispatch,
- plugin-originated content-update callbacks.

Future ARA host helper classes can extend this area with document/archive and
content-access helpers.

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

This proves the AAP-native ARA object model can be driven across the host/plugin
boundary.

### Milestone 2: prefetched audio source access

- host registers an audio source with deterministic PCM content,
- plug-in enables source sample access,
- plug-in requests a known sample range outside `process()`,
- host copies requested audio into shared memory,
- plug-in analyzes the returned audio and exposes the result in a measurable
  form,
- host verifies the observed metrics against the expected source data.

This is the first milestone that proves real ARA-like prefetched audio access.

### Milestone 3: content invalidation and reread

- host swaps or mutates the backing source content,
- host sends content-changed notification,
- plug-in rereads the same source range,
- plug-in emits new analysis values,
- host verifies the before/after difference.

This demonstrates that the plug-in is not using stale cached data.

## What is currently implemented

The current implementation covers milestones 1 through 3 and a small project editor:

- `aap::ara::HostRuntime` provides host capability and sample-read callbacks,
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

Compact per-modification opaque archives and project JSON persistence are
implemented. Full ARA document archive-controller semantics remain future work.

### Musical content and timeline synchronization

Arrangement and bidirectional content updates are implemented. Tempo, meter and
musical content-reader APIs remain future work.

### Multiple audio sources and playback regions

The editor already maps multiple sources, modifications and playback regions.
Extend audible rendering and file import beyond the current generated tones and
diagnostic output.

### Host UI/editor integration

The same-instance hosted Compose UI is implemented. Add editor-facing state such
as focused playback region or selection changes, if and when the AAP ARA
contract defines those hooks.

## Acceptance criteria for the sample

The sample should not be considered a valid ARA example unless all of the
following are true:

- the host provides the required ARA services itself,
- the plug-in performs non-realtime source sample reads through the ARA host
  extension,
- sample data is copied through the negotiated shared-memory mechanism,
- the host can prove that plug-in-visible results changed after host-side source
  content changed.

Anything less is scaffolding, not an ARA sample.
