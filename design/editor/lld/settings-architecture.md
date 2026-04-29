# Settings Architecture LLD

Status: `ED-M04 implementation-ready`

## 1. Purpose

Concrete placement and mutation design for every setting touched by ED-M04.
This LLD is not a survey of all future settings: it locks down where each
ED-M04 setting lives, how it is mutated, what marks dirty, what does not, and
which mutation path is forbidden. ED-M07 re-reviews project cook/content
settings; this document stays out of that scope.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-002`, `REQ-007`, `REQ-009`, `REQ-037` | Scene/environment values are real scene authoring data. |
| `GOAL-003`, `REQ-008`, `REQ-026` | Runtime-facing settings apply through runtime services. |
| `GOAL-006`, `REQ-022`, `REQ-024` | Rejected/invalid settings produce `OperationResult`. |

## 3. Architecture Links

- [property-inspector.md](./property-inspector.md): component edits use scene
  commands.
- [environment-authoring.md](./environment-authoring.md): scene environment
  storage decision.
- [runtime-integration.md](./runtime-integration.md): runtime FPS/logging.

## 4. Current Baseline

- `Oxygen.Editor.Runtime/src/Engine/IEngineSettings.cs` — startup engine
  config; consumed by `EngineService.InitializeAsync`. Persisted via
  `ISettingsService<IEngineSettings>` from DroidNet hosting.
- `IEngineService` exposes `TargetFps : uint`, `MaxTargetFps : uint`,
  `EngineLoggingVerbosity : int` (read/write valid in `Ready`/`Running`).
- `Scene` / `SceneNode` / components store authored values; ED-M04 adds
  `Scene.Environment` (see env LLD).
- Workspace / docking layout is persisted by existing editor data services
  (out of ED-M04 scope; ED-M01 owns this).

Brownfield gap: nothing defines, in writing, that `TargetFps` is a runtime
session setting (not a project setting), that environment is scene scope (not
editor scope), and that workspace activation may not call settings paths
that environment editing might trip over.

## 5. ED-M04 Setting Placement Matrix

For every ED-M04 setting, this matrix is normative. Implementation must reject
storing a setting outside its row.

| Setting | Scope | Owning service | Storage | Mutation API | Dirties scene? | Live-applied? |
| --- | --- | --- | --- | --- | --- | --- |
| `TransformComponent.LocalPosition/Rotation/Scale` | Scene component | `ISceneDocumentCommandService.EditTransformAsync` | scene file (`TransformData`) | command | yes | yes (sync) |
| `GeometryComponent.Geometry` (URI) | Scene component | `EditGeometryAsync` | `GeometryComponentData.GeometryUri` | command | yes | yes |
| `MaterialsSlot.Material` (URI) | Scene component | `EditMaterialSlotAsync` | `MaterialsSlotData.MaterialUri` | command | yes | yes, best-effort runtime override |
| `PerspectiveCamera.{FOV, Near, Far, Aspect}` | Scene component | `EditPerspectiveCameraAsync` | `PerspectiveCameraData` | command | yes | yes |
| `DirectionalLightComponent.*` | Scene component | `EditDirectionalLightAsync` | `DirectionalLightData` | command | yes | yes |
| `Scene.Environment.*` | Scene | `EditSceneEnvironmentAsync` | `SceneData.Environment` | command | yes | per-field, see env LLD |
| `IEngineService.TargetFps` | Runtime session | `IEngineService` setter | in-memory only | property setter (no command) | no | yes (immediate) |
| `IEngineService.EngineLoggingVerbosity` | Runtime session | `IEngineService` setter | in-memory only | property setter (no command) | no | yes (immediate) |
| `IEngineSettings` (startup) | Editor preference | `ISettingsService<IEngineSettings>` | DroidNet user-local settings | `ISettingsService.Save` | no | only on next engine init |
| Workspace docking, recent docs | Workspace | existing editor data services | user-local | (not ED-M04) | no | n/a |
| Project content roots, cook scope | Project | `Oxygen.Editor.Projects` | project metadata | (ED-M07) | n/a | n/a |

This table is the dispute-settler: any pull request that mutates one of these
values via a different path must be changed.

## 6. Mutation Path Rules

### 6.1 Scene-scope settings

Every scene-scope setting in §5 is mutated only through
`ISceneDocumentCommandService`. Direct property setters on `Scene`,
`SceneNode`, components, or slots are internal and only reachable from the
command implementation.

```csharp
// allowed
await commandService.EditTransformAsync(ctx, [nodeId], edit, session);

