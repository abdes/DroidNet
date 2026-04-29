# Live Engine Sync LLD

Status: `ED-M04 implementation-ready`

## 1. Purpose

Concrete design for the ED-M04 live-sync adapter that projects committed
authoring edits onto the embedded Oxygen Engine. This LLD owns the
`SyncResult` shape, per-operation adapter mapping, runtime-readiness handling,
unsupported-operation behavior, and `OperationResult` propagation. Engine
lifecycle, surface/view leases, and cooked-root mounting stay in
[runtime-integration.md](./runtime-integration.md).

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-003`, `REQ-008`, `REQ-026` | Embedded preview reflects supported edits. |
| `GOAL-006`, `REQ-022`, `REQ-024` | Sync failures produce visible `OperationResult`. |
| `SUCCESS-003` | Live editor preview shows authored content. |

## 3. Architecture Links

- [property-inspector.md](./property-inspector.md): edit producers and result
  rendering.
- [environment-authoring.md](./environment-authoring.md): environment adapter.
- [runtime-integration.md](./runtime-integration.md): engine readiness states.
- [diagnostics-operation-results.md](./diagnostics-operation-results.md):
  `FailureDomain` and code prefixes.

## 4. Current Baseline

- `Oxygen.Editor.WorldEditor/src/Services/ISceneEngineSync.cs` and
  `SceneEngineSync.cs` provide:
  - `Task<bool> SyncSceneAsync(Scene, ct)` and
    `Task<bool> SyncSceneWhenReadyAsync(Scene, ct)` (full resync; destroys/
    recreates the native scene through `OxygenWorld.DestroyScene()` /
    `CreateSceneAsync(name)`).
  - `Task CreateNodeAsync(node, parentGuid?)`,
    `Task RemoveNodeAsync(nodeId)`, `RemoveNodeHierarchyAsync`,
    `RemoveNodeHierarchiesAsync`,
    `ReparentNodeAsync(nodeId, newParent, preserveWorld)`,
    `ReparentHierarchiesAsync`, `UpdateNodeTransformAsync(node)`.
  - Internal `ApplyRenderableComponents` covers Geometry, Light, Camera
    attaches.
  - Several rendering/material override slot operations currently throw
    `NotImplementedException`.
- `IEngineService.State : EngineServiceState` exposes the `NoEngine` →
  `Initializing` → `Ready` → `Starting` → `Running` → `ShuttingDown` /
  `Faulted` lifecycle. `OxygenWorld` is valid only in `Running`.
- A class-level `SemaphoreSlim sceneSyncGate` already serializes scene-wide
  syncs; per-op coalescing inside an `EditSessionToken` is not implemented yet.
- Sync failures today are mostly logged. ED-M03 introduced `SceneCommandResult`
  with `OperationResultId` but the population from sync paths is incomplete.

Brownfield gaps:

1. No typed `SyncResult` enum / record; methods return `bool` or `Task`.
2. Material slot, environment, and several override slots throw
   `NotImplementedException` — must become `Unsupported` results.
3. Per-edit-session coalescing (one drag → one sync call) is not implemented.
4. `OperationResult` propagation from sync into `SceneCommandResult` is
   inconsistent.

## 5. Target Design

```text
ISceneDocumentCommandService.Edit*Async
   |
   |-- validate, mutate, dirty/undo (one HistoryKeeper entry per session)
   |
   v
ISceneEngineSync.<op>     -> SyncOutcome
   |
   |-- IEngineService.State == Running ?
   |       no  -> SyncOutcome.SkippedNotRunning(state, reason)
   |       yes -> try: engine API call
   |               -> SyncOutcome.Accepted
   |              catch unsupported (no API, NotImpl) -> Unsupported(field)
   |              catch invalid arg  -> Rejected(reason)
   |              catch other        -> Failed(exception)
   v
