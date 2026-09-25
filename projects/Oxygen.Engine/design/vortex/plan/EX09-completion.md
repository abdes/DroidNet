# EX09 completion checkpoint

Updated 2026-09-25. **EX09 is validated and closed.** Renderer numerical evidence
below is retained; real-app widget qualification and final user LightBench,
ordinary MultiView and offscreen acceptance are recorded in
[EX10 closeout](EX10-completion.md). Structured commit delivery was authorized after user review.

## Completed and verified

| Work                                                            | Evidence and result                                                                                                                                                                                                                                                                                                      |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Seven useful scenarios, compact UI and independent spot profile | [Preset report](EX09-presets-results.md), [images and profile](EX09-presets-visual-review.md). Existing passing reference/point/spot checks credited; no repeated benchmark campaign.                                                                                                                                    |
| Exposure settings, adaptation and lifecycle                     | Current native Debug suite **230/230**. Release qualifies all **230 distinct cases** through the original 224 passing cases plus repairs and affected checks. This is a union, not a claimed passing original Release run.                                                                                               |
| HDR endpoint reference repair                                   | Direct-light and static-sky range tests now use the accepted EX07 raster model 2. The superseded reciprocal BRDF remains an independent comparison elsewhere. Numerical transport tolerances unchanged.                                                                                                                  |
| Failed-view contracts                                           | Three tests now expect isolated failure instead of escaping exceptions. Late-recording discard is distinguished from rejection before drawing. Capture rejection, output preservation, retained exposure and retry assertions remain; the candidate test additionally compares the target image before/after discard.    |
| Fog-history product fix                                         | Temporal reuse rejects producer failure (bit 16), not ordinary FP16 candidate rejection (bit 2). Quantified history error survives FP32 recovery. Final Release repaired/neighbor tests **9/9**, then all affected fog checks **14/14**; Debug full suite passes. Nonfinite producer history still rejects and recovers. |
| Safe local settings files                                       | Temporary-file publication preserves the prior snapshot on replacement failure; actual reads are bounded to 1 MiB. Invalid-save, invalid/oversized-load and denied-replacement/retry checks pass. The new file implementation and tests are clang-tidy clean in two Release contexts.                                    |
| Panel layout correction                                         | The user rejected the reserved-viewport design and its intermediate zoom fix. Restore the original full-window scene with translucent overlay panels; panel width has no camera or viewport effect. Actual Camera/Settings/LightBench clicks leave the unobscured scene byte-identical; see overlay evidence below.      |
| Shared DemoShell surface                                        | All five inset-related DemoShell files are restored to HEAD; the dock background and camera-aspect workaround are removed.                                                                                                                                                                                               |
| Settings/file validation                                        | The intermediate 9/9 records are historical. The dock-aspect helper/test are removed with the rejected design; the retained eight settings/file cases pass in both configurations after restoring overlays. The later requested wider framing is qualified separately below.                                             |
| MultiView operational sequence                                  | Ordinary PiP and offscreen Release each complete **144 frames**, normal exit, with logged resize, retained/fresh view identity, sharing, owner removal/recreation and final pause. These are operational checks, not new pixel-comparison or timing claims.                                                              |
| Operating instructions                                          | LightBench README documents actual presets, controls, save/load and CLI IDs. MultiView README now uses `oxyrun`, explains controlled proof recipes versus ordinary edits, and documents previous-owner-frame sharing latency.                                                                                            |

## Requirement coverage

The fresh exposure suite uses current sources/shaders in the existing non-Tracy
Ninja tree. Important contract owners and representative tests are listed below;
the complete per-case Release provenance is in the evidence summary.

