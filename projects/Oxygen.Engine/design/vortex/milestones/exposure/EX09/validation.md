# EX09 completion checkpoint

Updated 2026-09-25. **EX09 is validated and closed.** Renderer numerical evidence
below is retained; real-app widget qualification and final user LightBench,
ordinary MultiView and offscreen acceptance are recorded in
[EX10 closeout](../EX10/validation.md). Structured commit delivery was authorized after user review.

## Completed and verified

| Work                                                            | Evidence and result                                                                                                                                                                                                                                                                                                      |
| --------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Seven useful scenarios, compact UI and independent spot profile | [Preset report](validation.md), [images and profile](validation.md). Existing passing reference/point/spot checks credited; no repeated benchmark campaign.                                                                                                                                                              |
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
subsequently passed manual checks.

## Durable records

- [Native results, case provenance and identities](evidence/validation/ex09-completion-20260925/native-summary.json).
- [MultiView operational records](evidence/validation/ex09-completion-20260925/multiview-operational.json).
- [Final overlay comparison](evidence/validation/ex09-completion-20260925/overlay-panels/comparison.json). The earlier `panel-and-files-summary.json` records the rejected docked design.
- [Camera panel after the correction](evidence/validation/ex09-completion-20260925/overlay-panels/camera.png).

Original failing attempts and passing repairs are retained as compressed native
JSON/logs beside these summaries. The fog shader change required one Release
shader request rebuild. No EX07 benchmark, RenderDoc capture or timing baseline
was repeated. Normal FP32 production behavior is unchanged by distinguishing
the diagnostic rejection bit from producer failure.

## Final disposition

The actual Reset/Save/Load and panel-return workflows pass in Debug/Release;
camera extents remain unchanged when switching panels. Acceptance covered
adaptation, resize/close and the original directional toggle in ordinary
LightBench, plus ordinary and offscreen MultiView. EX08.1 and EX08.2 are complete.
Indoor's finite 1024-map edge quality remains documented rather than hidden by
a demo-only shadow tuning change. Structured commit delivery was subsequently authorized after review.

## Final default framing

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
changed. [Final LightBench identities/results](evidence/validation/ex09-completion-20260925/final-lightbench-summary.json)
preserve the initial failed probe attempt and its passing correction. The overlay
pixel comparison predates only this requested framing constant change; the
panel-independent viewport implementation is identical.

## EX09 usable preset candidate

Status: historical preset qualification, followed by final EX09 validation and
acceptance on 2026-09-25. See [EX10 closeout](../EX10/validation.md) for current
status and [EX08.2](../EX08.2/validation.md) for the real-app widget suite.
The user prioritized usable scenarios and specifically requested Indoor/Outdoor.
EX08.1 and EX08.2 are complete. Structured commit delivery was authorized after user review.

## Delivered

The Scenario selector replaces visibility-only buttons with complete local
settings: Neutral Reference, Point Falloff, Spot Cone, Material Lighting,
Auto Adaptation, Indoor and Outdoor Daylight. Each selection applies geometry,
camera, lights and post-processing coherently; Reset restores that preset.
Camera and directional/point/spot controls remain accessible. Saved settings
retain preset identity; the shipped indoor settings match the Indoor defaults.
New optional local fields preserve existing files' original horizontal-light
semantics. There is no legacy version reader or general experiment controller.

- Point Falloff: 1000 lm, fixed EV2.729, one gray receiver; 1/2/4 m buttons.
- Spot Cone: 1000 lm at 3 m, 15/30-degree half angles, fixed EV5.4495.
- Material Lighting: matte/glossy spheres and ground, angled 1000-lux directional
  illumination, shadows, fixed EV8 and ACES.
- Auto Adaptation: cards/spheres/ground, angled 1000-lux illumination, Auto/Average
  and ACES. 100/1000/10000-lux steps preserve exposure history.
- Indoor: warm 1600-lm point fill plus 2500-lm spot, both casting shadows,
  Auto/Center Weighted and ACES.
- Outdoor Daylight: white 100000-lux angled directional illumination, shadows,
  Auto/Center Weighted and ACES. These are direct-lighting comparisons using the
  same reference objects, not new GI/sky implementations.

Auto profiles use explicit public startup seeds (EV8, EV5 and EV14.6 respectively).
They do not derive initial exposure from an inactive manual field or add GPU
measurement machinery. New preset scenes are application data, not shader changes.

