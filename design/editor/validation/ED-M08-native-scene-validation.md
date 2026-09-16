# ED-M08 — Native scene loading and rendering checks

## Status

The final Release sweep `20260916-071101` passed load/capture acceptance for all
20 scenes and exited zero. Every image was independently inspected: 19 show the
expected scene forms/materials; Sponza's lit image remains dark under its authored
zero-intensity lights. All 20 results record settings and history restoration.
No new empty/off-target view or scene-obscuring fog blanket was observed in the
19 visible scenes. Single images do not establish temporal stability or parity.

This sweep uses the repaired RenderScene temporal view identity and explicit Spot
metering for InstancingTestScene. The earlier `20260916-062704` full sweep and
targeted checks remain separate evidence; no original result status was changed.

The native Debug rebuild and all eight selected native suites passed. The Debug
SDK install and subsequent MSBuild Interop rebuild completed with exit code zero.
This supporting record does not close M08.1, M08.2 or M08.3 parity qualification.

## Coverage and evidence

Native Inspector inventories identify 16 maintained scenes in
`Examples/Content/.cooked` and four imported scenes in
`Examples/RenderScene/.cooked`. They report complete version-5 scene descriptors.
The aggregate checks scene key, source key, virtual path and descriptor node
count against the preserved inventory and runtime scene summary for every result.
Runtime-created procedural nodes are distinguished from descriptor node counts.

The runner uses the maintained native RenderScene and RenderDoc export paths,
checks exact requested-scene publication before capture, and uses private CVar
archives. Each result records settings and history restoration using the runner's
comparison with the saved original files. Those original result statuses and
history files remain unchanged.

| Evidence | Result / boundary |
| --- | --- |
| [Machine aggregate](../../../artifacts/ed-m08/all-scenes/native-scene-validation.json) | Exact 20/20 inventory/result coverage, per-image observations, restoration, capture provenance and remaining gates. |
| [Build-order verification](../../../artifacts/ed-m08/all-scenes/build-order-verification.json) | Parallel Release probe: all three forced objects finished before linking; all 117 direct object inputs predate their binaries. |
| [Final native test log](../../../artifacts/ed-m08/all-scenes/final-debug-tests.log), [XML](../../../artifacts/ed-m08/all-scenes/final-debug-tests.xml) | 8/8 suites pass: AssetLoader, AsyncImportGltf, FrameContext, ModuleManager, ScriptingComponent, Scripting Module, CompilationService and Bindings. |
| [SDK install log](../../../artifacts/ed-m08/all-scenes/install-sdk.log), [Interop rebuild log](../../../artifacts/ed-m08/all-scenes/interop-rebuild.log) | Current Debug SDK installed, then Interop rebuilt with MSBuild; both exit zero. Interop's minimal log has no warning/error lines; no unprinted build-summary counts are inferred. |

Capture and source evidence live below
`projects/Oxygen.Engine/out/build-ninja/`; large images and RDC files are linked
by the aggregate rather than copied into production projects.

## RenderScene view identity and Instancing exposure

RenderScene previously left `CompositionView::view_state_handle` invalid.
`ExposurePass::Execute` returns fixed exposure when both exposure and view state
handles are invalid. RenderScene now publishes a temporal identity for its active
scene/camera session and retires it on scene publication/clear, camera replacement
or loss of a renderable window. This supplies the existing runtime requirement;
it does not change the engine's axis conventions or add qualification hooks.

Native rendered behavior was checked with unchanged authored lights, Auto mode
`2` and compensation `0`:

- The original view with no valid temporal handle was almost black.
- `20260916-065841` has a valid handle and Average metering (`0`); its cube grid
  clips almost entirely to white. The existing metering path includes the large
  black background. This preserves evidence of the unresolved metering problem.
- `20260916-070539` uses Spot metering (`2`), radius `0.30`, low/high percentiles
  `0.50`/`0.95` and target luminance `0.18`. Its reviewed image shows a useful
  colored 1,000-cube grid. Native loading reports 1,051 nodes and 1,000 renderables.
  The same explicit profile is used by the final sweep.

This is source-traced native rendered behavior, not structured GPU readback or
RenderDoc replay. The PNGs are exported embedded capture thumbnails. The Spot
profile makes this fixture useful without claiming that the specified
background and Fixed-bar exclusion from metering or authored post-process precedence is
implemented.

## Sponza source, camera and diagnostic image

The glTF camera and directional/spot-light attachment basis correction was
recooked from all four original models by the maintained reimport script.
`renderscene-reimport/20260916-021724-9fae276d/result.json` records successful
publication to the standard RenderScene cooked root; original-source hashes,
the manifest, validation output and protected prior content are retained there.

The original Sponza glTF contains 24 authored lights, all with intensity zero.
The corrected scene has 162 native nodes, 115 renderables, six perspective
cameras, one directional light and 23 point lights. The original artist camera
is retained. No camera profile or authored-light override is applied; the native
log records that the preview sun is skipped because a directional light exists.
A dark lit image therefore remains expected for this authored lighting setup.

`20260916-065903` uses the existing Base Color diagnostic mode `7`. Its reviewed
image shows the textured courtyard from the preserved artist camera. It proves
useful geometry/material/camera inspection independently of illumination; it is
not a lit-scene result, light parity proof or an authored-light repair. The final
full sweep uses the normal lit mode for Sponza and retains that distinction.

## Procedural workload

The maintained `proc-cubes/import-manifest.json` source recipe is restored to
6,000 cubes. Both the `20260916-062704` and final `20260916-071101` procedural
case logs record peak draws of 6,000. Its native descriptor contains three
nodes; the script creates the workload at runtime. A reduced earlier recipe is
not used as final workload evidence.

## Remaining gates

- Complete subsequent integration and required managed qualification; a successful
  Interop rebuild alone does not prove editor integration behavior.
- M08.2 retains background and Fixed-bar exclusion from metering and authored
  post-process precedence. The exposure profiles above do not close those gates.
- M08.3 still requires the specified standalone visual parity qualification;
  these maintained-scene captures are supplemental native evidence.

Runtime fixes remain normal production behavior. Capture runners, temporary
profiles, generated evidence and source-recovery material remain in the build
tree or `artifacts/ed-m08`; no example schema or qualification payload is added
to normal engine, editor or SDK runtime builds.
