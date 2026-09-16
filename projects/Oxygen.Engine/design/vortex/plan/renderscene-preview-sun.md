# RenderScene preview sun

Status: **validated and closed for the preview-sun/profile workflow** on
2026-09-16. Debug and Release builds, 97 focused tests, scripted native checks,
and user-performed visual/UI validation support this scope. Sponza's non-sun
lighting defects and the separate exposure correction remain open.

## Problem and approved behavior

RenderScene must show which environment and sun are in use, what a preview will
change, and how to return to the authored scene. Preview sun is an independent
opt-in control across **Use Scene**, all five premades, and **Custom**. It is not
another profile. A new settings file starts with Use Scene and preview off.

Use Scene restores the loaded scene's authored environment. Its sole optional
addition is the explicit preview-sun toggle. Premades supply their declared
environment and exposure values; Custom restores saved environment edits.
Switching away from Custom retains its values. An explicit environment-property
edit becomes Custom; changing preview does not change the selected profile.

Preview is available only when no directional light has `IsSunLight` or an
explicit **Primary**/**Secondary** atmosphere slot. Hidden or disabled authored
suns still reserve that designation. Prefer an existing directional light,
choosing a visible candidate first; create a temporary directional only when no
candidate exists. Source models and cooked files remain unchanged.

## Implementation boundary

- `PreviewSunController` owns eligibility, deterministic candidate inspection,
  borrowed-role restoration, and temporary-light lifetime. It checks the whole
  hierarchy, including hidden and inactive nodes, after scene readiness.
  Reconciliation must not duplicate lights or carry a candidate across scenes.
  Active preview owns Primary independently of the selected profile. Native light
  mutation observers relinquish a conflicting preview role without recursive
  observer dispatch; temporary-node cleanup occurs in the normal apply phase.
- Borrowing preserves intensity, color, transform, visibility, and shadows. It
  temporarily enables world lighting, environment contribution, `IsSunLight`,
  and Primary. Disabling restores the original flags and slot; it does not delete
  the scene light. A new authored sun supersedes a preview.
- A temporary `Preview Sun` uses DemoShell's default directional construction:
  100,000 lux, warm white, 0.53-degree source angle, shadow bias 0.03, and Oxygen's
  Z-up basis. Disabling removes it. The toggle itself adds no atmosphere, sky
  light, fog, cubemap, or exposure adjustment. A selected profile may apply its
  own sun and environment values afterward.
- `EnvironmentSettingsService` owns RenderScene's persisted selection, separate
  Custom state, preview preference, and runtime profile application.
  `EnvironmentSceneSnapshot` restores the service-owned authored environment,
  directional lights/rotation, and local fog when returning to Use Scene. Other
  scene systems and topology remain intact. The startup camera-only placeholder
  is excluded; scene changes discard stale pending work. Publication defers
  synchronization and preview activation until runtime binding captures the
  untouched scene, preventing temporary roles from becoming the authored baseline.
- Skybox requests have generation and weak-lifetime guards. Switching profile or
  source cancels pending loads before a stale completion can pin, equip, or
  overwrite status. A stored skybox path alone must not inject a map into Use Scene.
- UI selection and edits persist; CLI startup choices remain transient, including
  premade exposure values. Exposure arithmetic and renderer policy belong to the
  separate exposure correction plan, not this example UX change.

## User-facing controls

**Environment → Profile** precedes **Preview sun**. State-aware help is visible
and wrapped, not confined to a tooltip:

- Before a scene is ready, explain that a scene must be loaded.
- When unavailable, name the authored sun blocking preview and explain why its
  assignment is preserved, including hidden/off sun reservations.
- When available and unchecked, name the candidate and describe borrowing it;
  if no directional exists, describe creating a temporary `Preview Sun`.
- When checked, name the actual light, distinguish borrowed from created, and
  explain precisely what unchecking restores or removes. Explain an inactive or
  zero-illuminance light rather than implying the checkbox guarantees brightness.
- State that scene files stay unchanged. With no selected sun, show guidance
  instead of ineffective editable sun controls. Keep renderer diagnostics under
  collapsed **Runtime details**.

The public CLI **Environment** help group contains:

| Option | Contract |
| --- | --- |
| `--environment-profile <key>` | Exact keys: `scene`, `custom`, `outdoor-sunny`, `outdoor-cloudy`, `foggy-daylight`, `outdoor-dawn`, `outdoor-dusk`. Invalid keys fail with the allowed choices. |
| `--preview-sun=true\|false` | Overrides `render_scene.preview_sun.enabled` for this process, independently of the profile. The persisted default is false. |

The existing advanced `--startup-skybox <path>` routes through Custom. With no
explicit profile it selects transient Custom; combining it with an explicit
non-Custom profile is rejected. Runtime logs identify the resolved choices and
preview decision. Explicit UI changes after CLI startup are ordinary saved edits.

## Validation

1. Unit regressions cover missing lights, untagged directional reuse, visible
   candidate preference, hidden/off authored suns, Primary/Secondary blocking,
   candidate identity, exact role restoration, scene lifetime, and idempotence.
   Exercise profile selection, Custom restoration, Use Scene restoration,
   transient CLI choices, preset exposure persistence, and canceled skybox work.
2. Build native RenderScene in Debug and Release and run affected DemoShell suites.
3. Use a dedicated lightweight native scene fixture with no directional, an
   untagged directional, an authored Primary, and a hidden/off authored sun. Keep
   fixture/schema/tooling under the build tree, outside normal example/SDK builds.
4. Capture preview off/on in Use Scene and Custom, and exercise all five premades.
   Verify the actual candidate/source, light count, appearance, restored authored
   values, and unchanged profile when toggling preview. Inspect wrapped UI help in
   every state; a log or unit assertion alone does not qualify its readability.
5. Exercise Custom edits, profile switching, save/relaunch, CLI overrides without
   saving environment/exposure changes, and stale skybox cancellation. Preserve
   settings/CVars/history around qualification and retain before/after evidence.
6. Use Sponza only for final integration proof after the small fixture passes.
   Do not compensate for unrelated renderer or exposure defects with a skybox or
   renderer changes. Keep those visual gates open with their own evidence.
7. Update the operating guide and evidence record with the precise verified
   scope; distinguish unit, runtime, UI, and visual outcomes.

## Current qualification evidence

The [closeout record](../../../../../artifacts/ed-m08/preview-profiles/closeout.json)
identifies logs, captures, settings protection, manual acceptance and open defects.
Native artifacts are under engine `out/build-ninja/ed-m08/preview-profiles/`.

| Check | Result and boundary |
| --- | --- |
| Native builds | Debug succeeds in `build-debug-9.log`; Release is current in `build-release-final.log`. No renderer or exposure-arithmetic change is included. |
| Focused tests | **97/97** across six suites: DefaultSceneLighting 10, PreviewSunController 18, EnvironmentSceneSnapshot 8, SkyboxService 6, EnvironmentSettingsService 46, EnvironmentVm 9. See `tests-debug-9.log` and XML. |
| Dedicated fixture | **13/13** native cooking jobs and Inspector validation pass. Five small scenes cover absent, untagged, authored, hidden/off, and isolated sun-only lighting. Camera framing includes the sky; axes and identities are checked in `fixture/camera-sun-only-validation.json`. |
| Native scenarios | **18/18** runs in `matrix-20260916-085555` exit zero and pass final preview-state, scene-publication and persistence checks. Coverage includes preview off/on, all five premades, Custom, and saved-Custom relaunch. Native embedded capture images are retained. |
| CLI rejection | Unknown profile and conflicting Scene/skybox options exit nonzero without changing settings; see `cli-rejection-results.json`. |
| Manual visual/UI acceptance | The user confirmed the requested help readability, Custom edit/reopen persistence, sun-only behavior, and final Sponza sun/profile checks on 2026-09-16. This is user-reported acceptance; no additional assistant-operated Sponza capture is claimed. |

The native scenario matrix predates the final observer/contrast refinement; the
97-test run and user manual acceptance cover the final implementation. Earlier
captures with lost preview ownership are retained as failed investigation evidence,
not final acceptance.

**Remaining defects outside this closeout:** Sponza lighting/rendering from
sources other than the Sun remains broken, as confirmed during manual acceptance.
The user explicitly accepted closure of this workflow with that issue open.
Exposure correction remains in
[its owning plan](exposure-and-lightbench-correction.md). These checks do not close
ED-M08 native data, renderer parity, captured-sky IBL, or full visual qualification.

## Capture tooling correction

The user also requires the touched scripts to be robust. The attempted replay
export hit a broken bundled PySide2 import, and launching hidden did not prevent
RenderDoc's UI from appearing. Remove the failed exporter and Qt shutdown path.

- Extract the original embedded capture image with a reusable wrapper around
  `renderdoccmd thumb`. Validate native status, a fresh PNG, dimensions, and hashes;
  preserve an existing output when export fails. Document the image's resolution
  limit and distinguish it from replay output.
- Run automated replay analyzers through RenderDoc's supported startup `--python`
  mode. A bootstrap must always exit before UI startup, including import and
  analysis failures. Reuse the shared controller/report callback interface and
  explicitly release replay resources. No PySide2 or window-closing workaround.
- Keep manual analysis within an already-open UI available through the existing
  helper, without pretending an opaque widget can close the application.
- Exercise successful analysis, invalid captures/analyzers, quoted paths, native
  image export, and supported size limits. Failure must exit nonzero and preserve
  useful diagnostics; a report alone cannot hide an execution failure.

## Historical evidence: 2026-09-15 policy and capture tooling

The evidence below retains its original scope: default-on injection only when no
directional existed, before independent opt-in preview and profile restoration.
It does not validate the current candidate-reuse or ImGui contract. The capture
tooling results remain evidence for those tools, not new preview behavior.

Workspace evidence: `artifacts/renderscene-preview-sun/evidence.json`, with
commands, logs, settings, executable/model/capture/image hashes, and test XML.

- Debug native RenderScene build passed (`final-build.log`, no compiler warnings).
- DefaultSceneLighting: 9/9 tests passed. EnvironmentSettingsService: 36/36
  passed, including forced-custom policy preservation and disabled parented sun
  selection/re-enable across scene changes.
- Original `rgb_cubes.fbx` reimported through `reimport_scenes.ps1` with BC7/Full
  into an isolated library; native Inspector and descriptor hashes passed.
- The four runs below exited 0 and restored the exact user settings hash
  `D55F5EA7B9CE89F815982C43D70612FBF63AAD53DC1A9D7E58AFEE73C4B093B5`.
- Reimport policy tests passed: None/Max3 (3 mips), BC7/Full (11 mips), and
  None/None (1 mip), with native texture-table and payload checks. Four invalid
  parameter combinations failed before creating output artifacts.
- Capture tooling: six replay/bootstrap unit tests passed. Native action-dump
  analysis passed in PowerShell 7 and Windows PowerShell 5.1. Callback, syntax,
  invalid-capture, and timeout cases were rejected. After removing output pipes,
  repeated 1-second timeouts returned in 1.08–1.69 seconds in PowerShell 7 and
  2.73 seconds in Windows PowerShell 5.1, with no leftover owned processes.
- Native thumbnail extraction, size limiting, paths with spaces, and invalid
  capture/output preservation passed. Automated replay uses `--python`; manual
  execution within an open RenderDoc UI was not exercised.
- A controlled sleeping thumbnail-tool fixture verified the timeout branch:
  failure returned in 1.646 seconds, the child exited, the previous PNG hash
  stayed unchanged, and a partial temporary export was removed.

| Native run | Observed result |
| --- | --- |
| `rgb-on-ready` | 1,383 authored nodes, 1,374 renderables, 4 point lights, no directional light. One preview sun added before publication; models visibly lit. |
| `rgb-off-ready` | Same scene, camera, and EV14 exposure; CLI preview disabled. Models nearly dark under the existing local lights. |
| `cube-preserve` | Authored directional light retained; preview skipped. Explicit CLItrue overrides persistedfalse. Scene visibly rendered. |
| `persisted-off` | EmissiveScene, no directional light. Persistedfalse respected with no CLI override; no preview added. |

Visual evidence uses the capture's original embedded image, extracted with
RenderDoc's native `renderdoccmd thumb` command (2048×1120 for these captures).
This is the image recorded by the running app. A replay export in this session
showed black scene content and is not accepted as native appearance evidence.
The failed replay shutdown attempt and insufficient-frame-budget runs are retained
as diagnostics, not successful validation. No CUA, mouse, or keyboard control
was used.

The Debug/hash-verified RGB dependency walk took about 57 seconds. Capture frame 1500
followed loaded-scene publication by about 12 seconds. The shorter attempts ended
before loading completed. This latency is a separate serial dependency-loading
issue, not part of the sun correction.

The corrected full original-source generation was subsequently published through
the script: four scenes, 3,537 assets, 239 imported textures, BC7 with full mips.
All 8,131 publication-file hashes, four original source hashes, and 73 external
dependency hashes passed an independent audit (`own-final-audit.json` under
`artifacts/native-examples-refresh`). Town now has 1,249 indexed and physical
materials, with all material values, image mappings, and mesh bindings checked.
Nine case-only pairs retain distinct identities; four have different rendered
values and five resolve equivalently, matching the source.

Native run `final-library-mount` mounted that generation with hash verification:
all 3,537 descriptor hashes checked, none missing/skipped, then CubeScene loaded
and rendered. User settings restored again. That historical evidence alone does not establish the full 16+4 scene visual
matrix. The later [native scene record](../../../../../design/editor/validation/ED-M08-native-scene-validation.md)
owns that coverage and its remaining renderer limits.
