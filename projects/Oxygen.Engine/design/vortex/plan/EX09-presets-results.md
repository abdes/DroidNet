# EX09 usable preset candidate

Status: historical preset qualification, followed by final EX09 validation and
user acceptance on 2026-09-25. See [EX10 closeout](EX10-completion.md) for current
status and [EX08.2](EX082-execution.md) for the real-app widget suite.
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
The table below uses the widened default framing; [final records](validation/ex09-completion-20260925/final-lightbench-summary.json)
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

[Evidence summary and hashes](validation/ex09-presets-20260925/summary.json),
[Debug suite](validation/ex09-presets-20260925/lightbench-ex09-all-debug.json.gz),
[initial Release suite](validation/ex09-presets-20260925/lightbench-ex09-all-release.json.gz),
[corrected Release step case](validation/ex09-presets-20260925/lightbench-ex09-steps-release.json.gz).
Raw logs are stored alongside those records. Final visual fixes and source hashes
are recorded in [visual qualification](validation/ex09-presets-20260925/visual-summary.json).

## Visual review

The agent ran and inspected all seven presets. The [visual report](EX09-presets-visual-review.md)
links every screenshot and records corrections to prose-heavy UI, clipped labels,
floating objects, gray-card camouflage and Indoor's disabled local shadows.
All seven have usable starting views. Indoor shadow-edge stair-stepping remains
an explicit quality observation requiring assessment; this is not a claim of
flawless rendering or shadow-filter parity.

The subsequent [Spot Cone full-profile check](EX09-presets-visual-review.md#spot-cone-follow-up-full-falloff-check)
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
[README](../../../Examples/LightBench/README.md#user-ui-acceptance) lists the
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