## Native results

Both configurations use the existing non-Tracy Ninja tree. The final Debug suite
passes **13/13**. Release qualifies the same 13 distinct cases: 12 passed in the
initial full suite, and the corrected light-step case passes in a focused rerun.
The original step expectation incorrectly assumed exact scale invariance of a
quantized, percentile-trimmed histogram. The replacement uses the independent
histogram reference at each step; numerical tolerances were not relaxed and no
production exposure code was changed. Do not read the retained failed attempt as
an unresolved product failure or a passing full-suite report.

After visual review, affected settings and viewing-scene tests were repeated:
Release **7/7** after placement/lighting changes, then **6/6** after enabling
Indoor local shadows; Debug **7/7** on the final candidate. Unaffected reference,
point and spot results are credited. After the requested 20% zoom-out, Debug passes **17/17** and Release qualifies
**17 distinct cases** (16 original passes plus a corrected outside-cone probe).
The table below uses the widened default framing; [final records](evidence/validation/ex09-completion-20260925/final-lightbench-summary.json)
retain exact provenance and the rejected probe attempt.

| Check            | Observed Release result                                              |
| ---------------- | -------------------------------------------------------------------- |
| Point at 1 m     | 4.774404 cd/m²                                                       |
| Point at 2 m     | 1.193377 cd/m²                                                       |
| Point at 4 m     | 0.297450 cd/m²                                                       |
| Spot center      | 15.731779 cd/m²; outside-cone receiver remains dark                  |
| Auto Adaptation  | Meter 27.2990 cd/m²; settled gain 0.00659361; gray output 0.5384     |
| Indoor           | Meter 1.54088 cd/m²; settled gain 0.1168163; gray output 0.5739      |
| Outdoor Daylight | Meter 2958.8774 cd/m²; settled gain 0.0000608330; gray output 0.5154 |

Point/spot analytical checks exercise forward production shading; applicable EX07
shared/deferred photometry evidence is credited. The three Auto scene checks use
deferred production rendering with shadows, independently verify metering/target,
check convergence and reject washed-out gray subjects. Bright/dark steps retain
the exposure generation and respect adaptation speed. The Neutral Reference
continues to pass its existing forward/deferred color/depth/output tests.
All presets validate and round-trip; malformed and coupled-invalid loads remain
rejected. No benchmark, historical capture or new runtime instrument was run.

[Evidence summary and hashes](evidence/validation/ex09-presets-20260925/summary.json),
[Debug suite](evidence/validation/ex09-presets-20260925/lightbench-ex09-all-debug.json.gz),
[initial Release suite](evidence/validation/ex09-presets-20260925/lightbench-ex09-all-release.json.gz),
[corrected Release step case](evidence/validation/ex09-presets-20260925/lightbench-ex09-steps-release.json.gz).
Raw logs are stored alongside those records. Final visual fixes and source hashes
are recorded in [visual qualification](evidence/validation/ex09-presets-20260925/visual-summary.json).

## Visual review

The agent ran and inspected all seven presets. The [visual report](validation.md)
links every screenshot and records corrections to prose-heavy UI, clipped labels,
floating objects, gray-card camouflage and Indoor's disabled local shadows.
All seven have usable starting views. Indoor shadow-edge stair-stepping remains
an explicit quality observation requiring assessment; this is not a claim of
flawless rendering or shadow-filter parity.