// forbidden in ED-M04
node.Components.OfType<TransformComponent>().First().LocalPosition = newPos;
```

The command path:

1. validates,
2. mutates,
3. records `HistoryKeeper` entry (one per `EditSessionToken`),
4. marks the scene dirty (existing document mechanism),
5. requests live sync,
6. publishes `OperationResult` for any warning or failure.

### 6.2 Runtime-session settings

`TargetFps` and `EngineLoggingVerbosity` are mutated through the
`IEngineService` setters. The scene editor toolbar / settings strip wraps each
write in a `Runtime.Settings.Apply` `OperationResult`:

- read pre-write value,
- attempt setter,
- on `InvalidOperationException` (wrong state) or any other failure, the
  setter throws — the wrapper catches, restores the displayed value, publishes
  `Failed` result with `FailureDomain.Settings` and code `OXE.SETTINGS.RuntimeRejected`.
- on success, publishes `Succeeded` (no `OperationResult` UI, only a log entry).

These writes never dirty any scene or document. They are not persisted by
ED-M04. (Whether they become durable preferences is in §15.)

### 6.3 Editor preferences (`IEngineSettings`)

Read once at engine `InitializeAsync`. Editing the preference between
sessions is allowed via existing `ISettingsService` pathways but is not part
of ED-M04 inspector UX. ED-M04 does not surface a settings panel for these.

### 6.4 Forbidden cross-scope writes

| Forbidden | Why |
| --- | --- |
| Storing scene environment in `IEngineSettings`. | Environment is scene authoring intent. |
| Storing `TargetFps` in `SceneData`. | Runtime preference is per-session, not per-scene. |
| Inspector code calling `ISettingsService<IEngineSettings>.Save` directly. | Inspector touches scene commands; preferences are not scene scope. |
| Project policy fields (cook scope, content roots) being edited from the inspector. | Owned by `Oxygen.Editor.Projects` and re-reviewed in ED-M07. |
| Diagnostic overrides (log level via env var, runtime DLL path override) becoming durable settings. | Bootstrap/diagnostic only. |

## 7. UI Surfaces (ED-M04)

Settings reach the user only through these surfaces in ED-M04:

- Inspector component sections — scene-scope fields per
  [property-inspector.md](./property-inspector.md).
- Inspector Environment section — scene-scope per
  [environment-authoring.md](./environment-authoring.md).
- Scene editor toolbar / runtime strip — `TargetFps`,
  `EngineLoggingVerbosity` (existing UI; wrap writes per §6.2).
- Output/log panel + inline error placement — for `Settings`-domain
  diagnostics.

ED-M04 does **not** introduce a generic "Settings" panel.

## 8. Persistence Behavior

| Setting class | Persisted? | When |
| --- | --- | --- |
| Scene-scope | yes | on `Scene.Save` (existing document path) |
| Runtime-session | no | session-only |
| Editor preference (`IEngineSettings`) | yes | by `ISettingsService` save |
| Diagnostic override | no | command-line / env var |
| Workspace layout | yes | by existing editor data services (not ED-M04) |

Round-trip rule for scene-scope: every field in §5 deserialized through
`SceneJsonContext` re-emits its in-memory value byte-for-byte (within
`float`/`Vector3`/`Quaternion` ULP).

## 9. Validation And Defaults

Per-setting validation lives in the command/setter, not in the UI control:

- Numeric ranges (camera near/far, exposure compensation, intensity ≥ 0,
  scale axes ≠ 0, FPS within `[1, MaxTargetFps]`) are enforced by the
  command/setter and surface diagnostics if rejected.
- Enums are constrained to the declared enum's defined members.
- Defaults are taken from component constructor defaults
  (`PerspectiveCamera.DefaultFieldOfViewDegrees = 60f`,
  `DirectionalLightComponent.DefaultIntensityLux = 100_000f`, etc.) and
  `SceneEnvironmentData` default initializer.
- Missing JSON fields deserialize to those defaults; the editor must not
  silently write back without a user edit.

## 10. Operation Result Mapping

| Failure | Domain | Code (existing prefix) |
| --- | --- | --- |
| Scene-scope value invalid | `SceneAuthoring` | `OXE.SCENE.*.Invalid` |
| Cross-field constraint | `SceneAuthoring` | `OXE.SCENE.*.<Constraint>` |
| Runtime setter rejected | `Settings` | `OXE.SETTINGS.RuntimeRejected` |
| Runtime not in `Ready`/`Running` | `Settings` | `OXE.SETTINGS.RuntimeStateInvalid` |
| Editor preference save failed | `Settings` | `OXE.SETTINGS.PreferenceSaveFailed` |
| Live sync rejected/unsupported | `LiveSync` | `OXE.LIVESYNC.*` (env LLD) |

`OperationResult.AffectedScope`:

- scene-scope failures set `SceneId`/`NodeId`/`ComponentType`/`ComponentName`
  where applicable;
- runtime/editor preferences leave node/scene blank.

## 11. Dependency Rules

Allowed:

- Inspector → `ISceneDocumentCommandService` (scene-scope).
- Scene editor toolbar → `IEngineService` (runtime-session).
- Existing editor preference UI → `ISettingsService<IEngineSettings>`.

Forbidden:

- Inspector ↔ `IEngineService` direct read/write of any setting.
- Inspector ↔ `ISettingsService` for any scope.
- Scene-scope write paths bypassing `ISceneDocumentCommandService`.
- Project-scope mutation from any ED-M04 module.

## 12. Validation Gates

1. Each row of §5 has a tested mutation path going through the named API.
   Tests mutate via API, save, reopen, assert equality (scene scope) or assert
   in-memory effect (runtime).
2. Static check (project references): inspector projects do not reference
   `Oxygen.Editor.Runtime.Engine.EngineService` or `IEngineSettings` types.
3. Scene save → reopen does not introduce any `Settings`-prefixed JSON outside
   the explicit scene authoring DTOs.
4. Mutating `TargetFps` in `Faulted` state surfaces `OXE.SETTINGS.RuntimeRejected`
   in the operation log; viewport keeps showing previous value.
5. No ED-M04 PR adds project/cook settings.

## 13. Open Issues

- Whether `TargetFps` and `EngineLoggingVerbosity` should become durable
  editor preferences post-ED-M04. Default for ED-M04: session-only.
- Whether to expose a centralized "Scene Settings" entry-point in the
  inspector breadcrumb in addition to the Environment section. Default: only
  the Environment section in ED-M04.
