# Environment Authoring LLD

Status: `ED-M04 implementation-ready`

## 1. Purpose

Concrete design for V0.1 scene environment authoring: sky atmosphere, sun
binding, and background intent, plus the adjacent scene-level post-processing
controls that affect preview and descriptor output. ED-M04 owns authoring data,
scene-level inspector sections, persistence, validation, command/undo, and the
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

- `Oxygen.Editor.World/src/Scene.cs` exposes `Scene.Environment` and normalizes
  missing/null nested environment data on hydrate and set.
- `Oxygen.Editor.World/src/Serialization/SceneData.cs` persists
  `SceneEnvironmentData` alongside `RootNodes` and `ExplorerLayout`.
- `DirectionalLightComponent` carries `IsSunLight` and
  `EnvironmentContribution`; `Scene.Environment.Edit` keeps sun binding and
  light flags coherent in one undo entry.
- `SceneEngineSync.UpdateEnvironmentAsync` owns the live-sync boundary. V0.1
  syncs sun binding through existing light paths and queues scene-level updates
  for native `SkyAtmosphere` and `PostProcessVolume` systems. Background color
  remains authored/persisted intent until a native clear/background API is
  exposed.
- `IEngineSettings` / `EngineSettingsService` remain process-startup engine
  config only. They do not store scene environment data.

## 5. Schema Decision

The Oxygen native **scene descriptor** contains an `environment.sky_atmosphere`
block. The editor authoring model mirrors the V0.1 scalar subset needed to
produce that native block without inventing an editor-only JSON schema. The
Oxygen material descriptor (`oxygen.material.v1`) is unrelated.

Decision (recorded for ED-M04):

1. **Augment editor scene authoring** with a new `SceneEnvironmentData`
   record persisted inside `SceneData`. This is the editor authoring source of
   truth for V0.1.
2. Field names and units mirror the native descriptor where the descriptor has
   fields today: meters for planet/atmosphere/scattering lengths, linear RGB
   for albedo and luminance factors, dimensionless scalars for Mie anisotropy
   and aerial controls.
3. Exposure, tone mapping, bloom, and color grading are post-processing
   controls, not environment controls. They are stored in
   `SceneEnvironmentData` only because the V0.1 scene DTO has one
   scene-settings record; the inspector renders them in separate
   domain-specific sections and live sync maps them to native
   `PostProcessVolume`.
4. ED-M07 cook output embeds the authored sky-atmosphere subset into the native
   scene descriptor. ED-M08 validates runtime parity from that cooked output.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.World` | `SceneEnvironmentData` DTO, `Scene.Environment` runtime accessor, defaults, hydrate/dehydrate. |
| `Oxygen.Editor.WorldEditor` Inspector | Scene-level Sky Atmosphere, Sun Binding, Exposure, Tone Mapping, Bloom, Color Grading, and Background sections (`SelectionPolicy.SceneOnly`). |
| `Oxygen.Editor.WorldEditor` Commands | `Scene.Environment.Edit`, undo, dirty, sync request. |
| `Oxygen.Editor.WorldEditor` Services / `SceneEngineSync` | Scene-settings sync adapter; sun-binding, sky-atmosphere, and post-process mapping to engine API. |
| `Oxygen.Editor.Runtime` | Engine readiness; surfaces missing API such as background color as `Unsupported`. |

## 7. Data Contracts

### 7.1 `SceneEnvironmentData` (new, in `Oxygen.Editor.World/src/Serialization/`)

```csharp
public sealed record SceneEnvironmentData
{
    public bool AtmosphereEnabled { get; init; } = true;

    public SkyAtmosphereEnvironmentData SkyAtmosphere { get; init; } = new();

    // Reference to the DirectionalLight node intended as the sun.
    // null = no sun bound. Stored as a node Guid that resolves to a node
    // owning a DirectionalLightComponent. The component's IsSunLight flag
    // is kept in sync by Scene.Environment.Edit (see §8.4).
    public Guid? SunNodeId { get; init; }

    // Legacy mirror fields kept only for old call sites; PostProcess is the
    // native-parity source of truth and Dehydrate normalizes these from it.
    public ExposureMode ExposureMode { get; init; } = ExposureMode.Auto;
    public float ManualExposureEv { get; init; } = 9.7f;
    public float ExposureCompensation { get; init; } = 0f;
    public ToneMappingMode ToneMapping { get; init; } = ToneMappingMode.AcesFitted;

    public PostProcessEnvironmentData PostProcess { get; init; } = new();

    // Optional clear/sky color when AtmosphereEnabled = false.
    // Linear RGB in [0,1]^3.
    public Vector3 BackgroundColor { get; init; } = new(0f, 0f, 0f);
}

public enum ExposureMode { Manual = 0, ManualCamera = 1, Auto = 2 }

