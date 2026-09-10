# Runtime Integration LLD

Status: `ED-M07 review-ready`

## 1. Purpose

Define the managed boundary between the WinUI editor and the embedded Oxygen
Engine runtime. This LLD was first reviewed for ED-M02 and is re-reviewed in
ED-M04 only for inspector-driven live-sync completion semantics. It covers
runtime lifecycle, native runtime
discovery assumptions, runtime settings, surface leases, engine view lifecycle,
cooked-root refresh ordering, threading, and the validation evidence needed
before live viewport behavior can support later authoring milestones.

This is not the LLD for scene synchronization, content cooking, standalone
runtime validation, or viewport authoring tools. Those later milestones consume
the runtime contracts defined here.

ED-M07 consumes this LLD only for validated cooked-root mount refresh after
content pipeline cook. Multi-viewport remains deferred and is not reopened by
ED-M07.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `REQ-025` | Embedded viewport renders the active scene through the live engine. |
| `REQ-027` | Runtime surface/view lifecycle supports the V0.1 single live viewport; multi-viewport is deferred. |
| `REQ-028` | Runtime presentation is routed to the correct editor surface for the supported live viewport. |
| `REQ-030` | Partial: runtime presentation provides the embedded preview path used later for parity validation; full authored-content parity remains ED-M08. |
| `SUCCESS-003` | Live editor viewport presents correctly. |
| `SUCCESS-005` | Runtime presentation is stable enough for later authoring validation. |

ED-M04 consumes this LLD for `REQ-008`, `REQ-022`, `REQ-024`, and `REQ-026`
only to define runtime readiness, rejected runtime setting writes, and sync
completion semantics. ED-M04 does not reopen surface/view lifecycle scope.

## 3. Architecture Links

- `ARCHITECTURE.md`: runtime boundary, threading/frame phases, dependency
  direction, and diagnostics policy.
- `DESIGN.md`: runtime integration LLD ownership and cross-LLD workflow chains.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.Runtime` owns the managed runtime service;
  `Oxygen.Editor.Interop` owns the C++/CLI bridge.
- `viewport-and-tools.md`: viewport UI consumes this LLD's runtime service and
  surface/view contracts.
- `diagnostics-operation-results.md`: runtime failure domains and operation
  result vocabulary.

## 4. Current Baseline

The current codebase has the core runtime pieces needed for ED-M02:

- `Oxygen.Editor.Runtime` exposes `IEngineService` and `EngineService`.
- `EngineServiceState` models `NoEngine`, `Initializing`, `Ready`, `Starting`,
  `Running`, `ShuttingDown`, and `Faulted`.
- Native runtime discovery is process bootstrap infrastructure, not a Project
  Browser responsibility. The editor now defers engine startup until workspace
  activation/runtime use.
- `WorkspaceViewModel` starts the engine before cooked-root refresh. The
  current cooked-root refresh can mount the project `.cooked` root when
  `.cooked/container.index.bin` exists, with legacy per-mount index fallback.
  ED-M02 documents the runtime ordering and user-visible warning behavior; the
  full cooked-index policy remains `content-pipeline.md` / ED-M07 scope.
- `IEngineSettings` and `EngineSettingsService` provide startup settings.
  `EngineSettingsExtensions` maps them to the interop engine configuration.
- `IEngineService.TargetFps`, `MaxTargetFps`, and `EngineLoggingVerbosity`
  expose runtime-adjustable settings once the engine is ready/running.
- `ViewportSurfaceRequest`, `ViewportSurfaceKey`, and `IViewportSurfaceLease`
  model logical viewport-to-surface ownership.
- `Viewport.xaml.cs` attaches a `SwapChainPanel` through
  `IEngineService.AttachViewportAsync`, creates a native editor view, resizes
  the surface, and destroys the view before disposing the lease.
- `EngineService` tracks active leases, per-document surface limits, and
  blacklisted/orphaned viewport IDs when native cleanup fails.

Known ED-M02 gaps:

- Runtime operations mostly log failures; they do not consistently publish
  operation results.
- Attach/create/resize completion means "accepted by the managed/native
  boundary", not "a frame has visibly presented"; validation must use visual
  evidence and logs.
- Engine startup is owned by workspace activation today. ED-M02 must make that
  ownership explicit and ensure workspace code does not call mount/surface
  operations before `Running`.
- Surface and view ownership is split between `Oxygen.Editor.Runtime` and
  WorldEditor viewport code. ED-M02 accepts this split but documents the
  invariants that must hold.

## 5. Target Design

ED-M02 target flow:

```mermaid
flowchart LR
    Workspace[Workspace Activation]
    Runtime[IEngineService]
    Engine[Embedded Engine]
    Cooked[Cooked Root Refresh]
    Viewport[Viewport Control]
    Surface[Surface Lease]
    View[Engine View]
    Present[Visible Presentation]

    Workspace --> Runtime
    Runtime --> Engine
    Workspace --> Cooked
    Cooked --> Runtime
    Viewport --> Surface
    Surface --> Runtime
    Viewport --> View
    View --> Runtime
    Runtime --> Present
