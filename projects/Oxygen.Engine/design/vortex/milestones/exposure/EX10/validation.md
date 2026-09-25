# EX08–EX10 closeout

Status: **validated and closed, 2026-09-25**. Implementation and automated qualification are complete.
LightBench adaptation/resize/close, ordinary MultiView with exposure/console
controls, the offscreen layout and the carried-over directional checkbox
confirmation all have user **OK**. No implementation or acceptance item remains.
Changes are delivered in structured commits following user review.

## Delivered

- Seven complete LightBench presets: Neutral Reference, Point Falloff, Spot Cone,
  Material Lighting, Auto Adaptation, Indoor and Outdoor Daylight. Reference
  values are independently calculated, not live measurement claims.
- Complete frame-boundary reset and schema-validated atomic local save/load.
  Camera/light controls remain available. The scene fills the window behind
  translucent panels; switching panels does not change camera framing.
- A compact rounded preset overlay with a scrollbar-free popup and an amber
  modified-state border scoped to its container. Defaults frame objects 20%
  smaller than the original candidate. No Defaults/Modified text badge.
- Ordinary Release console commands for the existing scene/camera settings
  owners, explicit exposure-owner transitions and local preset/reset operations.
  Invalid complete requests are atomic; queued work is not reported as applied.
  Command retirement preserves executing callbacks and rejects stale handles.
- Build-configured Test Engine automation inside the real LightBench/TexturedCube
  apps. Development generation enables it by default; `-UiTests:$false` excludes
  the driver, fixtures and capture code for final-release builds.
- The demonstrated fog-history defect is repaired: an FP16 candidate rejection
  no longer discards quantified history during valid FP32 recovery. Producer
  failure and invalid certificates still reject history. No production precision
  policy, BRDF, shadow quality mapping or numerical tolerance was changed.

## Qualification and reuse

