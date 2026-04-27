# Environment Authoring LLD

Status: `ED-M04 implementation-ready`

## 1. Purpose

Concrete design for V0.1 scene environment authoring: atmosphere on/off, sun
binding, exposure, and tone mapping. ED-M04 owns authoring data, the
scene-level inspector section, persistence, validation, command/undo, and the
live-sync request boundary. Cook output and runtime parity are validated by
ED-M07 / ED-M08 and consume the data shape defined here.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-002` | Environment authored as real persisted scene data. |
| `GOAL-003` | Edits request live sync where engine API exists. |
| `GOAL-006` | Validation and sync failures surface as `OperationResult`. |
| `REQ-007` | Save/reopen preserves environment fields. |
| `REQ-008` | Mutations request live sync. |
| `REQ-009` | Environment included in V0.1 component/scene scope. |
| `REQ-022` | Failures visible. |
| `REQ-024` | Diagnostics distinguish authoring vs. sync vs. runtime causes. |
| `REQ-026` | Embedded preview reflects supported fields. |
| `REQ-037` | Survives save/reopen without manual repair. |
| `SUCCESS-002`, `SUCCESS-003`, `SUCCESS-004` | Authored, previewed, available for parity. |

## 3. Architecture Links

- [property-inspector.md](./property-inspector.md): hosts the Environment
  section; selection policy `SceneOnly`.
- [live-engine-sync.md](./live-engine-sync.md): defines the sync adapter and
  unsupported behavior.
- [settings-architecture.md](./settings-architecture.md): Environment is
  **scene** scope, not editor/project.

## 4. Current Baseline

Concrete state of the code:

- `Oxygen.Editor.World/src/Scene.cs` exposes `RootNodes`, `ExplorerLayout`,
  `Project`. **No `Environment` member.**
- `Oxygen.Editor.World/src/Serialization/SceneData.cs` defines
  `RootNodes : IList<SceneNodeData>` and `ExplorerLayout :
  IList<ExplorerEntryData>?`. **No environment field.**
- `DirectionalLightComponent` already carries `IsSunLight : bool` (default
  `true`) and `EnvironmentContribution : bool` (default `true`).
  `DirectionalLightData` mirrors both.
- `SceneEngineSync` applies directional light components via `AttachLightAsync`
  including the sun/environment-contribution flags. There is **no environment
  adapter** today (no atmosphere call, no exposure call, no tone-mapping
  call).
- `IEngineSettings` / `EngineSettingsService` carry process-startup engine
  config; renderer/post-process state is engine-default at runtime.

Brownfield gap summary: scene environment intent is implicit in
`DirectionalLightComponent` flags; the rest of the desired V0.1 surface
(atmosphere toggle, exposure, tone mapping) has no authoring storage and no
sync surface.

## 5. Schema Decision

The Oxygen engine **scene descriptor** does not currently expose an environment
sub-schema that the editor can consume directly: native engine state for
atmosphere/exposure/tone-mapping is set imperatively at runtime. The Oxygen
material descriptor (`oxygen.material.v1`) is unrelated. There is therefore no
existing engine JSON schema that fits as-is.

Decision (recorded for ED-M04):

1. **Augment editor scene authoring** with a new `SceneEnvironmentData`
   record persisted inside `SceneData`. This is the editor authoring source of
   truth for V0.1.
2. Field names and units mirror what the engine's renderer subsystems already
   consume (lux, EV stops, ACES/Reinhard tone-map identifiers) so ED-M07's
   cook step can emit a 1:1 engine descriptor without translating concepts.
3. ED-M07 owns the cook output: it emits a parallel descriptor (proposed
   filename `<scene>.env.json` or an inline block in the cooked scene
   descriptor; final shape decided by ED-M07). The editor authoring schema
   defined here is stable for ED-M04.
4. If a future engine-side environment descriptor schema lands (proposed name
   `oxygen.scene.environment.v1`), `SceneEnvironmentData` becomes a 1:1
   shadow. ED-M04 introduces no compatibility shim because no engine schema
   exists yet.

This is an "augment editor schema now, propose engine schema later" path. It
is not a parallel format introduced for UI convenience; it captures real
authored intent that has no engine-side home today.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.World` | `SceneEnvironmentData` DTO, `Scene.Environment` runtime accessor, defaults, hydrate/dehydrate. |
| `Oxygen.Editor.WorldEditor` Inspector | Environment section UI (`SelectionPolicy.SceneOnly`). |
| `Oxygen.Editor.WorldEditor` Commands | `Scene.Environment.Edit`, undo, dirty, sync request. |
| `Oxygen.Editor.WorldEditor` Services / `SceneEngineSync` | Environment sync adapter; sun-binding mapping to engine API. |
| `Oxygen.Editor.Runtime` | Engine readiness; surfaces missing-API as `Unsupported`. |

