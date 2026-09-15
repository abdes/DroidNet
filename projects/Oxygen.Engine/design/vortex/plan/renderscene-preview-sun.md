# RenderScene preview sun

Status: sun implementation and native validation passed (2026-09-15).
Capture tools passed native success/failure and bounded replay-timeout checks.

## Problem and approved behavior

RenderScene loads imported glTF/GLB/FBX scenes that often contain no lights.
Its environment service currently only resolves an existing sun; its activation
helper never injects one and also forces authored sun shadows on. Persisted
environment values deliberately yield to each newly activated scene, so they
cannot serve as a dependable missing-light fallback.

The user approved adding a preview sun by default when a loaded scene has **no
directional light**. This also illuminates intentionally dark or local-light-only
scenes. A RenderScene option must disable it for authored lighting validation.

## Implementation boundary

- RenderScene adds the preview sun to the fully built, staged runtime scene,
  before publication and environment-service binding. The startup camera-only
  placeholder is excluded. CLI, restored-library, and interactive-import scene
  requests share this path.
- Detect directional components throughout the hierarchy, including invisible
  and disabled lights. Their presence suppresses injection without promoting or
  changing them. Repeated preparation must not duplicate the sun.
- Reuse DemoShell's default directional-light construction; add no atmosphere,
  skylight, fog, or exposure overrides. Cooked content remains unchanged.
- Read `render_scene.preview_sun.enabled` (default true) at startup;
  `--preview-sun=true|false` overrides it for the process without persisting the
  override. Each loaded scene owns its light; no cached fallback crosses scenes.
- EnvironmentSettingsService binds and reads authored sun properties, including
  disabled suns and directional-only scenes, without rewriting light or node
  flags during activation. Scene transitions discard pending prior-scene edits.
- Renderer, importer, and other demos retain their lighting policies. M08 is
  outside this change.

## Validation

1. Unit regressions: missing light, local-only light, untagged/disabled/hidden/
   nested directional lights, existing sun, repeated preparation, independent
   scenes, preserved environment, and authored sun properties on activation.
2. Build native RenderScene and affected DemoShell tests; run affected suites.
3. Run the refreshed `rgb_cubes` through the native app with preview on and off,
   identical camera/exposure, isolated persisted CVars, and restored settings.
   Retain load/publication logs and native GPU output images after readiness.
4. Verify an authored directional-light scene receives no additional sun.
   Exercise scene-switch/binding lifetime through regressions without desktop
   input while the user is using the computer.
5. Update the RenderScene operating guide with behavior, controls, and evidence.

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

## Evidence

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
and rendered. User settings restored again. The larger example-refresh task still
has interactive desktop checks and the full 16+4 scene visual matrix outstanding.
