# ARA Integration in AAP

## Goal

Define and implement an AAP-native ARA extension family so that AAP has its own
equivalent of `ARA VST3`, `ARA AU`, or `ARA CLAP`.

The ARA features should be exposed only through AAP extension APIs plus AAPXS
transport where needed. They should not be modeled as forwarded calls into
VST3, CLAP, Audio Unit, or any other foreign plug-in API.

## Repository boundary

ARA uses aap-core's role-aware AAPXS lifecycle (`docs/AAPXS_LIFECYCLE.md`).
The dispatcher creates the canonical `AraClientAAPXS` / `AraServiceAAPXS`
context before proxy lookup and releases it through the initiator lifecycle
hook. Proxy getters borrow that context without lazy creation or a shared mutex.
Rebuild ARA and its native consumers together with this SDK update; its ARA
extension API and serialization remain unchanged.

The AAP-native public API belongs in `aap-core`.

If implementation work later needs the upstream ARA SDK or any companion-SDK
dependent helper code, that should live outside `aap-core` in a sibling
repository such as `aap-ara`. This keeps potential licensing, redistribution,
and dependency-management issues out of the core AAP repository while allowing
`aap-core` to define the stable AAP-facing contract.

## Upstream status

As of `2026-06-10`, the latest tagged public ARA SDK release is `2.3.0`
(`2025-11-06`). Upstream `main` is documented by Celemony as work in progress
and should not be treated as a stable product target.

Implications:

- production work should target `ARA 2.x` first,
- any `ARA 3` work in AAP should remain explicitly experimental.

## What AAP has to implement

If AAP wants true ARA support, it needs AAP-native equivalents for the host
facilities that ARA-capable AAP plug-ins expect:

- document controller lifecycle,
- musical context objects,
- region sequence / playback region management,
- random access to audio source samples outside `process()`,
- transport and time conversion,
- tempo, meter, key, and content update notifications,
- persistence for ARA archives and edit state,
- editor selection/focus integration.

These are larger than a normal plug-in extension. They form a host-side model.

The most likely first host implementation for this is not `aap-core` alone but
an AAP DAW host such as UAPMD.

## Extension-shape direction

The new AAP ARA work should look like an AAP extension family with:

- plugin-side ARA extension APIs,
- host-side ARA extension APIs,
- AAPXS request/reply transport for non-RT control traffic,
- shared-memory or file-descriptor based transport for large non-RT audio data,
- persistence hooks that align with AAP state management.

The important rule is that the public AAP-facing contract is the AAP ARA
extension itself. Internal implementation details can vary, but the host/plugin
relationship must be defined in AAP terms.

## Recommended execution order

### Phase 1: capability advertisement

Add an explicit AAP extension that lets plug-ins and hosts declare:

- supported ARA generation,
- broad renderer/editor role flags.

This work starts in `include/aap/ext/ara.h`.

This is deliberately small. It avoids freezing the wrong transport or document
model too early.

### Phase 2: define the AAP host-side ARA model

Before adding lots of IPC, define the AAP equivalents for:

- audio sources and sample readers,
- playback regions,
- musical contexts,
- host edit transactions,
- archive persistence and restore,
- host/editor notifications.

This should live as a dedicated AAP extension family, not as ad hoc special
cases in `PluginInstance`.

The first concrete draft of that API is now started in
`include/aap/ext/ara.h` with:

- opaque object IDs for document, musical context, region sequence, audio
  source, audio modification, and playback region,
- versionable property structs for those objects,
- host-driven plug-in extension entry points to create, update, and destroy the
  model graph,
- plug-in-driven host extension entry point for random-access source sample
  reads.

That is still intentionally incomplete, but it is enough to begin separate host
and plug-in implementations against a shared graph contract.

### Phase 3: add AAPXS transport

Once the model is stable, add AAPXS-backed transport for non-realtime ARA data:

- lifecycle/control requests,
- region and context synchronization,
- content notifications,
- editor coordination,
- archive/state handoff.

The first transport draft now also exists in
`include/aap/core/aapxs/ara-aapxs.h` with:

- dedicated AAPXS opcodes for graph lifecycle and updates,
- flattened wire structs for pointer-based public property types,
- a fixed control shared-memory budget for model messages,
- a sample-read wire shape where the host writes returned audio into the
  extension-owned shared-memory region rather than trying to pass process-local
  pointers across IPC.

Do not route bulk sample reads through MIDI2 SysEx8. That path is wrong for
random-access audio. Use shared memory, file descriptors, or another explicit
non-RT mechanism.

### Phase 4: first host integration

The first serious integration target should be an AAP DAW host such as UAPMD,
because that is where the document model, region ownership, transport, and
persistence already have a place to live.

## Non-goals

The following would be misleading:

- exposing draft `ARA 3` as stable,
- treating ARA as only a GUI concern,
- forcing random-access audio into the realtime `process()` path,
- defining the AAP ARA public contract in terms of foreign plug-in APIs.

## Immediate next steps

1. keep `include/aap/ext/ara.h` experimental while the graph API settles,
2. implement the AAPXS wire contract in `include/aap/core/aapxs/ara-aapxs.h`,
3. design the AAP-side document model around an actual host such as `uapmd`,
4. add concrete host/plugin implementations on top of the draft transport.

If the next implementation stage needs the upstream ARA SDK for validation,
examples, or interop experiments, spin that work out into `aap-ara` rather
than adding the SDK as a submodule of `aap-core`.