SceneCommandResult { Succeeded=true, OperationResultId=<published if not Accepted> }
```

Invariants:

1. Sync is invoked only after authoring state is committed.
2. Sync **never** rolls back authoring state. Authoring lives independently of
   sync.
3. `Unsupported` and `SkippedNotRunning` do not throw; they produce a warning
   `OperationResult`. Authoring command's `Succeeded` remains `true`.
4. `Rejected` and `Failed` produce `SucceededWithWarnings` / `PartiallySucceeded`
   on the command result; the command itself is still `Succeeded` because
   authoring succeeded.
5. The command's `await` returns when sync has reached an accepted/skipped/
   rejected/failed/unsupported terminal — not when a frame has presented.
6. `SceneEngineSync.sceneSyncGate` continues to serialize scene-wide syncs.
   Incremental ops do not take the gate; they enter a per-`(SceneId, NodeId)`
   coalescer (see §8.4).

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.WorldEditor.Documents.Commands` | Decide when to call sync; map results into `OperationResult`; carry `EditSessionToken`. |
| `Oxygen.Editor.WorldEditor.Services.SceneEngineSync` | Translate authoring delta to `OxygenWorld` calls; coalesce per session; classify outcomes. |
| `Oxygen.Editor.Runtime.IEngineService` | Expose `State` and `World`. |
| `Oxygen.Editor.Interop.OxygenWorld` | Native engine surface. |

ED-M04 keeps `SceneEngineSync` as the single adapter. Per-component sub-
adapters are an optional refactor and not a closure gate.

## 7. Data Contracts

### 7.1 `SyncOutcome`

```csharp
public enum SyncStatus
{
    Accepted = 0,
    SkippedNotRunning,
    Unsupported,
    Rejected,
    Failed,
    Cancelled,
}

public sealed record SyncOutcome(
    SyncStatus Status,
    string OperationKind,
    AffectedScope Scope,
    string? Code = null,        // OXE.LIVESYNC.* when not Accepted
    string? Message = null,
    Exception? Exception = null);
```

`Accepted` does **not** imply a presented frame. ED-M04 does not add a
presented-frame contract; visual proof is manual.

### 7.2 `EnvironmentSyncResult`

Aggregate for the environment adapter (per [environment-authoring.md](./environment-authoring.md)):

```csharp
public sealed record EnvironmentSyncResult(
    SyncStatus Overall,                                // worst across fields
    IReadOnlyDictionary<string, SyncStatus> PerField); // field key -> status
```

### 7.3 New `ISceneEngineSync` methods

Add to the existing interface; existing `Task<bool>` methods stay for
back-compat but are wrapped to return `SyncOutcome` internally.

```csharp
Task<SyncOutcome> UpdateNodeTransformAsync(Scene scene, SceneNode node, CancellationToken ct = default);

Task<SyncOutcome> AttachGeometryAsync(Scene scene, SceneNode node, CancellationToken ct = default);
Task<SyncOutcome> DetachGeometryAsync(Scene scene, Guid nodeId, CancellationToken ct = default);

Task<SyncOutcome> AttachCameraAsync(Scene scene, SceneNode node, CancellationToken ct = default);
Task<SyncOutcome> DetachCameraAsync(Scene scene, Guid nodeId, CancellationToken ct = default);

Task<SyncOutcome> AttachLightAsync(Scene scene, SceneNode node, CancellationToken ct = default);
Task<SyncOutcome> DetachLightAsync(Scene scene, Guid nodeId, CancellationToken ct = default);

Task<SyncOutcome> UpdateMaterialSlotAsync(Scene scene, SceneNode node, int slotIndex, Uri? materialUri, CancellationToken ct = default);

Task<EnvironmentSyncResult> UpdateEnvironmentAsync(Scene scene, SceneEnvironmentData environment, CancellationToken ct = default);
```

## 8. Adapter Mapping (ED-M04)

### 8.1 Per-operation table

| Producer (operation kind) | `ISceneEngineSync` method | Engine API call (today) | If runtime not `Running` | If engine API absent |
| --- | --- | --- | --- | --- |
| `Scene.Component.EditTransform` | `UpdateNodeTransformAsync` | `world.UpdateNodeTransform(...)` | `SkippedNotRunning` | n/a (always present) |
| `Scene.Component.EditGeometry` (URI changed or added) | `AttachGeometryAsync` | `world.AttachGeometry(...)` | `SkippedNotRunning` | n/a |
| `Scene.Component.EditGeometry` (cleared / component removed) | `DetachGeometryAsync` | `world.DetachGeometry(nodeId)` | `SkippedNotRunning` | n/a |
| `Scene.Component.EditMaterialSlot` | `UpdateMaterialSlotAsync` | `world.SetMaterialOverride(nodeId, slotIndex, materialPath)` / clear override | `SkippedNotRunning` | n/a |
| `Scene.Component.EditCamera` | `AttachCameraAsync` (re-apply) | `world.AttachCamera(...)` | `SkippedNotRunning` | n/a |
| `Scene.Component.EditLight` | `AttachLightAsync` (re-apply) | `world.AttachLight(...)` | `SkippedNotRunning` | n/a |
| `Scene.Component.Add` (Geometry/Camera/DirectionalLight) | matching `Attach*Async` | as above | `SkippedNotRunning` | n/a |
| `Scene.Component.Remove` | matching `Detach*Async` or implicit via geometry/light/camera detach | as above | `SkippedNotRunning` | n/a for V0.1 supported types; `Unsupported` for OrthographicCamera/Point/Spot if engine lacks specific API |
| `Scene.Environment.Edit` | `UpdateEnvironmentAsync` | queued native scene-system updates for `SkyAtmosphere` and full native-parity `PostProcessVolume`; post-process sync includes exposure enabled, mode, key, manual EV, compensation EV, auto-exposure range/speeds/metering/histogram/window/target/spot radius, tone mapper, bloom, saturation, contrast, vignette, and display gamma; sun binding via light sync | `SkippedNotRunning` (overall) | per-field `Unsupported`; overall = worst |