```

Target invariants:

1. Project Browser does not start the engine.
2. Workspace activation is the first normal runtime startup trigger.
3. Cooked-root mount, surface attach, resize, view create, and view destroy
   require `EngineServiceState.Running`.
4. Runtime settings may be read/applied only in `Ready` or `Running` states.
5. Feature UI depends on `Oxygen.Editor.Runtime`, not on interop classes except
   for narrow existing view configuration structs until wrapper contracts exist.
6. A surface lease owns the native composition surface reservation for one
   `(document, viewport)` key.
7. An engine view is associated with exactly one viewport surface target while
   it is visible.
8. Disposing a viewport destroys its engine view before releasing the surface
   lease.
9. Native registration failure, cleanup failure, or orphaned viewport IDs must
   not poison future unrelated viewports.
10. Surface limits are enforced before native registration and report
    `RuntimeSurface` diagnostics when exceeded.
11. UI code must not synchronously block on native frame progress.

## 6. Threading And Frame Ordering

ED-M02 runtime calls cross the WinUI UI thread, managed async services, and the
native engine frame loop. The ordering contract is:

1. Workspace activation serializes `InitializeAsync` and `StartAsync`.
   Cooked-root refresh is awaited only after the service reaches `Running`.
2. `SwapChainPanel` access and the surface attach entry point originate on the
   UI thread.
3. Runtime services own engine state transitions and reject invalid-state calls
   with diagnostics instead of allowing hidden asserts to be the only signal.
4. Viewport unload cancels pending attach/create/resize work where possible and
   still runs view-destroy and lease-dispose teardown idempotently.
5. Resize requests may be coalesced, but the latest measured viewport size must
   eventually be sent to the runtime while the lease remains active.
6. ED-M02 does not expose a managed "presented frame observed" completion
   signal. Validation therefore uses visual checks and correlated logs.

## 7. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor` | Process bootstrap, DI composition, native runtime discovery setup. |
| `Oxygen.Editor.WorldEditor` workspace | Runtime startup trigger during workspace activation; cooked-root refresh request. |
| `Oxygen.Editor.WorldEditor` viewport UI | `SwapChainPanel` ownership, load/unload/size events, initial measured size, engine view request timing. |
| `Oxygen.Editor.Runtime` | Engine lifecycle, settings bridge, surface leases, view calls, input bridge access, runtime diagnostics mapping. |
| `Oxygen.Editor.Interop` | Managed/native bridge calls and native handle abstractions. |
| Oxygen Engine | Frame loop, rendering, content loading, composition, native resource ownership. |

ED-M02 accepts that viewport UI currently creates engine views directly after
surface attachment. Later runtime cleanup may wrap view lifecycle more tightly
inside `Oxygen.Editor.Runtime`, but the ED-M02 invariant is that all native
calls still pass through `IEngineService`.

View creation is initiated by viewport UI, but all native view operations are
invoked through `IEngineService.CreateViewAsync` / `DestroyViewAsync`; viewport
UI never calls `Oxygen.Editor.Interop` directly for view lifecycle.

## 8. Data Contracts

### Runtime State

`EngineServiceState` is the authoritative lifecycle state exposed to managed
editor code.

Runtime lifecycle transitions:

