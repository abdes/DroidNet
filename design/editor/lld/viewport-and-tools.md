# Viewport And Tools LLD

Status: `review`

## 1. Purpose

Define the ED-M02 viewport design: live embedded viewport presentation,
one/two/four pane layout stability, editor camera defaults/framing, runtime
settings surface, and the boundary between current stabilization work and later
authoring tools.

Selection highlights, transform gizmos, node icons, picking, and advanced
overlays are planned for ED-M09. They are intentionally not ED-M02 blockers.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `REQ-025` | A live embedded viewport renders the active scene. |
| `REQ-027` | Scene editor can create runtime views after scene load. |
| `REQ-028` | One/two/four viewport layouts are stable. |
| `REQ-029` | ED-M02 covers sane initial framing; explicit frame selected/all commands are ED-M09. |
| `REQ-030` | Viewport layout changes preserve usable presentation. |
| `REQ-035` | FPS/runtime setting controls are visible and honest. |
| `SUCCESS-003` | Users can see the scene in the editor viewport. |
| `SUCCESS-005` | Live viewport is stable enough for later authoring work. |

## 3. Architecture Links

- `runtime-integration.md`: runtime lifecycle, surface leases, engine views,
  settings, and diagnostics.
- `documents-and-commands.md`: later owner of command/selection integration.
- `scene-explorer.md`: later owner of hierarchy selection that viewport tools
  will consume.
- `diagnostics-operation-results.md`: visible viewport/runtime failures.
- `PLAN.md` ED-M02 and ED-M09 split.

## 4. Current Baseline

The current editor has:

- a WorldEditor workspace route with a center renderer outlet.
- `DocumentHostViewModel` and `SceneEditorViewModel` hosting scene documents.
- `SceneEditorViewModel` creates `ViewportViewModel` instances according to
  `SceneViewLayout`.
- layout menus for one, two, three, and four pane variants.
- `Viewport.xaml.cs` attaches `SwapChainPanel` instances to runtime surface
  leases.
- each viewport can create a native editor view after surface attach.
- per-viewport clear colors are used to diagnose surface/view routing.
- camera preset menu entries call runtime view camera preset APIs.
- FPS and logging verbosity controls read/write through `IEngineService`.
- initial layout creation is deferred until `SceneLoadedMessage` to avoid
  creating views before the scene exists in the engine.

Known ED-M02 gaps:

- several later tool concepts are present in UI names/menus but are not
  production-ready authoring tools.
- frame-all/frame-selected commands are not yet explicit user workflows.
- runtime/viewport failures are logged more reliably than they are presented.
- multi-view validation currently depends on manual visual inspection and logs.

## 5. Target Design

ED-M02 target viewport flow:

```mermaid
flowchart LR
    SceneLoaded[Scene Loaded]
    Layout[Scene View Layout]
    VM[Viewport VM]
    View[Viewport Control]
    Surface[Runtime Surface Lease]
    EngineView[Native Editor View]
    Presented[Visible Presented View]

    SceneLoaded --> Layout
    Layout --> VM
    VM --> View
    View --> Surface
    Surface --> EngineView
    EngineView --> Presented
```

Target invariants:

1. Viewports are created only after the scene is available to the runtime.
2. Each visible viewport has one viewport ID, one surface lease, and at most
   one assigned engine view.
3. Hidden/removed viewports release their engine view and surface lease.
4. Layout changes update viewport metadata before surface/view requests rely on
   index/primary flags.
5. One/two/four pane layouts must not route multiple visible viewports to the
   same surface.
6. The editor camera is owned by the runtime/editor view, not by scene camera
   authoring data.
7. ED-M02 framing means the default camera observes authored content after
   load; explicit frame commands are ED-M09.
8. Runtime settings controls reflect service state and fail visibly.
9. Viewport UI remains responsive while attach/resize/create work is async.

## 6. Ownership

| State Or Behavior | Owner |
| --- | --- |
| Scene document layout metadata | WorldEditor scene document metadata |
| Viewport collection and layout selection | `SceneEditorViewModel` |
| Viewport identity, index, primary flag, clear color | `ViewportViewModel` |
| `SwapChainPanel` lifetime and size events | `Viewport` WinUI control |
| Surface lease | `Oxygen.Editor.Runtime` |
| Engine view ID | Runtime result stored by `ViewportViewModel` |
| Runtime camera preset calls | `ViewportViewModel` through `IEngineService` |
| Runtime FPS/logging settings | Scene editor UI through `IEngineService` |
| Selection/picking/gizmos | ED-M09 LLD scope, not ED-M02 |

## 7. Data Contracts

### Viewport Layout

`SceneViewLayout` describes the requested pane arrangement. ED-M02 validates:

- one pane.
- representative two-pane layout.
- four quadrants.

Other layout variants may exist but do not expand the ED-M02 validation matrix
unless they are touched by the implementation.

### Viewport Identity

Each viewport has:

- owning document ID.
- stable viewport ID for the current viewport instance.
- zero-based layout index.
- primary viewport flag.
- assigned native view ID, invalid when no view exists.
- diagnostic clear color.

Viewport IDs are runtime/session state and are not persisted as authoring data.

### Camera State

ED-M02 camera state is runtime/editor-view state:

