# ED-M02 Live Viewport Stabilization

Status: `review`

## 1. Purpose

Finish and validate the embedded live-engine viewport baseline so later
authoring milestones can rely on it as visual evidence.

ED-M02 is a stabilization milestone, not a new authoring-tool milestone. The
current codebase already has engine startup after workspace activation,
`SwapChainPanel` surface attachment, engine view creation, one/two/three/four
layout menus, runtime FPS/logging controls, camera preset calls, cooked-root
refresh, and a visible Vortex-rendered viewport. This plan turns that existing
behavior into a validated product baseline with clear diagnostics and manual
visual evidence.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-001` | The editor can open a project and show a usable live viewport. |
| `GOAL-003` | The embedded Vortex preview is stable enough to support future authoring validation. |
| `GOAL-006` | Runtime/viewport failures are visible and useful instead of being log-only or assertion-only. |
| `REQ-022` | Scoped: ED-M02 runtime startup, cooked-root refresh, surface, view, layout, and runtime-setting failures produce visible operation results or output/log diagnostics. |
| `REQ-023` | Scoped: ED-M02 engine/runtime viewport failures produce useful logs correlated to document/viewport/runtime state where known. |
| `REQ-024` | Scoped: ED-M02 diagnostics distinguish missing content/cooked-root state, surface/view state, settings rejection, and engine runtime state. |
| `REQ-025` | A live embedded viewport renders the active scene. |
| `REQ-027` | One/two/four viewport layouts are stable. |
| `REQ-028` | Each viewport presents to the correct editor surface. |
| `REQ-030` | Partial: ED-M02 establishes the embedded preview path; full preview parity remains ED-M08. |
| `SUCCESS-003` | Users can see the scene in the editor viewport. |
| `SUCCESS-005` | Viewport interaction is stable enough for later viewport authoring tools. |

`REQ-029` is not claimed by ED-M02. Initial camera framing is validation
evidence for `REQ-025` and `SUCCESS-003`; explicit camera navigation and frame
selected/all belong to ED-M09.

## 3. Required LLDs

Only these LLDs gate ED-M02:

- [runtime-integration.md](../lld/runtime-integration.md)
- [viewport-and-tools.md](../lld/viewport-and-tools.md)
- [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md)

The LLDs must be accepted before implementation starts. This detailed plan may
be revised if LLD review changes scope.

## 4. Scope

ED-M02 includes:

- Engine startup during workspace activation before cooked-root refresh.
- Runtime state guards so mount/surface/view/settings calls fail visibly
  instead of relying on debug assertions.
- Existing native runtime discovery from the engine install runtime directory.
- Existing cooked-root refresh ordering and non-fatal `AssetMount` warnings.
- Surface lease attach, resize, release, and surface-limit diagnostics.
- Engine view create, destroy, and camera preset call diagnostics.
- One-pane, representative two-pane, and four-quadrant viewport validation.
- Correct surface presentation evidence for every visible viewport.
- Resize/coalescing behavior where latest measured viewport size eventually
  reaches the runtime.
- Runtime FPS/logging setting success or visible rejection.
- Manual visual validation instructions and ledger evidence for the user.

## 5. Non-Scope

ED-M02 does not include:

- Selection, picking, transform gizmos, overlays, non-geometry node icons, or
  frame selected/all commands.
- Scene mutation commands, dirty state, undo/redo, save/reopen semantics, or
  live scene sync beyond existing scene-load behavior.
- Content import, descriptor generation, cooking, pak, catalog refresh, or
  cooked-index policy.
- Standalone runtime validation or full preview parity.
- Material, physics, environment, or component inspector authoring.
- Replacing the WinUI `SwapChainPanel` surface seam with an abstract handle.
- Reworking three-pane layout variants unless touched incidentally.

## 6. Implementation Sequence

### ED-M02.1 - Baseline Audit And Diagnostics Vocabulary

Lock down what currently works before hardening the code.

Tasks:

- Confirm current route from Project Browser open to workspace runtime startup,
  cooked-root refresh, scene load, layout creation, surface attach, view create,
  and first visible presentation.
- Decide and record where ED-M02 operation-kind string constants live. The
  recommended shape is static string constants, not a new enum: runtime
  operation kinds in the runtime-producing project, viewport operation kinds in
  the viewport-producing project, and shared diagnostic code prefixes in
  `Oxygen.Core` only when multiple producers consume them. Strings must match
  [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md)
  exactly:
  - `Runtime.Start`
  - `Runtime.Settings.Apply`
  - `Runtime.Surface.Attach`
  - `Runtime.Surface.Resize`
  - `Runtime.View.Create`
  - `Runtime.View.Destroy`
  - `Runtime.View.SetCameraPreset`
  - `Runtime.CookedRoot.Refresh`
  - `Viewport.Layout.Change`
- Add or verify diagnostic code prefixes for runtime and viewport failures,
  such as `OXE.RUNTIME.*`, `OXE.SURFACE.*`, `OXE.VIEW.*`,
  `OXE.VIEWPORT.*`, `OXE.SETTINGS.*`, and `OXE.ASSET_MOUNT.*`.
- Do not introduce an operation-kind enum unless
  [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md)
  is updated first.
- Map ED-M02 producers to active domains:
  - runtime startup/discovery -> `RuntimeDiscovery`
  - surface attach/resize/release -> `RuntimeSurface`
  - view create/destroy/preset -> `RuntimeView`
  - cooked-root refresh -> `AssetMount`
  - FPS/logging setting writes -> `Settings`
  - layout restoration/change failure -> `WorkspaceRestoration`
- Record any already-landed behavior in comments only where the code seam is
  otherwise unclear; avoid adding diagnostic noise.
- Identify a safe seam for runtime startup/discovery failure validation. If no
  safe seam exists in ED-M02, record the skipped failure-path verification and
  reason in the validation evidence.

Required behavior:

- Operation result and diagnostic contracts remain UI-independent.
- Runtime/viewport diagnostics include project, document, viewport, surface, or
  view IDs when known.

Validation:

- Unit tests cover new diagnostic mapping helpers if they are not trivial data
  constants.

### ED-M02.2 - Runtime Startup And Cooked-Root Refresh Hardening

Make the workspace/runtime ordering explicit and non-crashy.

Tasks:

- Verify workspace activation awaits `IEngineService.InitializeAsync` /
  `StartAsync` before any cooked-root refresh.
- Ensure cooked-root refresh exits with a visible `AssetMount` warning when the
  `.cooked` root or index is missing/unmountable.
- Ensure missing cooked roots do not block workspace entry or viewport
  creation.
- Convert ED-M02-owned invalid-state cooked-root refresh, surface, view, and
  runtime settings paths into visible diagnostics or explicit operation
  results.
- If a native/interop path cannot be made user-visible in ED-M02, record the
  exact limitation in the ED-M02 validation evidence.
- Keep exact cooked-index layout and refresh-after-cook policy out of ED-M02.

Required behavior:

- Workspace remains usable when cooked roots are absent.
- A failed refresh is visible in the output/log panel with enough project path
  information to diagnose it.
- No Project Browser path starts the engine.

Validation:

- Manual open of a valid project with cooked output shows the viewport.
- Manual or controlled missing-cooked-root run opens the workspace and records a
  warning instead of aborting.

### ED-M02.3 - Surface Lease And Resize Stabilization

Make surface ownership reliable across layout and window changes.

Tasks:

- Audit `IViewportSurfaceLease` attach/resize/dispose behavior for idempotent
  teardown.
- Ensure viewport unload cancels pending attach/create/resize work where
  possible and still destroys the view before releasing the lease.
- Ensure resize requests may coalesce but the latest measured panel size is
  eventually submitted while the lease remains active.
- Add `RuntimeSurface` diagnostics for attach failure, resize failure, release
  failure, orphaned viewport ID, and surface-limit rejection.
- Keep all `SwapChainPanel` access in the WinUI viewport host path.
- Preserve distinct document/viewport IDs in logs for every surface request.

Required behavior:

- Closing/reopening a scene releases old document surfaces and does not leak
  surface reservations.
- Switching layout does not leave stale surfaces visible.
- Surface-limit failure is visible with reason `LimitExceeded` if it occurs.

Validation:

- Targeted tests cover pure lease bookkeeping/state-machine behavior where the
  current runtime code can be tested without WinUI.
- Manual validation includes resize and close/reopen checks.

### ED-M02.4 - Engine View Lifecycle And Camera Preset Diagnostics

Make view create/destroy/preset calls observable and safe.

Tasks:

- Ensure viewport UI initiates view lifecycle but native view calls go only
  through `IEngineService.CreateViewAsync`, `DestroyViewAsync`, and
  `SetViewCameraPresetAsync`.
- Add or harden `RuntimeView` diagnostics for view create failure, destroy
  failure, invalid view ID, and camera preset failure.
- Ensure failed view creation leaves the surface lease disposable.
- Ensure destroy failure does not prevent control teardown.
- Ensure camera preset calls do not fault the runtime or the UI.
- Keep explicit camera navigation and frame selected/all out of scope.

Required behavior:

- A view failure is tied to the affected document/viewport where known.
- Camera preset failure is reported or logged as `Runtime.View.SetCameraPreset`
  / `RuntimeView`.

Validation:

- Manual validation exercises at least one camera preset from the viewport menu
  and confirms no runtime/UI fault.

### ED-M02.5 - Viewport Layout Matrix Stabilization

Validate one/two/four viewport layouts against the real UI.

Tasks:

- Verify `SceneEditorViewModel`, `SceneLayoutHelpers`, and
  `SceneEditorView.xaml.cs` produce stable placements for:
  - `OnePane`
  - one representative two-pane layout, preferably `TwoMainLeft`
  - `FourQuad`
- Ensure `ViewportViewModel` instances have distinct viewport IDs and clear
  diagnostic visual cues for each visible surface.
- Ensure layout change failure publishes `Viewport.Layout.Change` only when the
  change cannot be applied, restoration fails, or runtime surface/view work
  fails.
- Avoid expanding the validation matrix to three-pane layouts unless the
  implementation touches them.

Required behavior:

- One/two/four layouts do not crash, abort, or present stale/blank panels after
  the scene is loaded.
- Every visible viewport can be correlated to a distinct document/viewport ID in
  logs.
- Every visible viewport has visual evidence of correct routing, such as
  distinct clear color per viewport or distinct camera orientation per
  viewport. Any other cue must be agreed before validation and recorded in the
  ledger row.

Validation:

- Manual validation records one/two/four screenshots or notes.
- Logs record distinct document/viewport IDs for each visible viewport.

### ED-M02.6 - Runtime Settings Surface

Make existing FPS/logging controls honest.

Tasks:

- Verify scene editor FPS and logging controls reflect `IEngineService` state.
- Clamp FPS values through existing runtime limits and report failures.
- Publish `Runtime.Settings.Apply` / `Settings` diagnostics when FPS/logging
  writes are rejected or attempted in an invalid runtime state.
- Present settings failures inline near the scene editor setting when practical
  and always in the output/log panel.
- Avoid inventing a broader editor settings architecture in this milestone.

Required behavior:

- A successful setting write visibly updates or remains consistent with the UI.
- A rejected setting write does not pretend success.

Validation:

- Manual validation changes FPS/logging values in a running workspace.
- A simulated or forced invalid-state settings write produces a visible
  `Settings` diagnostic if a straightforward test seam exists.

### ED-M02.7 - Build, Tests, And Manual Visual Validation

Close the milestone with explicit evidence.

Tasks:

- Add/update focused unit tests for:
  - runtime state guards and invalid-state diagnostics where test seams exist.
  - surface lease bookkeeping where WinUI-independent.
  - layout helper placements.
  - diagnostic operation-kind/domain mapping where touched.
- Decide the runtime test location before adding tests. Create
  `projects/Oxygen.Editor.Runtime/tests/` only if at least one pure,
  WinUI-independent helper is extracted from runtime code; otherwise keep
  runtime service behavior under integration/manual validation and record that
  decision.
- Run repository build/test validation through MSBuild and the existing test
  runner only. Do not use `dotnet build`.
- Prepare a manual validation checklist for the user with exactly what should
  be visible.
- Update [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) ED-M02
  checklist and validation ledger after user validation.

Required behavior:

- No implementation is considered complete until manual one/two/four visual
  validation is possible for the user.
- Any skipped test or manual validation item is recorded against ED-M02, not
  hidden in a generic gap list.

Validation:

- MSBuild succeeds for the affected solution/projects.
- Targeted tests pass or skipped tests are explicitly recorded.
- User validates the visible viewport matrix.

## 7. Project/File Touch Points

Expected primary touch points:

- `projects/Oxygen.Core/src/Diagnostics/`
  - operation-kind constants or diagnostic-code additions if not already
    present.
  - failure-domain mapper/status helpers if ED-M02 needs new behavior.
- `projects/Oxygen.Core/tests/Oxygen.Core.Tests.csproj`
  - operation-kind/domain/status mapping tests where behavior is added.
- `projects/Oxygen.Editor.Runtime/src/Engine/`
  - `EngineService.cs`
  - `EngineService.surfaces.cs`
  - `EngineService.views.cs`
  - `IEngineService.cs`
  - `IViewportSurfaceLease.cs`
  - `ViewportSurfaceRequest.cs`
  - runtime setting failure handling.
- `projects/Oxygen.Editor.Runtime/tests/`
  - does not exist today. Add it only if ED-M02 extracts pure,
    WinUI-independent runtime state/lease helpers worth unit-testing; otherwise
    record that runtime service behavior is integration/manual validated.
- `projects/Oxygen.Editor.WorldEditor/src/Workspace/`
  - `WorkspaceViewModel.cs` cooked-root refresh ordering and warnings.
- `projects/Oxygen.Editor.WorldEditor/src/SceneEditor/`
  - `SceneEditorViewModel.cs`
  - `SceneEditorView.xaml.cs`
  - `Viewport.xaml.cs`
  - `ViewportViewModel.cs`
  - `SceneLayoutHelpers.cs`
  - logging partials for diagnostic evidence.
- `projects/Oxygen.Editor.WorldEditor/tests/SceneExplorer/Oxygen.Editor.WorldEditor.SceneExplorer.Tests.csproj`
  - layout helper or viewport metadata tests where WinUI-independent.
- `projects/Oxygen.Editor.WorldEditor/src/Output/`
  - output/log panel integration only if ED-M02 diagnostics need new
    presentation support beyond ED-M01.
- `projects/Oxygen.Editor.Interop/test/`
  - existing runtime interop tests may be extended if the change touches
    runtime settings/view operations.

Runtime service behavior remains integration/manual validated in ED-M02 unless
a pure helper is extracted. Extracted pure helpers are tested in their owning
test project.

Expected project/reference constraints:

- `Oxygen.Editor.WorldEditor` may depend on `Oxygen.Editor.Runtime`.
- `Oxygen.Editor.Runtime` remains the only managed runtime service calling
  `Oxygen.Editor.Interop` for engine operations.
- Project Browser must not regain `IEngineService` or runtime dependencies.
- Runtime services must not depend on WorldEditor UI types.
- Viewport UI must not call C++/CLI interop directly.

## 8. Dependency And Migration Risks

| Risk | Mitigation |
| --- | --- |
| ED-M02 already has partially landed behavior, so a plan could accidentally rewrite stable code. | Treat current implementation as baseline and harden only the seams required by the LLDs. |
| Runtime invalid-state calls can assert before producing user-visible diagnostics. | Add service-level guards and operation-result/output-log diagnostics before UI paths call runtime operations. |
| Multi-viewport validation can pass visually while surfaces are misrouted. | Require distinct document/viewport IDs in logs and visually distinguishable evidence per viewport. |
| Resize behavior is timing-sensitive and can become flaky. | Coalesce resize requests but guarantee latest measured size is eventually submitted while the lease is active. |
| Missing cooked roots can be mistaken for a content-pipeline failure. | ED-M02 only reports non-fatal `AssetMount` warnings; ED-M07 owns cooked-index policy and cook/mount refresh after cooking. |
| Runtime settings UI can silently log failures. | Convert settings write failures into `Runtime.Settings.Apply` results and output/log diagnostics. |
| `Oxygen.Editor.Runtime` currently exposes a WinUI `SwapChainPanel` surface attach seam that is not fully documented in the architecture brownfield register. | ED-M02 accepts the existing seam and keeps `SwapChainPanel` access at the surface attach boundary only. A brownfield-register architecture update should be filed alongside this plan before a later abstraction milestone. |
| Manual validation is required because no managed presented-frame signal exists. | Make screenshots or structured manual notes part of ED-M02 closure. |
| Build validation can drift from repository expectations. | Use MSBuild and the existing repository test runner only; do not use `dotnet build` or alternate build trees. |

## 9. Validation Gates

ED-M02 can close only when:

- Normal launch still starts at Project Browser without initializing or
  starting the engine.
- Opening the Vortex project starts the runtime after project/workspace
  activation and before cooked-root refresh.
- Native runtime DLLs load from the engine install runtime directory.
- One-pane scene viewport renders the active scene.
- Representative two-pane layout renders each visible viewport to the correct
  surface.
- Four-quadrant layout renders each visible viewport to the correct surface.
- Switching between one/two/four layouts does not crash, abort, or leave
  blank/stale panels.
- Three-pane variants are not ED-M02 validation targets, but each exposed
  three-pane variant gets a smoke check: open it and confirm it does not crash,
  abort, or assert. Record pass/skip in the ledger evidence.
- Resizing the window and panes keeps viewports scaled and non-overlapping.
- Closing/reopening the scene releases old surfaces/views and creates new
  surfaces/views.
- Default editor camera observes authored content without clipping the whole
  scene.
- Camera preset menu calls do not fault the runtime/UI.
- FPS/logging controls apply or produce visible diagnostics.
- A rejected or invalid-state runtime settings write produces a visible
  `Settings` diagnostic, or the untested failure path is recorded with reason.
- Runtime startup/discovery failure is verified as a visible
  `RuntimeDiscovery` diagnostic where a safe test seam exists; otherwise the
  skipped failure-path verification is recorded with reason.
- Missing/unmountable cooked roots produce non-fatal `AssetMount` warnings.
- Surface/view/settings failures produce operation-result or output/log
  diagnostics with affected document/viewport where known.
- Required MSBuild/test validation passes using the repository build system and
  existing tree only; no `dotnet build` and no alternate build tree. Skipped
  verification is recorded.

Manual visual validation expectations for the user:

- After opening Vortex, the central scene viewport shows rendered scene content,
  not a blank panel.
- In one-pane layout, one live viewport fills the scene editor area.
- In two-pane layout, two live viewports are visible and independently routed;
  they must not show stale copies of the same surface.
- In four-quadrant layout, four live viewports are visible, stable, and
  correctly scaled.
- Acceptable distinct-viewport cues for ED-M02 are distinct clear color per
  viewport or distinct camera orientation per viewport. Any other cue must be
  agreed before validation and recorded in the ledger row.
- Resizing the editor window or docked panes updates viewport sizes without
  leaving black/stale rectangles.
- FPS/logging controls either apply or show a visible failure result.

## 10. Status Ledger Hook

When ED-M02 implementation and validation land, update:

- [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) ED-M02 checklist.
- [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) validation ledger
  with one concise ED-M02 evidence row.

The ledger evidence should record:

- launch path and runtime startup timing.
- engine runtime DLL discovery path.
- build configuration and MSBuild command used.
- test command(s) used, if any.
- one/two/four viewport validation result.
- correct surface presentation evidence.
- runtime settings result.
- cooked-root warning behavior, if exercised.
- user manual validation outcome.

If the hardening tasks in section 6 require code changes beyond the
currently-checked implementation items in
[../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md), reconcile the ED-M02
checklist before recording the validation row. Add explicit bullets such as
"runtime invalid-state writes produce visible diagnostics" instead of treating
new hardening work as already complete.