```mermaid
stateDiagram-v2
    [*] --> NoEngine
    NoEngine --> Initializing
    Initializing --> Ready
    Initializing --> ShuttingDown: partial initialization failure
    Ready --> Starting
    Starting --> Running
    Starting --> ShuttingDown: startup failure
    Running --> Faulted: loop exits unexpectedly
    Running --> ShuttingDown
    Ready --> ShuttingDown
    Faulted --> ShuttingDown: cleanup or reinitialization request
    ShuttingDown --> NoEngine: ownership released
    ShuttingDown --> Faulted: ownership remains
```

Rules:

- Lifecycle and asynchronous surface/view operations share a gate. Shutdown
  drains earlier operations; repeated shutdown/disposal waits for prior cleanup.
- Normal-operation guards remain distinct from internal teardown preconditions.
  `Ready`, partial initialization, and faulted execution all have cleanup paths.
- A failed surface release is recorded independently. Remaining releases and
  safe native destruction still run. Unreleased native surfaces remain tracked
  until successful release or destruction of their native owner.
- Native loop exit ends waits for surface/view acknowledgments that can no longer
  arrive. Stop and loop completion precede context destruction; the interop
  `WaitForLoopCleanupAsync` signal additionally covers posted UI cleanup.
- Native `StopEngine` must tolerate repeated calls and synchronize with removal
  of the engine owner. The stop request flag crosses threads and is atomic.
- `ShutdownAsync` reports accumulated failures after establishing final ownership
  state. An intermediate error can be reported even if eventual cleanup released
  everything. `NoEngine` is never reported while native ownership remains.
- `DisposeAsync` uses the same teardown as a non-throwing, logged fallback. Once
  disposal is requested, new runtime operations are rejected. Cleanup remains
  callable after failure; retained resources cannot be overwritten on restart.
- Initialization/startup exceptions retain their original cause; secondary
  cleanup failures are logged. Reinitialization first cleans any failed session.
- The application registers `EngineShutdownService` after the UI hosted service
  so ordinary host shutdown awaits runtime teardown before stopping the dispatcher.
  Hosted-service construction does not resolve UI-dependent services. The `App`
  constructor connects window tracking once the dispatcher exists, using the same
  shutdown singleton registered with the host.
  Final-window shutdown also runs in Aura's finalization phase, after every close
  guard and document commit succeeds and before native window destruction.
  Canceled document close does not shut down the runtime. Overlapping approved
  window closes count toward the final-window decision.
- The application caller catches shutdown failures and publishes
  `Runtime.Shutdown` operation results and logs. Existing exit behavior is
  retained; this fix does not introduce retry dialogs or forced termination.