## 7. Data Contracts

### 7.1 `SceneEnvironmentData` (new, in `Oxygen.Editor.World/src/Serialization/`)

```csharp
public sealed record SceneEnvironmentData
{
    public bool AtmosphereEnabled { get; init; } = true;

    // Reference to the DirectionalLight node intended as the sun.
    // null = no sun bound. Stored as a node Guid that resolves to a node
    // owning a DirectionalLightComponent. The component's IsSunLight flag
    // is kept in sync by Scene.Environment.Edit (see §8.4).
    public Guid? SunNodeId { get; init; }

    public ExposureMode ExposureMode { get; init; } = ExposureMode.Auto;

    // EV stops applied on top of the chosen ExposureMode.
    // Range [-10, 10]; clamped on commit.
    public float ExposureCompensation { get; init; } = 0f;

    public ToneMappingMode ToneMapping { get; init; } = ToneMappingMode.Aces;

    // Optional clear/sky color when AtmosphereEnabled = false.
    // Linear RGB in [0,1]^3.
    public Vector3 BackgroundColor { get; init; } = new(0f, 0f, 0f);
}

public enum ExposureMode { Auto, Manual }

public enum ToneMappingMode { None, Aces, Reinhard, Filmic }
```

### 7.2 `SceneData` extension

```csharp
public sealed record SceneData : GameObjectData
{
    public IList<SceneNodeData> RootNodes { get; init; } = [];
    public IList<ExplorerEntryData>? ExplorerLayout { get; init; }
    public SceneEnvironmentData Environment { get; init; } = new();
}
```

`Environment` is non-nullable; missing in older JSON deserializes to defaults
(no migration required because no V0.1 scenes are released yet).

### 7.3 `Scene` accessor

```csharp
public sealed class Scene : GameObject
{
    public SceneEnvironmentData Environment { get; private set; } = new();
    // Mutated only by Scene.Environment.Edit command via internal setter.
}
```

`Hydrate` populates `Environment` from `SceneData.Environment`. `Dehydrate`
emits the current value.

### 7.4 Field validation

| Field | Rule | Diagnostic code |
| --- | --- | --- |
| `AtmosphereEnabled` | bool | — |
| `SunNodeId` | must resolve to a node owning a `DirectionalLightComponent`; `null` allowed | `OXE.SCENE.ENVIRONMENT.SunRefStale` (warning) |
| `ExposureMode` | enum | `OXE.SCENE.ENVIRONMENT.ExposureMode.Invalid` |
| `ExposureCompensation` | clamp `[-10, 10]` | clamp silently, no error |
| `ToneMapping` | enum | `OXE.SCENE.ENVIRONMENT.ToneMapping.Invalid` |
| `BackgroundColor` | clamp per-channel `[0, 1]` | clamp silently |

## 8. Commands, Services, Adapters

### 8.1 Operation kind

`Scene.Environment.Edit` (added to `SceneOperationKinds`).

### 8.2 Command surface

Defined in [property-inspector.md](./property-inspector.md) §8.1:

```csharp
Task<SceneCommandResult> EditSceneEnvironmentAsync(
    SceneDocumentCommandContext ctx,
    SceneEnvironmentEdit edit,
    EditSessionToken session);
```

`SceneEnvironmentEdit` is a partial-write record (each field nullable/optional).

### 8.3 Mutation rules