| Evidence                                                                                                                    | Result and scope                                                                                                                                                                                                                                                 |
| --------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| [EX08/EX09 native reference qualification](../EX09/validation.md)                                                           | Exposure suite: 230/230 Debug; 230 distinct Release cases through 224 original passes plus recorded repairs/affected checks. The original Release run is not relabelled as passing.                                                                              |
| [Final wider-framing LightBench record](../EX09/evidence/validation/ex09-completion-20260925/final-lightbench-summary.json) | 17/17 Debug; 17 distinct Release cases through 16 original passes plus the corrected outside-cone receiver probe. Tolerances unchanged.                                                                                                                          |
| [Console native checkpoint](../EX08.1/validation.md)                                                                        | 66/66 in Debug and Release: registry/policy/lifetime, settings owner, bindings and local actions.                                                                                                                                                                |
| [EX08.2 final records](../EX08.2/validation.md#results)                                                                     | In each configuration: 6/6 LightBench widget workflows, 1/1 TexturedCube workflow, and 7/7 changed CPU checks after migration. All 26 preset-tool checks pass.                                                                                                   |
| [MultiView operational record](../EX09/evidence/validation/ex09-completion-20260925/multiview-operational.json)             | Both existing 144-frame sequences, ordinary and offscreen, completed their required phases and normal shutdown. Final human ordinary/offscreen checks are accepted.                                                                                              |
| Existing MultiView validators                                                                                               | Unchanged. Layout checks retain exact target/producer sets, exposure/coverage/scene-RGB budgets and auxiliary-copy checks; interaction checks retain the required event timeline and sharing/source-loss contracts. Existing numerical proof remains applicable. |
| Build isolation                                                                                                             | Both existing Ninja trees rebuilt in Debug/Release with UI instrumentation off. Compile databases exclude test sources/defines; ordinary app binaries exclude the session marker. The UI runner rejects an ordinary tree before launch.                          |

The [test migration review](../EX08.2/README.md) records
the migration of simulated complete-reset and preset-application workflows into
actual widget tests. Codec/schema validation, file-failure injection, command
retirement/rollback and all nine numerical reference/rendering checks remain
native. The new tests replace fragile coordinate-driven interaction checks, not
the numerical oracle.

The new UI test translation units are clang-tidy clean. The prior binding review
is clean for new translation units and changed owner lines; unrelated existing
owner warnings remain documented. A repository-wide clean-lint claim is not made.

The later development-workflow update makes `UiTests` enabled by default in
`generate-builds.ps1`; final-release generation uses `-UiTests:$false`. The
actual non-Tracy Ninja development tree is now configured with Test Engine,
and VS Code's clangd resolves all three UI translation units without errors.
Manual checks confirmed that repair. Earlier disabled-build qualification records
retain their original at-run state and identities.

No EX07 benchmark, RenderDoc capture or performance baseline was repeated or
replaced. The accepted EX07D/E baseline reports remain authoritative. Original
EX05 capture paths that were transient are historical evidence, not newly
revalidated raw captures. Current native lifecycle/HDR tests, operational runs
and user checks supply the affected closeout coverage.

## Acceptance record

| Check                                                                               | Result                                                                                                                  |
| ----------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------- |
| Seven preset images and spot footprint                                              | Agent reviewed at the user's explicit request; [images, findings and corrections](../EX09/validation.md).               |
| Final LightBench adaptation, resize and normal close                                | User OK on ordinary non-Tracy Release.                                                                                  |
| Ordinary MultiView: Main/PiP, camera, exposure, resize, console `pp.targets`, close | User OK on ordinary non-Tracy Release.                                                                                  |
| Offscreen MultiView scene panes, stability, resize and close                        | User OK on ordinary non-Tracy Release.                                                                                  |
| Original directional-light checkbox regression                                      | User OK: off darkens the cards; on restores lighting. Native publication/forward/deferred pixel regression also passes. |

The console's edit/rejection/stale-target/preset/reset workflow is also tested
through the real console input widget in Debug and Release. Existing native
renderer tests remain the authority for GPU transition acknowledgement and
history semantics; the UI suite does not invent a new GPU readback channel.

## Review and operation

Start with [LightBench operation](../../../../../Examples/LightBench/README.md),
[console commands](../../../../../Examples/DemoShell/Console.md),
[UI-test setup](../EX08.2/validation.md#enable-run-restore), and
[MultiView operation](../../../../../Examples/MultiView/README.md).

The code is organized into these groups:

1. `Examples/LightBench`: scenes/presets, settings/schema/file handling, panel and
   application wiring; corresponding native and widget tests.
2. `src/Oxygen/Console` and DemoShell's settings/console bindings: lifetime,
   complete validation and explicit target/status semantics.
3. `VolumetricFog.hlsl` and affected Exposure fixtures/tests: the bounded history
   repair and migration of tests to the existing isolated-failure contract.
4. Conan/CMake, `tools/cli/oxy-ui-tests.ps1`, DemoShell's conditional test session
   and TexturedCube widget test: instrumentation isolation and real-app coverage.

Finite 1024-resolution indoor shadow edges remain an explicitly documented
quality limit, not a hidden demo-only renderer adjustment. Removed experiment
controllers, runtime measurement services and batch/report frameworks remain
removed. The user subsequently authorized structured commits.

## Structured delivery

| Commit      | Change                                                                             |
| ----------- | ---------------------------------------------------------------------------------- |
| `1ba937653` | Safe command retirement and active callback lifetime.                              |
| `3794a0711` | Certified fog history retained through FP32 recovery.                              |
| `c166b179f` | Exposure qualification aligned with renderer contracts and test-support inventory. |
| `4b99960f6` | Validated DemoShell post-process console controls.                                 |
| `18d7fea4f` | Calibrated LightBench presets, settings workflows and native tests.                |
| `8e6cb98ae` | Real-app UI automation, development build policy, runner and editor integration.   |

This closeout commit contains developer instructions, reconciled plans and the
durable evidence archive. Commit hooks normalized source/configuration formatting;
recorded capture and saved-scene fixture bytes remain unchanged. Qualification
metadata describes its original pre-commit runs, rather than being rewritten
to imply another benchmark or capture.