| Requirement                                                  | Current native coverage                                                                                                                                                                                                           |
| ------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Manual EV, physical camera, disabled and zero target         | `ManualCameraAndDisabledWriteUnifiedGpuState`, `ServiceManualConsumesExactGpuGain`, `FrameResolvePreservesOperationalEndpointsAndCameraGain`, scene fixed-mode cases.                                                             |
| Equal-time adaptation, long frames, pause and zero speed     | `HybridCrossingMatchesElapsedTimeAcrossFrameSchedules`, `BrighteningUsesSpeedDownAcrossFrameSchedules`, `LongAndIrregularHybridStepsRemainEquivalent`, zero-speed/paused-session cases. The existing 5e-4 EV budget is unchanged. |
| Metering, masks and curves                                   | `Metering_test.cpp`, `Masks_test.cpp`, curve cases in `Adaptation_test.cpp`; independent histograms and accepted-mask replacement semantics.                                                                                      |
| Seed/cut/mode/startup and recovery                           | `Transitions_test.cpp`, `SceneLifecycle_test.cpp`, `ScenePreparation_test.cpp`, precision/status and device-event cases.                                                                                                          |
| Sharing, source loss, lifetime and isolation                 | `Sharing_test.cpp`, `SourceLoss_test.cpp`, `SceneSharing_test.cpp`, `StateLifetime_test.cpp`, queued consumer and auxiliary handoff cases.                                                                                        |
| Opaque/masked/translucent/forward/emissive endpoints         | `LightingRadiance_test.cpp`, `MaterialTextures_test.cpp`, queued mixed-format consumers and source-failure/depth/mask-hole checks.                                                                                                |
| Sky/AP/fog/bloom and history                                 | `SkyRadiance_test.cpp`, `FogComposition_test.cpp`, `FogHistory_test.cpp`, `LocalFog_test.cpp`, `ExternalBloomUsesFrameDomainAndHonorsDisable`, range/recovery tests.                                                              |
| Production FP32/P=1 versus explicit diagnostic qualification | `Fp32Only_test.cpp`, `ScenePrecision_test.cpp`, `Fp32Reference_test.cpp`, frame-domain and suitability cases. No production precision policy change.                                                                              |

Prior EX05 layout/pixel proofs remain historical evidence as recorded in the
tracker; some original transient raw-manifest paths no longer exist. Their old
numbers are not presented as new measurements. Current native auxiliary and
family tests plus the two application sequences provide fresh operational
coverage. Final ordinary and offscreen MultiView visual/interaction acceptance
was subsequently confirmed OK by the user.

## Durable records

- [Native results, case provenance and identities](validation/ex09-completion-20260925/native-summary.json).
- [MultiView operational records](validation/ex09-completion-20260925/multiview-operational.json).
- [Final overlay comparison](validation/ex09-completion-20260925/overlay-panels/comparison.json). The earlier `panel-and-files-summary.json` records the rejected docked design.
- [Camera panel after the correction](validation/ex09-completion-20260925/overlay-panels/camera.png).

Original failing attempts and passing repairs are retained as compressed native
JSON/logs beside these summaries. The fog shader change required one Release
shader request rebuild. No EX07 benchmark, RenderDoc capture or timing baseline
was repeated. Normal FP32 production behavior is unchanged by distinguishing
the diagnostic rejection bit from producer failure.

## Final disposition

The actual Reset/Save/Load and panel-return workflows pass in Debug/Release;
camera extents remain unchanged when switching panels. The user accepted
adaptation, resize/close and the original directional toggle in ordinary
LightBench, plus ordinary and offscreen MultiView. EX08.1 and EX08.2 are complete.
Indoor's finite 1024-map edge quality remains documented rather than hidden by
a demo-only shadow tuning change. Structured commit delivery was subsequently authorized after review.

## Final default framing requested by the user

Preset defaults now use an 8 m minimum width and 3.75 m minimum height: at 16:9,
the view is 8 × 4.5 m and objects are 20% smaller than with the original framing.
The shipped Indoor snapshot uses the same defaults. The scene still covers the
full window behind translucent controls; panel widths are not camera inputs.
Camera-dependent native probe coordinates were adjusted to continue sampling
their intended surfaces, with unchanged shading/exposure tolerances. In particular,
the outside-cone probe stays 2 m from the spot center rather than moving off the
receiver when the viewport covers more world space.

The wider defaults pass **17/17 Debug**. Release qualifies **17 distinct cases**
through 16 original passes and the corrected receiver-probe rerun. No tolerance
changed. [Final LightBench identities/results](validation/ex09-completion-20260925/final-lightbench-summary.json)
preserve the initial failed probe attempt and its passing correction. The overlay
pixel comparison predates only this requested framing constant change; the
panel-independent viewport implementation is identical.