1. Validate fields per §7.4.
2. Write the merged `SceneEnvironmentData` onto `Scene.Environment`.
3. Record a single `HistoryKeeper` entry (or coalesce inside the
   `EditSessionToken` for drag/text interactions).
4. Mark scene dirty via the document command path.
5. Request live sync (§9).
6. Emit `OperationResult` with `AffectedScope.SceneId` set; on warning include
   the failing field code.

### 8.4 Sun binding rule

When `SunNodeId` changes:

- Within the same command, set `IsSunLight = true` on the new sun node's
  `DirectionalLightComponent` and `IsSunLight = false` on every other
  `DirectionalLightComponent` in the scene. This produces a single undo entry.
- Setting `SunNodeId = null` clears `IsSunLight` on all directional lights.
- A stale `SunNodeId` (node deleted) is preserved as-is until the user clears
  or rebinds it; the inspector renders an unresolved warning row.

### 8.5 Sync adapter

New method on `ISceneEngineSync`:

```csharp
Task<EnvironmentSyncResult> UpdateEnvironmentAsync(
    Scene scene,
    SceneEnvironmentData environment,
    CancellationToken cancellationToken = default);
```

Implementation in `SceneEngineSync` calls per-field engine APIs:

| Field | Engine call (when API exists) | If absent |
| --- | --- | --- |
| `AtmosphereEnabled` | renderer atmosphere toggle | `Unsupported` warning, value persisted |
| `SunNodeId` | propagate via existing `AttachLightAsync` for the new sun light, or by re-applying `IsSunLight`/`EnvironmentContribution` | falls through existing light sync |
| `ExposureMode` + `ExposureCompensation` | renderer exposure call | `Unsupported` warning |
| `ToneMapping` | renderer tone-map call | `Unsupported` warning |
| `BackgroundColor` | renderer clear-color call | `Unsupported` warning |

The adapter never throws when an API is missing. It returns a result aggregating
per-field statuses (see [live-engine-sync.md](./live-engine-sync.md) §7).

## 9. UI Surfaces

The Environment section is visible:

- when no scene node is selected (empty selection state shows "Scene"),
- through a "Scene → Environment" entry from the inspector breadcrumb at any
  time (selection-independent affordance),
- never as a fake component on a node.

```text
+--------------------------------------------------------------+
| [icon] Environment                                  [^/v]    |
|        Atmosphere, sun, exposure, tone mapping.              |
+--------------------------------------------------------------+
| Atmosphere   [x] enabled                                     |
| Sun          [v] DirectionalLight 01           [Bind] [X]    |
|              ! Stale reference (node deleted)                |
| Exposure     [Auto v]   Comp [ +0.0 ] EV                     |
| Tone Map     [ACES v]                                        |
| > Advanced                                                   |
|   Background [swatch] 0.00 0.00 0.00                         |
+--------------------------------------------------------------+
| ! Live preview of exposure/tone-mapping unavailable in       |
|   current engine API. Authoring saved.                       |
+--------------------------------------------------------------+
```

UI rules:

- Sun picker lists every node with a `DirectionalLightComponent`; selecting
  one issues `Scene.Environment.Edit { SunNodeId = id }` (which transitively
  flips `IsSunLight` per §8.4).
- Stale sun rendered as `! <stored guid prefix>` with a Clear button; never
  silently resets to `null`.
- Disabling Atmosphere does not hide the BackgroundColor row (kept as
  authoring intent).
- Section warning appears when any sync field returned `Unsupported` — the
  warning lists the affected field set.

## 10. Persistence And Round Trip

- `SceneEnvironmentData` is serialized through `SceneJsonContext` (add the
  type to the `JsonSerializerContext`).
- Save → reopen reproduces all fields exactly. `SunNodeId` survives even when
  the target node is missing.
- Defaults for new scenes are: `AtmosphereEnabled = true`, `SunNodeId = null`
  (set automatically when the new scene's first directional light is created
  if the scene template does so), `ExposureMode = Auto`, `Compensation = 0`,
  `ToneMapping = Aces`, `BackgroundColor = (0,0,0)`.