The subsequent [Spot Cone full-profile check](#spot-cone-follow-up-full-falloff-check)
passes 2/2 focused tests in both Debug and Release. It independently verifies
31,625 pixels per render path and explains the visible inner transition as the
authored angular profile's slope change. No renderer/preset correction was needed.

## Remaining

### EX09D coverage audit in progress

The current Release exposure correctness executable was rebuilt because older
EX05 raw-manifest paths are no longer present. Its 230-case run passes 224 and
finds six failures. Two radiance endpoint cases still use the superseded
reciprocal BRDF reference instead of EX07's accepted raster model 2. Three
failure-injection cases still expect exceptions to escape although EX07 now
isolates failed views and returns failure. Those test contracts must be migrated
without weakening the image/history assertions. The sixth case, repeated half
fog history followed by float recovery, exposed the product defect below.
The initial failed run is retained; it is not presented as a passing suite. No performance benchmark or capture was repeated.

The fog diagnosis identified a product defect: temporal reuse tested status mask
18, conflating candidate FP16 rejection (bit 2) with producer failure (bit 16).
Valid history carrying quantified half-storage error was discarded on float
recovery. The bounded correction rejects producer failure only, preserving
error propagation and existing rejection of nonfinite/invalid certificates.
The original repeated-history test remains the acceptance gate, with explicit
checks distinguishing rejected candidates from failed producers. Actual failed
producer history still must be rejected; no tolerance is relaxed.

Source review also found that settings Save truncated the destination before a
complete replacement existed. A bounded local file helper now uses temporary
publication and a bounded actual read; failure/retry and invalid-file tests pass in both configurations. It does not change renderer behavior or preset values.

Screenshot runs have closed normally. The
[README](../../../../../Examples/LightBench/README.md#manual-verification-checklist) lists the
remaining interaction checks and operating commands. Do not ask the user to
repeat the agent's seven-preset appearance review. Final source-review/style work
and structured implementation commits remain. EX09D/E retain their existing
proof/operational requirements; this candidate does not claim group closure.

### Panel layout rejected during acceptance

The user rejected reserving viewport space for panels: the established contract
is a full-window scene with translucent overlaid toolbar/panels. The initial
attempt to preserve zoom while moving the viewport was also rejected. Both that
camera-aspect workaround and the opaque UI background are removed. The five
DemoShell files changed solely for inset support are restored to HEAD. Panel
width no longer participates in LightBench's viewport or camera fitting.

Earlier panel-switch images and the nine-test checkpoint record the rejected
intermediate implementation, not accepted UX. Their special dock-aspect test was
removed with the helper. Fresh overlay checks and retained settings/file results
will supersede them; previous photometry and renderer qualifications are unaffected.

## EX09 preset visual review

Reviewed 2026-09-25 in the existing **non-Tracy Ninja Release** build. All seven
presets were launched separately, captured from their visible 1920×1080 client
areas and inspected by the agent, as requested. Manual scenes settled for five
seconds; Auto scenes for eighteen seconds. Each process closed normally with
exit 0. Adjacent JSON files record the capture method, dimensions and preset;
logs contain no reported runtime warning/error. These are visual checks, not
performance measurements or renewed EX07 baselines.

The seven images below record the earlier preset appearance review. The user
subsequently rejected the reserved panel viewport. Final LightBench restores
full-window rendering with translucent overlays and defaults that render objects
20% smaller. See [final overlay evidence](validation.md); these earlier images are
not final UI acceptance. Their photometric findings remain historical evidence.

## Findings and corrections

- Removed the paragraphs describing each preset from the panel. The compact
  selector, reset/status row and controls remain; explanations are in the README.
- Reduced field widths and shortened unit labels to prevent clipped controls.
  Enabled sources appear first, so Point and Spot presets expose their controls
  immediately.
- Lowered the viewing-scene cards and spheres onto the floor. Previously their
  centers were one metre high despite their half-height/radius being 0.5 m.
- Changed directional incidence in the viewing scenes so the gray card remains
  distinguishable from the gray floor. The calibrated material values and
  Neutral/Point/Spot reference scenes are unchanged.
- Enabled both local-light shadow flags explicitly in Indoor. Previously the
  scene inherited the engine's disabled default. Point/spot shadow switches are
  now available alongside Enabled, and save/load preserves them.

## Image judgments

| Preset and screenshot                                                                        | Observed result                                                                                                                                      | Judgment                                                                                                |
| -------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------- |
| [Neutral Reference](evidence/validation/ex09-presets-20260925/screens/neutral-reference.png) | Three centered cards with distinct gray/white/black responses; compact reference table.                                                              | Suitable for the numerical reference.                                                                   |
| [Point Falloff](evidence/validation/ex09-presets-20260925/screens/point-falloff.png)         | Visible radial falloff on a broad gray receiver; distance buttons and active point controls accessible.                                              | Suitable for fixed-exposure distance comparison.                                                        |
| [Spot Cone](evidence/validation/ex09-presets-20260925/screens/spot-cone.png)                 | Clear footprint, bright center and visible edge falloff; active spot controls accessible.                                                            | Full footprint independently checked in both render paths; see below.                                   |
| [Material Lighting](evidence/validation/ex09-presets-20260925/screens/material-lighting.png) | Grounded spheres, distinct matte/glossy responses, small glossy highlight and visible cast shadows.                                                  | Suitable for direct-light material comparison.                                                          |
| [Auto Adaptation](evidence/validation/ex09-presets-20260925/screens/auto-adaptation.png)     | All cards remain distinct after settling; objects and shadows remain readable.                                                                       | Suitable starting view for adaptation steps; native tests separately verify both transition directions. |
| [Indoor](evidence/validation/ex09-presets-20260925/screens/indoor.png)                       | Warm local illumination, distinct cards, readable sphere shading and visible overlapping local shadows.                                              | Usable; shadow edges show visible stair-stepping that warrants a separate quality assessment.           |
| [Outdoor Daylight](evidence/validation/ex09-presets-20260925/screens/outdoor-daylight.png)   | White directional illumination, distinct cards and cast shadows. Settled brightness resembles the lower-lux Auto scene because exposure compensates. | Suitable daylight comparison.                                                                           |

Indoor and Outdoor are direct-lighting calibration rigs, not complete room/sky/GI
environments. Deep unlit regions are expected with environmental lighting
disabled. Source review confirms that Indoor uses the ordinary Medium per-light resolution
hint, requesting 1024-pixel local maps, and the existing production filters.
The screenshot exposes finite-map edge quality; it does not establish a new
filter correctness defect or justify arbitrary bias tuning. The known EX07 PCF
qualification remains applicable because its shader/settings contract is unchanged.
Final acceptance of this viewing quality remains with the user; keep the image
and do not hide the limitation by disabling shadows.

Earlier placement images are retained under `screens/before-placement/` for
comparison. The screenshots demonstrate scene appearance and panel layout;
they do not prove every camera/edit/save/load interaction. Final source review,
remaining user interaction acceptance and EX09D/E are tracked separately. No
overall EX09 closure or commit approval is implied.

## Spot Cone follow-up: full falloff check

The user questioned the visible inner transition. The original two-sample test
only qualified center brightness and darkness outside the cone; that was not
full-profile proof. The additional native test now checks **31,625 foreground
pixels per rendering path**, including **7,924 outside-cone pixels** required to
remain exactly black. Both spot tests pass in **Debug and Release (2/2 each)**
using the existing non-Tracy Ninja tree. No renderer or preset changes were
needed, and the existing screenshot was reused.

The independent CPU calculation combines solid-angle flux normalization,
inverse-square attenuation, authored range fade, receiver cosine and the EX07
GGX reference. It includes the complete disk and an unlit surrounding annulus.
The footprint error budget is 2e-5 of peak HDR, with the existing deferred
material storage/sampling bounds added separately. The original stricter
center-relative photometry test remains in place.

| Quantity                                    |                                                                                  Result |
| ------------------------------------------- | --------------------------------------------------------------------------------------: |
| Expected peak luminance                     |                                                                        15.7317796 cd/m² |
| Inner boundary: 3 × tan(15°)                |                                                                              0.803848 m |
| Outer boundary: 3 × tan(30°)                |                                                                              1.732051 m |
| Forward maximum absolute HDR error          |                                                    0.00005937 cd/m² (0.000377% of peak) |
| Deferred maximum absolute HDR error         |                                         0.1323704 cd/m², within material packing bounds |
| Deferred maximum excess over packing bounds |                                                                     0.00000003432 cd/m² |
| Saved screenshot versus ideal display curve | Mean 0.3261, maximum 1.5406 code values out of 255 over 502,652 pixels/all RGB channels |

The screenshot differences include material packing, dithering, display
quantization and pixel registration; the native HDR test is the numerical
acceptance gate. The screenshot curve closely follows the independent prediction:

![Captured radial falloff versus independent model](evidence/validation/ex09-presets-20260925/spot-capture-profile.png)

**Conclusion:** the visible inner ring is the expected change in slope where the
angular response leaves its constant inner region and starts its squared
cosine-space falloff. Brightness is continuous across the boundary. It is not
saturation, an Auto exposure effect, or an unexplained discontinuity in shading.
A smoother transition would require intentionally changing the authored angular
profile and its flux normalization; this check does not justify such a change.

[Test evidence/hashes](evidence/validation/ex09-presets-20260925/spot-profile-summary.json),
[Release results](evidence/validation/ex09-presets-20260925/lightbench-spot-profile-release.json.gz),
[Debug results](evidence/validation/ex09-presets-20260925/lightbench-spot-profile-debug.json.gz),
[reproducible screenshot calculation](evidence/validation/ex09-presets-20260925/check_spot_capture.py).

## Permanent preset bar: bounded visual polish

At the user's request, preset selection, Reset, Defaults/Modified and contextual
buttons now live in a separate, fixed top-center overlay. It remains available
while any sidebar panel is active. The full-window scene and translucent sidebar
contract are unchanged. Only the preset bar's local ImGui styles are adjusted.

Final Release screenshots at 1920×1080 show a **410×117 px** bar, centered with
**23 px top margin**. Font-relative sizing/padding are snapped to whole pixels;
rounded corners, no title bar/border, stronger status contrast and equal-width
context buttons keep it compact. Blue selection persists while hovering/clicking.
The popup minimum height accounts for all seven rows and padding; it no longer
shows the tiny overflow scrollbar seen in the first attempt.

| Step               | Screenshot                                                                                               | Judgment                                                                                     |
| ------------------ | -------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------- |
| 1. Default bar     | [Point Falloff](evidence/validation/ex09-completion-20260925/preset-overlay-final/point-falloff.png)     | Compact, centered, padded; 2 m selected and Defaults readable.                               |
| 2. Open selector   | [Seven options](evidence/validation/ex09-completion-20260925/preset-overlay-final/02-preset-menu.png)    | All labels visible with no scrollbar or cropped final option.                                |
| 3. Switch sidebar  | [Camera controls](evidence/validation/ex09-completion-20260925/preset-overlay-final/03-camera-panel.png) | Preset bar remains accessible at the same position; scene viewport stays fixed.              |
| 4. Change distance | [1 m selected](evidence/validation/ex09-completion-20260925/preset-overlay-final/04-dim-selected.png)    | Click changes illumination, selected button stays blue under hover, status becomes Modified. |
| 5. Reset           | [Restored default](evidence/validation/ex09-completion-20260925/preset-overlay-final/05-reset.png)       | Returns to 2 m and Defaults without changing the active Camera panel.                        |

Both Debug and Release app builds pass. This presentation-only follow-up reuses
the qualified scene/photometry tests; it does not rerun performance baselines.
Mouse actions and screenshot states were inspected. Complete keyboard/focus
regression coverage remains the separately scheduled EX08.2 work. The user's
"OMG it's nice" feedback approves the visual direction; it does not by itself
close the remaining EX09 interaction and MultiView acceptance gates.

[Current UI identities and screenshot hashes](evidence/validation/ex09-completion-20260925/preset-overlay-final/review.json).

### Final status-indicator change

The final design removes Defaults/Modified text. The final bar is
narrower and uses only a thin amber border for dirty configuration; the normal
(default) state has no visible border. Context buttons fill the available row.
Reset restores the preset and clears the outline. This supersedes the textual
status presentation in the preceding screenshot set. Current clean/dirty/reset
images are under `validation/ex09-completion-20260925/preset-border/`.

The final dirty highlight uses the existing Spectrum orange palette token,
separating modified configuration from blue selected controls.

Verified final states: [modified with amber border](evidence/validation/ex09-completion-20260925/preset-border/04-dim-selected.png)
and [Reset restored, no border](evidence/validation/ex09-completion-20260925/preset-border/05-reset.png).
Both labels are absent; the active button remains blue. Debug and Release builds
pass. [Final indicator evidence](evidence/validation/ex09-completion-20260925/preset-border/review.json).

### Popup border isolation

The user's open-popup check found the dirty color leaking into the selector's
border, including while the preset itself was clean. The parent window's border
style is now popped immediately after `Begin` renders its decoration, before
creating any popup. Actual captures verify a neutral dropdown in both
[clean](evidence/validation/ex09-completion-20260925/popup-border-scope/02-preset-menu.png)
and [dirty](evidence/validation/ex09-completion-20260925/popup-border-scope/04b-dirty-popup.png)
states; only the dirty parent retains amber. All seven entries remain visible.
[Current source and image identities](evidence/validation/ex09-completion-20260925/popup-border-scope/review.json).