### 8.2 Material-slot V0.1 behavior

`UpdateMaterialSlotAsync` maps the authored material URI to the cooked engine
virtual path before calling the runtime override API:

- `asset:///<Mount>/<Path>.omat.json` -> `/<Mount>/<Path>.omat`.
- `asset:///<Mount>/<Path>.omat` -> `/<Mount>/<Path>.omat`.
- `null` or the empty material sentinel clears the slot override.

The sync outcome is `Accepted` once the engine command is queued. Runtime asset
resolution/load failure is logged by the native command because the material may
not be mounted yet; it must not roll back the authored scene slot.

### 8.3 Component remove → detach

When `Scene.Component.Remove` removes a Geometry/Light/Camera, the command
service issues a single `Detach*Async` call after the authoring removal
completes. Removing a `TransformComponent` is denied at the command layer
(see [property-inspector.md](./property-inspector.md) §8.4); no sync is
attempted.

### 8.4 Per-session coalescing

The command service holds the `EditSessionToken`. While a session is open:

- Sub-commits update the in-flight authoring value but **do not** call sync.
- A preview sync throttle (default 16 ms minimum interval) may issue
  `Update*Async` calls with the latest authoring value while the user is still
  dragging. These calls update the live viewport only; they do not create undo
  entries and do not publish separate top-level operation results.
- `session.Commit()` cancels any pending preview timer and issues exactly one
  terminal sync call with the final value. The result of that call populates
  `SceneCommandResult.OperationResultId`.
- `session.Cancel()` issues one sync call with the pre-Begin value to revert
  preview.

This keeps "drag 100 samples" → "one undo entry and one terminal sync result"
while still allowing live preview to update at a throttled cadence.

### 8.5 Runtime-state classification

| `IEngineService.State` | Behavior |
| --- | --- |
| `NoEngine`, `Initializing`, `Ready`, `Starting`, `ShuttingDown` | `SkippedNotRunning`, code `OXE.LIVESYNC.NotRunning` with state in diagnostic detail. |
| `Running` | proceed to engine call. |
| `Faulted` | `SkippedNotRunning`, code `OXE.LIVESYNC.RuntimeFaulted`. |

The adapter never starts the engine. That is a workspace-activation
responsibility per [runtime-integration.md](./runtime-integration.md).

### 8.6 Cancellation

Each `Update*Async` accepts a `CancellationToken`. The session's token chains
the document/scene cancellation. Cancellation results in `SyncOutcome` with
`Status = Cancelled`, `Code = OXE.LIVESYNC.Cancelled`. The command maps this
to operation status `Cancelled` and does **not** retry.

## 9. UI Surfaces

The adapter has no UI of its own. Consumers render:

- field-level errors → authoring (`SceneAuthoring`); not adapter output.
- section-level warnings → adapter output of `Unsupported` /
  `SkippedNotRunning`.
- pane/output log → `Rejected` / `Failed` details.

See [property-inspector.md](./property-inspector.md) §9 for the warning
placement rules.

## 10. Persistence

The adapter does not persist anything. Authoring persistence is the command
service's job. Native scene state, view IDs, and mounted roots are runtime
session state and must not be persisted.

## 11. Sync / Cook / Runtime Behavior

Sync is preview only. Cook is ED-M07. The adapter must:

- never resolve cooked paths,
- never trigger cook,
- never mount/unmount cooked roots.