- perspective/default view.
- orthographic preset requests where already exposed.
- initial camera framing generated by the runtime/editor view.

Scene camera components and view-through-camera authoring are not ED-M02 scope.

### Runtime Settings Surface

ED-M02 viewport-adjacent settings:

- target FPS.
- native logging verbosity.
- simple view/preset menu state where already present.

Controls must not pretend a setting applied if `IEngineService` rejects it.

## 8. Commands, Services, Or Adapters

ED-M02 commands and service calls:

| User/UI Action | Owner | Runtime Interaction |
| --- | --- | --- |
| Open scene document | document/workspace | scene sync happens before layout creation. |
| Restore/change layout | `SceneEditorViewModel` | create/remove viewport VMs. |
| Viewport loaded | `Viewport` control | attach surface lease, create engine view, resize. |
| Viewport unloaded/removed | `Viewport` control | destroy engine view, dispose lease. |
| Pane/window resized | `Viewport` control | debounce and resize surface lease. |
| Camera preset selected | `ViewportViewModel` | call `SetViewCameraPresetAsync`. |
| FPS changed | scene editor UI | write `IEngineService.TargetFps`. |
| Logging verbosity changed | scene editor UI | write `IEngineService.EngineLoggingVerbosity`. |

Command-based scene mutation, undoable transform tools, and selection tools are
deferred to ED-M03/ED-M09.

## 9. UI Surfaces

ED-M02 UI surfaces:

- viewport content area.
- viewport toolbar/menus for layout and view presets.
- scene editor FPS/logging controls.
- output/log panel diagnostics.

Failure presentation:

- fatal runtime startup/surface/view failure should be visible near the
  workspace or viewport.
- non-fatal resize/create warnings may be visible in the output/log panel.
- missing cooked roots should be visible as a warning because geometry/material
  assets may not resolve.

## 10. Persistence And Round Trip

Persisted:

- scene document layout metadata.
- workspace layout.
- runtime/editor settings through their settings service.

Not persisted:

- viewport IDs.
- assigned engine view IDs.
- active surface leases.
- per-session diagnostic clear colors unless a later UI design chooses to make
  them a setting.

Restart behavior:

- opening the project/workspace recreates the runtime.
- opening/restoring a scene recreates viewport VMs.
- each loaded viewport attaches a new surface and creates a new engine view.

## 11. Live Sync / Cook / Runtime Behavior

ED-M02 depends on runtime presentation. It does not own live scene mutation
sync, cooking, or asset import.

Required ordering:

1. Project context is active.
2. Workspace starts runtime.
3. Workspace refreshes cooked roots.
4. Scene is loaded/synchronized into runtime.
5. Scene editor creates/restores layout.
6. Viewport controls attach surfaces and create views.
7. Viewports resize to measured panel sizes.

If a later step fails, earlier successful state remains valid where possible.
For example, missing cooked roots do not prevent the workspace from opening,
but may prevent asset-backed geometry/materials from rendering.

## 12. Operation Results And Diagnostics

ED-M02 viewport operation kinds:

- `Runtime.Surface.Attach`.
- `Runtime.Surface.Resize`.
- `Runtime.View.Create`.
- `Runtime.View.Destroy`.
- `Runtime.Settings.Apply`.
- `Viewport.Layout.Change`.

Failure domains:

- `RuntimeSurface`.
- `RuntimeView`.
- `Settings`.
- `WorkspaceRestoration` for restored layout issues.

Diagnostics must include document ID and viewport ID when available. Runtime
view ID should appear only in technical details/logs because it is not stable
authoring identity.

## 13. Dependency Rules

Allowed:

- WorldEditor viewport UI depends on `Oxygen.Editor.Runtime`.
- Viewport UI may use WinUI controls and `SwapChainPanel`.
- Viewport VM may store runtime-assigned view ID as session state.

Forbidden:

- Viewport UI must not call C++/CLI interop directly.
- Viewport IDs or view IDs must not be serialized into scene authoring data.
- Scene camera components must not be mutated to implement editor camera
  navigation.
- ED-M02 must not introduce selection/gizmo-specific dependencies.
- Layout changes must not depend on log parsing to decide whether presentation
  succeeded.

## 14. Validation Gates

ED-M02 viewport validation is complete when:

- one-pane scene viewport renders the active scene.
- representative two-pane layout renders each visible viewport to the correct
  surface.
- four-quadrant layout renders each visible viewport to the correct surface.
- switching between one/two/four layouts does not crash or leave blank/stale
  panels.
- resizing the workspace or split panes keeps viewports correctly scaled and
  non-overlapping.
- closing/reopening a scene releases old surfaces/views and creates new ones.
- default editor camera observes authored content without clipping the whole
  scene.
- camera preset menu calls do not fault the runtime.
- FPS/logging controls apply or produce visible diagnostics.

Evidence may include manual visual validation, logs showing distinct
document/viewport IDs, and targeted tests for pure layout/metadata helpers.

## 15. Open Issues

- Exact future UX for frame selected/all is deferred to ED-M09.
- Whether selection outline and node icons are pure overlay UI or partly engine
  debug rendering is deferred to ED-M09.
- Whether view lifecycle should move from viewport code-behind into a dedicated
  runtime view adapter is deferred until ED-M02 review decides it is necessary.
