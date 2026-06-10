# aap-ara

Experimental AAP-native ARA extension package for Audio Plugins For Android.

This repository is intentionally separate from `aap-core` so that:

- the AAP-facing ARA API can evolve without changing the core repository layout,
- Prefab/native packaging can be shipped as its own AAR,
- any future upstream ARA SDK experiments can live here without creating licensing or dependency pressure on `aap-core`.

## Modules

- `androidaudioplugin-ara`: AAR + Prefab package containing the ARA headers, native AAPXS implementation, and C++ host runtime helpers
- `samples:aaparahostsample`: host app that uses `aap::ara::HostRuntime` and demonstrates host-provided ARA document and audio-source services
- `samples:aaparapluginsample`: plug-in app that exposes the ARA extension, stores the model graph in memory, and performs host-driven sample reads

## Current Sample Scope

The current sample pair is intended to prove the minimum ARA-specific path that
matters for AAP hosts:

- the host exposes the ARA host extension,
- the plug-in exposes the ARA plug-in extension,
- the host creates and destroys the ARA object graph,
- the plug-in requests prefetched audio source samples from the host outside `process()`,
- the host can notify content changes and the plug-in can reread updated samples.

That scope is intentionally narrower than a production DAW integration. It is
meant to validate the host/plug-in contract first.

## Sample Roadmap

The remaining example milestones are tracked in
[`docs/SAMPLE_HOST_PLAN.md`](docs/SAMPLE_HOST_PLAN.md).

## Local Development

If `../aap-core` exists, Gradle uses it as an included build automatically.
Otherwise, dependencies are resolved via Maven repositories.