Geometry sync may receive a `GeometryUri` that the engine cannot resolve in
mounted cooked roots. The engine API will return failure; adapter classifies
as `Rejected` with `OXE.LIVESYNC.GEOMETRY.UnresolvedAtRuntime` and includes
the URI in `AffectedScope.AssetVirtualPath`.

## 12. Operation Results And Diagnostics

`OperationResult` reduction rules (consumed by the command service):

| `SyncOutcome.Status` | Command's `OperationResult.Status` | `Severity` |
| --- | --- | --- |
| `Accepted` | (no result published) | n/a |
| `SkippedNotRunning` | `SucceededWithWarnings` | `Warning` |
| `Unsupported` | `SucceededWithWarnings` | `Warning` |
| `Rejected` | `PartiallySucceeded` | `Warning` |
| `Failed` | `PartiallySucceeded` | `Error` |
| `Cancelled` | `Cancelled` | `Info` |

`EnvironmentSyncResult.Overall` is the worst per-field status; per-field codes
appear as separate `DiagnosticRecord` entries.

`AffectedScope` populated by adapter:

- `SceneId`, `SceneName` — always.
- `NodeId`, `NodeName`, `ComponentType`, `ComponentName` — for node-scoped
  ops.
- `AssetVirtualPath` — for geometry/material URI failures.

Diagnostic codes (under `OXE.LIVESYNC.`):

- `NotRunning`, `RuntimeFaulted`, `Cancelled`.
- `TRANSFORM.Rejected`, `TRANSFORM.Failed`.
- `GEOMETRY.Rejected`, `GEOMETRY.Failed`, `GEOMETRY.UnresolvedAtRuntime`.
- `CAMERA.Rejected`, `CAMERA.Failed`.
- `LIGHT.Rejected`, `LIGHT.Failed`.
- `MATERIAL.Rejected`, `MATERIAL.Failed`.
- `ENVIRONMENT.<Field>.Unsupported`, `ENVIRONMENT.<Field>.Rejected`,
  `ENVIRONMENT.Rejected`.

## 13. Dependency Rules

Allowed:

- Commands → `ISceneEngineSync`.
- `SceneEngineSync` → `IEngineService`, `OxygenWorld`.

Forbidden:

- Inspector VMs → `ISceneEngineSync` directly.
- `SceneEngineSync` → mutate authoring (`Scene`/`SceneNode`/components).
- `SceneEngineSync` → import/cook/mount.
- `SceneEngineSync` → persist any state (no JSON, no settings, no view IDs).
- Any sync site throwing `NotImplementedException` outwards. All "no engine
  API" cases must classify as `Unsupported`.

## 14. Validation Gates

1. Each ED-M04 operation in §8.1 has a unit/integration test covering
   `Accepted`, `SkippedNotRunning`, and either `Unsupported` or `Rejected`
   paths (mocked `IEngineService`).
2. `EditMaterialSlot` maps descriptor URIs to cooked `.omat` engine paths,
   accepts clear operations, and never throws, with the `MaterialUri`
   populated in `AffectedScope.AssetVirtualPath`.
3. With engine `Running`, dragging position 100 samples produces throttled
   preview sync calls (for example, roughly one per 16 ms during a continuous
   drag), exactly one terminal sync call on commit, and exactly one undo entry.
4. With engine in `Faulted`, every Update* returns `SkippedNotRunning` with
   `OXE.LIVESYNC.RuntimeFaulted`. Authoring still succeeds.
5. Environment edit with engine `Running` queues native `SkyAtmosphere` and
   full native-parity `PostProcessVolume` updates, preserving native enum
   ordinals and authored scalar values, and reports those fields as `Accepted`.
   Fields without a native API, such as background color, return
   `Unsupported`. The command publishes a `SucceededWithWarnings` result only
   for unsupported or rejected fields.
6. No sync method throws `NotImplementedException` or any exception that
   escapes the adapter; verified by exception-tape test that calls every
   method against an in-process mock world.
7. Static check: no inspector view-model project references
   `SceneEngineSync` or `ISceneEngineSync`.

## 15. Open Issues

- Whether to extract per-component sub-adapters (`TransformSyncAdapter`,
  `LightSyncAdapter`, …). Default: no, keep one adapter for ED-M04; revisit
  if ED-M07/M08 introduce more diverse engine APIs.
- Whether engine should expose a presented-frame completion hook. Out of
  ED-M04. Visual validation remains manual.
- Coalescing window default (16 ms) may need tuning for slower machines.
  Confirm during implementation.