public enum ToneMappingMode { None = 0, AcesFitted = 1, Filmic = 2, Reinhard = 3 }

public enum MeteringMode { Average = 0, CenterWeighted = 1, Spot = 2 }

public sealed record SkyAtmosphereEnvironmentData
{
    public float PlanetRadiusMeters { get; init; } = 6_360_000.0f;
    public float AtmosphereHeightMeters { get; init; } = 80_000.0f;
    public Vector3 GroundAlbedoRgb { get; init; } = new(0.4f, 0.4f, 0.4f);
    public float RayleighScaleHeightMeters { get; init; } = 8_000.0f;
    public float MieScaleHeightMeters { get; init; } = 1_200.0f;
    public float MieAnisotropy { get; init; } = 0.8f;
    public Vector3 SkyLuminanceFactorRgb { get; init; } = Vector3.One;
    public float AerialPerspectiveDistanceScale { get; init; } = 1.0f;
    public float AerialScatteringStrength { get; init; } = 1.0f;
    public float AerialPerspectiveStartDepthMeters { get; init; }
    public float HeightFogContribution { get; init; } = 1.0f;
    public bool SunDiskEnabled { get; init; } = true;
}

public sealed record PostProcessEnvironmentData
{
    public ToneMappingMode ToneMapper { get; init; } = ToneMappingMode.AcesFitted;
    public ExposureMode ExposureMode { get; init; } = ExposureMode.Auto;
    public bool ExposureEnabled { get; init; } = true;
    public float ExposureCompensationEv { get; init; }
    public float ExposureKey { get; init; } = 10.0f;
    public float ManualExposureEv { get; init; } = 9.7f;
    public float AutoExposureMinEv { get; init; } = -6.0f;
    public float AutoExposureMaxEv { get; init; } = 16.0f;
    public float AutoExposureSpeedUp { get; init; } = 3.0f;
    public float AutoExposureSpeedDown { get; init; } = 1.0f;
    public MeteringMode AutoExposureMeteringMode { get; init; } = MeteringMode.Average;
    public float AutoExposureLowPercentile { get; init; } = 0.1f;
    public float AutoExposureHighPercentile { get; init; } = 0.9f;
    public float AutoExposureMinLogLuminance { get; init; } = -12.0f;
    public float AutoExposureLogLuminanceRange { get; init; } = 25.0f;
    public float AutoExposureTargetLuminance { get; init; } = 0.18f;
    public float AutoExposureSpotMeterRadius { get; init; } = 0.2f;
    public float BloomIntensity { get; init; }
    public float BloomThreshold { get; init; } = 1.0f;
    public float Saturation { get; init; } = 1.0f;
    public float Contrast { get; init; } = 1.0f;
    public float VignetteIntensity { get; init; }
    public float DisplayGamma { get; init; } = 2.2f;
}
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
| `SkyAtmosphere.PlanetRadiusMeters` | finite, clamp minimum `1m` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.AtmosphereHeightMeters` | finite, clamp minimum `1m` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.GroundAlbedoRgb` | finite, clamp per-channel `[0,1]` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.RayleighScaleHeightMeters` | finite, clamp minimum `1m` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.MieScaleHeightMeters` | finite, clamp minimum `1m` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.MieAnisotropy` | finite, clamp `[-0.999,0.999]` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.SkyLuminanceFactorRgb` | finite, clamp minimum `0` per channel | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.AerialPerspectiveDistanceScale` | finite, clamp minimum `0` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.AerialScatteringStrength` | finite, clamp minimum `0` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.AerialPerspectiveStartDepthMeters` | finite, clamp minimum `0m` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.HeightFogContribution` | finite, clamp minimum `0` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` for non-finite |
| `SkyAtmosphere.SunDiskEnabled` | bool | — |
| `SunNodeId` | must resolve to a node owning a `DirectionalLightComponent`; `null` allowed | `OXE.SCENE.ENVIRONMENT.SunRefStale` (warning) |
| `PostProcess.ExposureMode` | native enum ordinal: `Manual=0`, `ManualCamera=1`, `Auto=2` | `OXE.SCENE.ENVIRONMENT.ExposureMode.Invalid` |
| `PostProcess.ExposureEnabled` | bool | — |
| `PostProcess.ManualExposureEv` | finite, clamp `[-24, 24]` | `OXE.SCENE.ENVIRONMENT.ManualExposure.Invalid` for non-finite |
| `PostProcess.ExposureCompensationEv` | finite | `OXE.SCENE.ENVIRONMENT.ManualExposure.Invalid` for non-finite |
| `PostProcess.ExposureKey` | finite, clamp minimum `0.001` | `OXE.SCENE.ENVIRONMENT.ManualExposure.Invalid` for non-finite |
| `PostProcess.ToneMapper` | native enum ordinal: `None=0`, `AcesFitted=1`, `Filmic=2`, `Reinhard=3` | `OXE.SCENE.ENVIRONMENT.ToneMapping.Invalid` |
| `PostProcess.AutoExposure*` | finite; min/max EV sorted; speeds/ranges/luminance/radius clamped to valid native ranges | `OXE.SCENE.ENVIRONMENT.ManualExposure.Invalid` for non-finite |
| `PostProcess.BloomIntensity`, `BloomThreshold`, `Saturation`, `Contrast`, `VignetteIntensity`, `DisplayGamma` | finite; non-negative where native expects non-negative, vignette `[0,1]`, gamma minimum `0.001` | `OXE.SCENE.ENVIRONMENT.ManualExposure.Invalid` for non-finite |
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
| `AtmosphereEnabled` + `SkyAtmosphere` | queued native `SkyAtmosphere` scene-system update | `Unsupported` warning, value persisted and cooked |
| `SunNodeId` | propagate via existing `AttachLightAsync` for the new sun light, or by re-applying `IsSunLight`/`EnvironmentContribution` | falls through existing light sync |
| `PostProcess` | queued native `PostProcessVolume` update. ED-M04 maps every authored field with native enum ordinals and names: exposure mode, enabled flag, key, manual EV, compensation EV, auto-exposure range/speeds/metering/histogram/window/target/spot radius, tone mapper, bloom, saturation, contrast, vignette, display gamma. | `Unsupported` warning only when the runtime API is unavailable |
| `BackgroundColor` | renderer clear-color call | `Unsupported` warning |

The adapter never throws when an API is missing. It returns a result aggregating
per-field statuses (see [live-engine-sync.md](./live-engine-sync.md) §7).

## 9. UI Surfaces

The scene-level settings sections are visible:

- when no scene node is selected (empty selection state shows "Scene"),
- never as a fake component on a node.

```text
+--------------------------------------------------------------+
| [icon] Sky Atmosphere                              [^/v]      |
|        Atmospheric sky and aerial perspective.                |
+--------------------------------------------------------------+
| Enabled       [ On ]                                          |
| Sun Disk      [x] Visible                                     |
| Planet Radius [ 6360.000 ] km                                 |
| Atmos. Height [   80.000 ] km                                 |
| Ground Albedo X [0.400] Y [0.400] Z [0.400]                  |
| Rayleigh Hgt. [    8.000 ] km                                 |
| Mie Height    [    1.200 ] km                                 |
| Mie Anisot.   [    0.800 ]                                    |
| Sky Luminance X [1.000] Y [1.000] Z [1.000]                  |
| Aerial Dist.  [    1.000 ]                                    |
| Aerial Str.   [    1.000 ]                                    |
| Aerial Start  [    0.000 ] m                                  |
| Height Fog    [    1.000 ]                                    |
+--------------------------------------------------------------+
| [icon] Sun Binding                                 [^/v]      |
+--------------------------------------------------------------+
| Sun Light     [ DirectionalLight 01 v ]          [clear]      |
| ! Stale reference (node deleted)                              |
+--------------------------------------------------------------+
| [icon] Exposure                                     [^/v]      |
+--------------------------------------------------------------+
| Enabled       [ On ]                                          |
| Mode          [ Auto v ]                                      |
| Compensation  [    0.000 ] EV                                 |
| Key           [   10.000 ]                                    |
| Metering      [ Average v ]             (Auto only)            |
| Auto Min EV   [   -6.000 ] EV100        (Auto only)            |
| Auto Max EV   [   16.000 ] EV100        (Auto only)            |
| Adapt Up      [    3.000 ] EV/s         (Auto only)            |
| Adapt Down    [    1.000 ] EV/s         (Auto only)            |
| Low Percent.  [    0.100 ]              (Auto only)            |
| High Percent. [    0.900 ]              (Auto only)            |
| Min Log Lum.  [  -12.000 ]              (Auto only)            |
| Log Lum Range [   25.000 ]              (Auto only)            |
| Target Lum.   [    0.180 ]              (Auto only)            |
| Spot Radius   [    0.200 ]              (Auto only)            |
| Manual EV     [    9.700 ] EV100        (Manual/Camera only)   |
+--------------------------------------------------------------+
| [icon] Tone Mapping                                [^/v]       |
+--------------------------------------------------------------+
| Mapper        [ ACES Fitted v ]                              |
| Display Gamma [    2.200 ]              (hidden when None)     |
+--------------------------------------------------------------+
| [icon] Bloom                                       [>/v]       |
+--------------------------------------------------------------+
| Intensity     [    0.000 ]                                    |
| Threshold     [    1.000 ]                                    |
+--------------------------------------------------------------+
| [icon] Color Grading                               [>/v]       |
+--------------------------------------------------------------+
| Saturation    [    1.000 ]              (hidden when None)     |
| Contrast      [    1.000 ]              (hidden when None)     |
| Vignette      [    0.000 ]              (hidden when None)     |
+--------------------------------------------------------------+
| [icon] Background                                   [^/v]     |
+--------------------------------------------------------------+
| Color         [swatch] X [0.00] Y [0.00] Z [0.00]             |
+--------------------------------------------------------------+
```

UI rules:

- The scene-level sections appear only when the scene itself is selected / no
  scene node is selected. They are not shown while editing a node.
- Sections are top-aligned in the inspector, using the same property-section
  composition as Transform/Geometry.
- Atmosphere and post-processing scalar/vector rows use DroidNet property
  sections, `DroidNet.Controls.NumberBox`, and
  `DroidNet.Controls.VectorBox`; no bespoke numeric editor is introduced.
- Exposure is its own section. `ManualExposureEv` is visible only for
  `Manual` and `ManualCamera`; the auto metering/range/adaptation/histogram
  rows are visible only for `Auto`. `ExposureCompensationEv` and
  `ExposureKey` remain visible because native fixed and auto exposure both
  consume them.
- Tone Mapping is its own section. `DisplayGamma` is hidden when
  `ToneMapper = None`.
- Bloom is its own section and is independent of tone-mapper selection.
- Color Grading is its own section and is hidden when `ToneMapper = None`.
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
- Defaults for new scenes are: `AtmosphereEnabled = true`,
  `SkyAtmosphere = new()`, `SunNodeId = null`
  (set automatically when the new scene's first directional light is created
  if the scene template does so), `PostProcess = new()` using native
  `PostProcessVolume` defaults (`ExposureMode = Auto`,
  `ManualExposureEv = 9.7`, `ExposureKey = 10.0`,
  `ExposureCompensationEv = 0`, `ToneMapper = AcesFitted`),
  `BackgroundColor = (0,0,0)`.
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

Cook: ED-M07 reads `Scene.Environment` and emits the native scene descriptor
`environment.sky_atmosphere` block for the supported sky-atmosphere subset.
ED-M04 must not embed cook policy.

Standalone parity: ED-M08 validates the cooked descriptor. ED-M04 only
guarantees authored data is present.

## 12. Operation Results And Diagnostics

| Failure | Domain | Code |
| --- | --- | --- |
| Invalid enum / out-of-range | `SceneAuthoring` | `OXE.SCENE.ENVIRONMENT.<Field>.Invalid` |
| Invalid sky-atmosphere scalar/vector | `SceneAuthoring` | `OXE.SCENE.ENVIRONMENT.SkyAtmosphere.Invalid` |
| Invalid manual exposure scalar | `SceneAuthoring` | `OXE.SCENE.ENVIRONMENT.ManualExposure.Invalid` |
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

1. New scene → scene-level settings show defaults (Atmosphere on, default sky
   atmosphere scalar/vector rows, no sun, Auto/manual EV 9.7/compensation
   0/Aces, black background).
2. Toggle Atmosphere → save → reopen → state preserved.
3. Bind sun to light A → A's `IsSunLight = true`, others `false`, single undo
   entry. Undo restores the previous configuration in one step.
4. Delete the sun node externally → Environment section shows stale-warning row;
   binding survives reopen.
5. Set `ManualExposureEv` to a non-finite value → rejected with
   `OXE.SCENE.ENVIRONMENT.ManualExposure.Invalid`; set `ExposureCompensation`
   to `+15` → clamped to `+10` silently; no error.
6. With engine `Running`, edits to sky-atmosphere and post-processing fields
   queue native scene-system updates and show a visible viewport change for
   visually observable values. Fields without a native API, such as background
   color, return `Unsupported`; authored value persists.
7. With engine not `Running`, edits succeed authoring-side, surface
   `LiveSync.SkippedNotRunning` warning.
8. Round-trip JSON test: load `SceneData`, mutate every environment field
   including `SkyAtmosphere`, serialize, deserialize, assert deep equality.
9. Null-safety test: loading a scene whose nested `SkyAtmosphere` is missing or
   null normalizes to defaults and does not crash scene-load UI refresh.
10. Cook descriptor generation test: authored sky atmosphere values appear in
    native scene descriptor JSON with matching units.
11. No `SceneEnvironmentData` reference appears in `Oxygen.Editor` settings,
   project, or runtime modules.

## 15. Open Issues

- Whether `BackgroundColor` should also accept HDR (`> 1.0`) values for
  non-atmosphere clear. Default ED-M04: clamped `[0,1]`; revisit when engine
  HDR clear API is exposed.
- Background native descriptor/API coverage is deferred. V0.1 persists that
  value and reports unsupported cook/live-sync coverage where applicable.
