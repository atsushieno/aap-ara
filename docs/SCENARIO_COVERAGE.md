# Sample scenario coverage

**Run Scenario** in the existing AAPARA host and plug-in launchers now runs the
original pre/post-change audio-fetch verification, followed by all eight test
supported upstream ARA_Examples TestHost functions. The functions are
compiled directly from the pinned SDK, not rewritten equivalents. They run
against `aaparapluginsample`, using separate documents/instances from the visible
editor. There are no separate ARA_Examples Android apps.

| ARA_Examples scenario | Coverage in the existing sample's Run Scenario |
| --- | --- |
| Property updates | Source names, sequence colors and playback-region timing, in addition to the original document/region edits. |
| Content updates | Host tempo, meter, tuning, chord and note callbacks; the original check still verifies changed audio samples. |
| Content reading | Source, modification and playback-region note readers, including source-to-playback time conversion. |
| Modification cloning | SDK `cloneAudioModification`, region creation and reading inherited content. The editor's Duplicate action alone did not test this API. |
| Archiving | Whole-document object archives restored into another document. This complements the editor's per-modification gain archives. |
| Split archives | Separate document, source and modification archives and their restore filters. |
| Drag and drop | Filtered source/modification archives restored under new persistent IDs in a second document. |
| Processing algorithms | Enumerate, select and analyze with both original-sample level-analysis modes. |

Basic graph construction and sample fetching are shared prerequisites of these
functions. Existing coverage is retained rather than replaced with another custom
scenario implementation. The report lists each upstream case and one final
`Result: PASS` only when both the original audio check and all eight cases pass.
A missing note-analysis or processing-algorithm exposure is a failure, not a pass
for an empty scenario.

## Plug-in exposures

The existing plug-in previously exposed audio-read metrics and gain archives,
without ARA-readable analysis content or processing algorithms. It now exposes:

- unpitched note events representing the measured audio level, through source,
  modification and playback-region content readers;
- two simple processing modes, **RMS level** and **Peak level**, preserving the
  original diagnostic audio output;
- host-provided note content when available;
- filtered persistence of source analysis, the selected algorithm and gain edits.

These are sample level-analysis events, not a pitch detector. The archive format
is `ARAAAP-state-v3`; `ARAAAP-gain-v2` and `gain-v1` projects remain readable.
The factory advertises the previous archive ID as compatible.

## Implementation and verification

`samples/scenarios` contains the reusable AAP companion entry, model-thread
transport, upstream TestHost build imports and scenario suite. The existing
editor keeps its original runtime and
workflow; the new suite runs after its original audio verification.

Native model tests check reader values, algorithm selection, gain scaling and
filtered restoration under remapped source/modification IDs. Android tests assert
that the existing Run Scenario report includes all eight successful cases while
a visible editor session remains alive, and verify private/exported service
connections and hosted gain editing independently.

Verified on Samsung SM-X230 (Android 16), 2026-10-22: the existing host's actual
Run Scenario button reports `Result: PASS` and `Upstream scenarios: PASS (8/8)`
against `org.androidaudioplugin.ara.sample.factory`. Private-workspace and hosted
editor tests passed; all three native suites passed. The original host and plug-in build in Debug
and Release for arm64-v8a and x86_64.