- Cook descriptor generation is ED-M07 scope. ED-M04 must persist the
  authored data unchanged.

## 11. Live Sync / Cook / Runtime Behavior

```text
Scene.Environment.Edit
  -> validate -> mutate -> dirty/undo
  -> ISceneEngineSync.UpdateEnvironmentAsync(scene, env)
        per-field: Accepted | Unsupported | Rejected | Failed
        runtime not Running -> entire op = SkippedNotRunning
  -> SceneCommandResult publishes OperationResult
        Succeeded                  if all Accepted
        SucceededWithWarnings      if any Unsupported / SkippedNotRunning
        PartiallySucceeded         if mix of Accepted + Rejected/Failed
        Failed                     authoring failed (validation only)
```

Cook: ED-M07 reads `Scene.Environment` and emits the engine descriptor. ED-M04
must not embed cook policy.

Standalone parity: ED-M08 validates the cooked descriptor. ED-M04 only
guarantees authored data is present.

## 12. Operation Results And Diagnostics

| Failure | Domain | Code |
| --- | --- | --- |
| Invalid enum / out-of-range | `SceneAuthoring` | `OXE.SCENE.ENVIRONMENT.<Field>.Invalid` |
| Stale `SunNodeId` | `SceneAuthoring` (warning) | `OXE.SCENE.ENVIRONMENT.SunRefStale` |
| Engine API missing for field | `LiveSync` | `OXE.LIVESYNC.ENVIRONMENT.<Field>.Unsupported` |
| Engine rejected call | `LiveSync` | `OXE.LIVESYNC.ENVIRONMENT.Rejected` |
| Runtime not running | `LiveSync` | `OXE.LIVESYNC.NotRunning` |

`AffectedScope` always sets `SceneId`/`SceneName`. Sun binding diagnostics also
set `NodeId`/`NodeName`.

## 13. Dependency Rules

Allowed:

- `Inspector.Environment.*` → `Documents.Commands` only.
- `Documents.Commands` → `ISceneEngineSync` (for `UpdateEnvironmentAsync`).
- `SceneEngineSync` → `IEngineService`/interop.

Forbidden:

- Environment values stored in `IEngineSettings`, project settings, or any
  editor-local settings store.
- Inspector calling `OxygenWorld` / interop directly.
- Hidden runtime fallback: if a field is `Unsupported`, the engine default
  applies but the authored value remains in the scene file.

## 14. Validation Gates

ED-M04 environment closure requires:

1. New scene → Environment section shows defaults (Atmosphere on, no sun,
   Auto/0/Aces, black background).
2. Toggle Atmosphere → save → reopen → state preserved.
3. Bind sun to light A → A's `IsSunLight = true`, others `false`, single undo
   entry. Undo restores the previous configuration in one step.
4. Delete the sun node externally → Environment section shows stale-warning row;
   binding survives reopen.
5. Set `ExposureCompensation` to `+15` → clamped to `+10` silently; no error.
6. With engine `Running` and exposure API available, edit shows visible
   change in viewport. Without API, `Unsupported` warning appears at section
   top; authored value persists.
7. With engine not `Running`, edits succeed authoring-side, surface
   `LiveSync.SkippedNotRunning` warning.
8. Round-trip JSON test: load `SceneData`, mutate every environment field,
   serialize, deserialize, assert deep equality.
9. No `SceneEnvironmentData` reference appears in `Oxygen.Editor` settings,
   project, or runtime modules.

## 15. Open Issues

- Whether `BackgroundColor` should also accept HDR (`> 1.0`) values for
  non-atmosphere clear. Default ED-M04: clamped `[0,1]`; revisit when engine
  HDR clear API is exposed.
- Engine API coverage for atmosphere/exposure/tone-mapping toggles is
  uncertain at ED-M04 start; the LLD assumes adapters return `Unsupported`
  until confirmed during implementation. No authoring contract changes when
  engine APIs land.
- ED-M07 will choose between inline embedding in the cooked scene descriptor
  vs. a sidecar `<scene>.env.json`; the editor authoring contract is
  decoupled from that choice.