Implementation and validation details: [issue #3 plan](../plan/issue-003-runtime-shutdown.md).

### Runtime Settings Snapshot

Settings inputs:

- startup `IEngineSettings` mapped into editor engine config during
  initialization.
- runtime `TargetFps`.
- runtime native logging verbosity.

Rules:

- Startup settings apply only when a new engine context is created.
- Runtime FPS/logging writes are immediate service calls and may fail if the
  engine is not `Ready`/`Running`.
- Failed settings writes must produce a visible diagnostic in ED-M02 validation.

### Surface Lease

`ViewportSurfaceRequest` contains:

- document ID.
- viewport ID.
- viewport index.
- primary viewport flag.
- optional diagnostic tag.

`ViewportSurfaceKey` is `(DocumentId, ViewportId)`.

`IViewportSurfaceLease` provides:

- `Key`.
- `IsAttached`.
- `AttachAsync`.
- `ResizeAsync`.
- `DisposeAsync`.

Rules:

- A lease is the only managed object allowed to resize or release its native
  surface.
- Re-attaching an already attached lease is a no-op.
- A failed attach removes the reservation when possible.
- A failed native unregister marks the viewport ID orphaned so it is not reused.
- Surface limits are enforced before native registration.

### Engine View

Current ED-M02 view contract:

- Viewport UI creates a view after a surface lease is attached.
- The view config includes name, purpose, compositing target viewport ID,
  initial pixel size, and clear color.
- The native engine returns an engine view ID.
- Viewport UI stores the assigned view ID and destroys it before lease disposal.

Rules:

- No view creation before a surface target exists.
- No view creation before the scene is loaded into the engine.
- A failed view creation must leave the surface lease disposable.
- Destroy failures are logged and surfaced through diagnostics where possible,
  but must not prevent control teardown.

### Cooked Root Refresh

ED-M02 mount contract:

- Workspace activation requests cooked-root refresh after runtime is running.
- The refresh uses the existing workspace/runtime mount path. The exact
  cooked-index layout is brownfield behavior until ED-M07 owns content-pipeline
  parity.
- Missing cooked roots are non-fatal for workspace entry but must be visible in
  logs/diagnostics because assets may not resolve.

ED-M07 mount contract:

- ContentPipeline requests runtime refresh only after cooked output validation
  succeeds.
- Brownfield scene save, material save/cook, Content Browser import/cook, and
  catalog-only refresh paths must stop publishing unvalidated cooked-root
  refresh messages; only a validated content-pipeline result may trigger
  runtime mount refresh in ED-M07.
- Workspace/runtime code still owns the actual `IEngineService` calls:
  the runtime mount calls. ED-M07B adds pause/drain, transaction coordination
  and rollback around these capabilities per content-pipeline section 16.
- The path passed to runtime is the validated cooked root for the active
  project/mount, not an authored asset path or a browser display path.
- Validated cook-result refresh mounts only the roots carried by that validated
  result. It must not rescan all `.cooked` child directories and accidentally
  mount stale or unrelated cooked output.
- `EditorModule` applies root changes at frame start through its existing
  `AddLooseCookedRoot` / `ClearCookedRoots` path; ED-M07 must not manipulate
  native asset-loader mounts mid-frame.
- Mount refresh failure is reported under `AssetMount`; staged cook and
  publication outcomes are distinct. ED-M07B restores prior roots or leaves
  preview explicitly unavailable with rollback output retained.

## 9. Commands, Services, Or Adapters

ED-M02 service operations:

| Operation | Owner | Completion Meaning |
| --- | --- | --- |
| Runtime initialize | `IEngineService.InitializeAsync` | Engine context created and service is `Ready`. |
| Runtime start | `IEngineService.StartAsync` | Frame loop startup call returned and service is `Running`. |
| Runtime shutdown | `IEngineService.ShutdownAsync` | Engine resources released or service faulted. |
| Apply startup settings | `EngineSettingsExtensions` | Settings copied into config before context creation. |
| Apply FPS/logging | `IEngineService` properties | Native service accepted the value. |
| Attach surface | `AttachViewportAsync` | Native surface registration completed and lease is attached. |
| Resize surface | `IViewportSurfaceLease.ResizeAsync` | Native resize request accepted/queued. |
| Create/destroy view | `CreateViewAsync` / `DestroyViewAsync` | Native view operation returned success or failure. |
| Refresh cooked roots | workspace through `IEngineService` | Existing cooked roots are mounted or a non-fatal warning is produced. |

Frame-presented completion is not exposed as a managed contract in ED-M02. The
detailed ED-M02 validation plan must therefore use visual validation and engine
logs to prove presentation.

## 10. UI Surfaces

ED-M02 runtime UI surfaces:

- Project Browser: no runtime UI; startup failures must not block Project
  Browser visibility.
- Workspace shell: owns the "runtime is starting / failed / cooked roots
  missing" user-visible state.
- Viewport control: owns surface attach/resize failure presentation near the
  viewport when possible.
- Scene editor toolbar/settings surface: invokes runtime FPS/logging changes,
  publishes `Runtime.Settings.Apply` results, and shows rejected writes inline
  near the control when practical.
- Output/log panel: shows runtime diagnostic details and correlated operation
  result summaries.

Settings failures always appear in the output/log panel. Inline presentation is
required where the scene editor settings surface is visible.

## 11. Persistence And Round Trip

Persisted:

- runtime/editor settings through settings services.
- scene document viewport layout metadata through document metadata.
- workspace layout through workspace/persistent state services.

Not persisted:

- engine service state.
- active surface leases.
- native engine view IDs.
- mounted cooked roots.

On restart, workspace/project activation recreates runtime state from project
context, settings, document metadata, and cooked roots.

## 12. Live Sync / Cook / Runtime Behavior

ED-M02 covers runtime readiness and presentation only.

Later consumers:

- `live-engine-sync.md` consumes `Running` state and frame-phase ordering for
  scene mutation sync.
- `content-pipeline.md` owns full cooked-root mount behavior after cook.
- `standalone-runtime-validation.md` consumes runtime/cooked parity evidence.
- `viewport-and-tools.md` consumes surface/view contracts for layout and
  camera validation.

ED-M02 must not add authoring-specific scene mutation semantics here.

### Scene Asset Completion Lifetime

The interop editor module owns one `SceneAssetRequests` session alongside its
native scene. Retire that session before scene teardown or replacement and at
module destruction. Pending loader callbacks retain only a weak completion
inbox, so they cannot keep the old scene/module alive or apply results after
shutdown. No engine rebuild or new public runtime API is needed for this
coordination; it uses existing loader callbacks and `SceneMutation` ordering.

Within `SceneMutation`, queued authoring commands execute before the completion
inbox is drained. Generation validation and application occur on that same
mutation path. Full node handles protect node identity, and the separate scene
session lifetime prevents reused scene identifiers from accepting old results.
The generation and material-slot semantics belong to
[live-engine-sync.md](./live-engine-sync.md#82-material-slot-v01-behavior).
Current asset failures retain the native runtime logging path and do not change
the managed queue-acceptance contract or roll back authored edits.

### ED-M04 Inspector-Driven Sync Semantics

ED-M04 adds **no new public surface** to `IEngineService`. It pins the exact
preconditions that the live-sync adapter
([live-engine-sync.md](./live-engine-sync.md)) consumes before invoking
`OxygenWorld` operations.

#### 12.1 Runtime readiness contract

Before any `ISceneEngineSync.<Update*|Attach*|Detach*>` call hits the engine,
the adapter MUST observe **all** of:

| Precondition | Source | Failure classification |
| --- | --- | --- |
| `IEngineService.State == Running` | `EngineService.State` | `SyncOutcome.SkippedNotRunning`, code `OXE.LIVESYNC.NotRunning`. |
| Managed world capability is available for the matching run/scene lifetime | `IEngineService.WorldCommands` target contract in section 18 | Unavailable/stale target result; no concrete facade access from the feature. |
| `IEngineService.State != Faulted` | `EngineService.State` | `SyncOutcome.SkippedNotRunning`, code `OXE.LIVESYNC.RuntimeFaulted`. |
| Scope cancellation token not cancelled | command-supplied `CancellationToken` | `SyncOutcome.Failed`, code `OXE.LIVESYNC.Cancelled`. |

The adapter performs these checks **without** taking any runtime lock other
than reading managed state/capability availability. The Runtime adapter rechecks
run/target identity before native dispatch. The feature never blocks
waiting for `Running`. Calls to `SyncSceneWhenReadyAsync` (full-scene resync)
remain the only awaiting variant and are reserved for workspace/document
activation, not inspector edits.

#### 12.2 Inspector-side rules

1. Inspector view-models and command services MUST NOT call
   `IEngineService.StartAsync` / `StopAsync` / `RestartAsync`. Engine lifecycle
   stays owned by workspace activation / scene editor controls.
2. Inspector commands do not own lifecycle subscriptions. The scene sync
   orchestrator owns visibly pending work and revision-aware reconnect under
   property-pipeline section 11. Full current-scene sync supersedes older queued
   values; stale document lifetimes never replay into a later scene.
3. Inspector commands MUST treat any thrown exception from
   `ISceneEngineSync` as `SyncOutcome.Failed` and continue. They MUST NOT
   re-throw to the UI.

#### 12.3 What "Accepted" means

`SyncOutcome.Accepted` means the engine boundary accepted the call. It does
NOT imply:

- a frame has been presented,
- the cooked geometry/material was resolved,
- the change is visible in the active view.

ED-M04 does not introduce a managed presented-frame completion contract.
Visual confirmation remains a manual validation step.

#### 12.4 Runtime setting writes

Inspector-adjacent runtime setting controls (target FPS, engine logging
verbosity) continue to use `Runtime.Settings.Apply` and the `Settings`
failure domain — unchanged from ED-M02. These do not flow through the
sync adapter and do not produce `OXE.LIVESYNC.*` codes.

## 13. Operation Results And Diagnostics

ED-M02 operation kinds:

- `Runtime.Start`.
- `Runtime.Settings.Apply`.
- `Runtime.Surface.Attach`.
- `Runtime.Surface.Resize`.
- `Runtime.View.Create`.
- `Runtime.View.Destroy`.
- `Runtime.View.SetCameraPreset`.
- `Runtime.CookedRoot.Refresh`.

Failure domains:

- `RuntimeDiscovery` for native DLL/path discovery.
- `RuntimeSurface` for surface attach/resize/release.
- `RuntimeView` for engine view create/destroy/preset failures.
- `AssetMount` for missing or failed cooked-root refresh when it affects
  runtime content availability.
- `Settings` for global runtime settings failures.

Minimum ED-M02 rule:

- failures that block visible viewport presentation must produce a visible
  user-facing diagnostic, not only a debug trace.
- non-fatal missing cooked roots may be warning diagnostics.
- invalid-state runtime settings writes produce `Runtime.Settings.Apply` /
  `Settings` diagnostics, with output/log panel presentation and inline
  presentation near the setting when possible.
- surface-limit rejection produces a `RuntimeSurface` diagnostic with reason
  `LimitExceeded`.
- ED-M02 only specifies that `Runtime.CookedRoot.Refresh` runs after `Running`,
  that missing/unmountable roots produce `AssetMount` warnings, and that the
  workspace remains usable. Cooked-index layout, validation, and refresh after
  cook are owned by `content-pipeline.md` in ED-M07.
- teardown failures may be log-only when the UI surface is already being
  destroyed, but must be visible in output/log diagnostics.

## 14. Dependency Rules

Allowed:

- `Oxygen.Editor.WorldEditor` depends on `Oxygen.Editor.Runtime`.
- `Oxygen.Editor.Runtime` depends on `Oxygen.Editor.Interop`.
- `Oxygen.Editor.Runtime` depends on DroidNet hosting/settings abstractions.
- The surface attach entry point may accept a `SwapChainPanel` passed by the
  WinUI viewport host; runtime contracts otherwise do not depend on feature UI
  modules.

Forbidden:

- Project Browser must not depend on `IEngineService`.
- Feature UI must not call `Oxygen.Editor.Interop` directly for runtime engine
  operations.
- Runtime services must not depend on WorldEditor UI types.
- Runtime services must not own project cook policy.
- Engine view IDs and native handles must not be persisted.
- UI code must not infer success by parsing engine log text.

## 15. Validation Gates

ED-M02 can be validated when:

- normal launch still starts at Project Browser without initializing or starting
  the engine.
- opening a valid project starts the runtime before workspace cooked-root mount.
- runtime DLL discovery loads native engine DLLs from the engine install runtime
  directory.
- one-pane layout presents the live viewport.
- multi-viewport validation is recorded as deferred, not as an ED-M02 gate.
- resizing panes/windows does not leave stale or blank surfaces.
- closing/reopening a scene releases old document surfaces and creates new
  surfaces without surface-limit leakage.
- runtime FPS/logging settings apply, or the UI shows a diagnostic explaining
  why they could not apply.
- an invalid-state or simulated rejected runtime setting write produces a
  visible `Settings` diagnostic.
- surface/view failure paths produce operation-result or output/log diagnostics
  with the affected document/viewport where known.

Tests are useful for state-machine and lease bookkeeping. Final ED-M02 closure
also requires manual visual validation because frame-presented completion is not
yet a managed contract.

## 16. Closed V0.1 Decisions

Existing operation completion continues to mean accepted/queued where stated.
ED-M08 adds explicit observed-state/capture completion for qualification; callers
cannot reinterpret accepted as presented. Runtime status uses the existing
workspace/viewport pending/failure surface plus operation/output details. No
new diagnostics dashboard is required.

## 17. V0.1 Qualification And Publication Boundary

ED-M07B implements matched-build preflight before interop/native work, using the
PRD's artifact/schema fingerprint. Missing/mismatched native artifacts disable
native operations visibly while Project Browser and safe authoring/save remain
available. The public managed boundary must be loadable without initializing
interop merely to open the Project Browser.

Publication briefly pauses preview and drains affected content reads before
fixed cooked-root replacement. The runtime exposes the required pause/drain/
remount/resume capabilities; ContentPipeline owns journal, paths and policy.
Standalone validation holds an output read lease. ED-M08 captures native observed
state and rendered frames through stable capabilities; existing accepted/queued
responses remain insufficient for presented-frame proof. The full publication
transaction and recovery behavior are in content-pipeline section 16.

Only one scene is live. Activation establishes document lifetime and current
revision before scene sync; late operations from an earlier activation cannot
change it. Pending edits remain visible until their actual current-state sync
succeeds. These guarantees are qualified in ED-M07A.4 and ED-M07B.2/4, with
standalone rendered evidence in ED-M08.

## 18. Managed World/Input Capabilities And Loop Supervision

### Capability Boundary (#10, ED-M07A.0)

Runtime owns injectable, managed-only `IRuntimeWorldCommands` and
`IRuntimeInputCommands`. IEngineService exposes WorldCommands/InputCommands,
not concrete OxygenWorld/OxygenInput. Port all feature consumers and remove the
old concrete properties in the same change; no forwarding compatibility facade
remains public. Existing direct facade use is migration debt under the prior
contract, not evidence that every old caller violated its accepted design.

World commands cover scene projection, node create/remove/reparent, scalar
property edits, geometry/material identity, scene environment and observed-state
requests. Feature/domain owners build immutable projection requests; Runtime
does not own a second authored scene model. Managed target records carry run ID,
scene/document lifetime, authored node ID, and view generation where relevant.
Input records carry managed key/button/modifier values, physical viewport-pixel
positions/deltas and that view target. WinUI event interpretation stays in the
UI bridge; native enum/struct/facade conversion is internal to Runtime adapters.

Capability responses identify operation/run/target and Accepted, Rejected,
Unavailable, Cancelled or Failed. Accepted remains boundary acceptance; observed
state and captures have their separate ED-M08 completion contract. An unavailable
capability is representable without loading the mixed-mode facade. The adapter
checks run/target lifetime again at dispatch and preserves #5 native request
acceptance, generation invalidation, and mutation-phase application.

Managed substitutes must simulate each outcome without constructing interop
world/input objects. Adapter tests cover key/button/modifier translation,
coordinate units, numeric payloads and identities. Dependency/call-site checks
reject feature facade access and native input DTO construction. Existing narrow
view configuration value types are not permission to expose world/input behavior.

### Active Run Observer (#6, ED-M07A.7)

The rebased #3 implementation already makes State read Faulted for a completed
loop task and ends surface waits when that task completes. Retain those safeguards.
Add one observer for each started run with a unique run ID. Under the lifecycle
gate it distinguishes requested shutdown from unexpected exit/fault, updates
state, publishes a managed state-change event with run ID and original outcome,
and emits a Runtime execution diagnostic. UI subscribers marshal to the dispatcher.
An old observer cannot modify a new run or dispose its ownership.

Resolve outstanding operation waits against run termination; no synchronous UI
wait or indefinite frame acknowledgment is permitted. Normal stop is not a fault.
Unexpected exit without an exception is still a diagnosed runtime failure.
State notification and diagnostics must occur without waiting for another State
getter or Shutdown call. Direct EngineService tests drive controlled run tasks
through fault, unexpected exit, stop, stop/exit race and restart. This adds active
supervision to #3; it does not repeat or weaken its cleanup contract.

Current native asset-load failures use #5 request/session/target correlation.
ED-M07A.0 forwards current failures through the managed runtime diagnostic/event
boundary to existing feature result surfaces; superseded failures remain discarded.
This is additional feature-visible reporting over the landed native mechanism,
not a new loader-generation implementation or a change to Accepted semantics.

## 19. Bounded Validation Capture Session

ED-M08 temporarily pins the saved scene projection/camera/profile under a
run/document/view-lifetime capture lease. New authoring revisions continue, but
matching scene mutation requests remain visibly pending until embedded observation
and capture finish. Navigation is disabled for the pinned viewport during that
window. This explicit validation mode does not claim the preview shows newer edits.

The standalone-validation LLD defines acquisition, timeout and release. Every
success/cancel/failure path removes temporary profile state and converges to the
current valid document snapshot; activation/close/run replacement invalidates old
callbacks and forbids restoring stale scene/view state. The capture lease can end
before standalone loading completes; the separate output read lease protects
published files until that process exits. Test edits during warm-up, late callbacks,
fault/cancel and scene changes. Normal one-live-scene current-revision behavior
resumes after the capture session, not merely after restoring camera settings.
