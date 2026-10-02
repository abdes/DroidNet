# Settings Architecture LLD

Status: `final V0.1 settings contract`

## 1. Purpose

Define where V0.1 scene, workspace, preview and startup settings live,
how they change, and which changes affect saved authoring data. Project content
policy remains in the project and content-pipeline contracts.

## 2. PRD Traceability

| ID                                          | Coverage                                                |
| ------------------------------------------- | ------------------------------------------------------- |
| `GOAL-002`, `REQ-007`, `REQ-009`, `REQ-037` | Scene/environment values are real scene authoring data. |
| `GOAL-003`, `REQ-008`, `REQ-026`            | Runtime-facing settings apply through runtime services. |
| `GOAL-006`, `REQ-022`, `REQ-024`            | Rejected/invalid settings produce `OperationResult`.    |

## 3. Architecture Links

- [property-inspector.md](./property-inspector.md): component edits use scene
  commands.
- [environment-authoring.md](./environment-authoring.md): scene environment
  storage decision.
- [runtime-integration.md](./runtime-integration.md): runtime FPS/logging.

## 4. Service ownership

- `Oxygen.Editor.Runtime/src/Engine/IEngineSettings.cs` — startup engine
  config; consumed by `EngineService.InitializeAsync`. Persisted via
  `ISettingsService<IEngineSettings>` from DroidNet hosting.
- `IEngineService` exposes `TargetFps : uint`, `MaxTargetFps : uint`,
  `EngineLoggingVerbosity : int` (read/write valid in `Ready`/`Running`).
- `Scene` / `SceneNode` / components and `Scene.Environment` store authored
  values.
- Workspace / docking layout is persisted by existing editor data services
  and the typed settings manager.

## 5. Setting placement

For every listed setting, this matrix is normative. Implementation must reject
storing a setting outside its row.

| Setting                                                       | Scope                           | Owning service                                    | Storage                                          | Mutation API                             | Dirties scene?   | Live-applied?                                        |
| ------------------------------------------------------------- | ------------------------------- | ------------------------------------------------- | ------------------------------------------------ | ---------------------------------------- | ---------------- | ---------------------------------------------------- |
| `TransformComponent.LocalPosition/Rotation/Scale`             | Scene component                 | `ISceneDocumentCommandService.EditTransformAsync` | scene file (`TransformData`)                     | command                                  | yes              | yes (sync)                                           |
| `GeometryComponent.Geometry` (URI)                            | Scene component                 | `EditGeometryAsync`                               | `GeometryComponentData.GeometryUri`              | command                                  | yes              | yes                                                  |
| Material overrides by geometry identity/SlotId                | Scene component                 | Material-slot command                             | Canonical scene override records                 | command                                  | yes              | yes; observed effect qualified in M08                |
| `PerspectiveCamera.{FOV, Near, Far, AspectMode, FixedAspect}` | Scene component                 | Camera command                                    | Canonical perspective-camera data                | command                                  | yes              | yes; target resize changes only effective view state |
| Node visibility / geometry cast-receive source modes          | Scene node                      | Property command                                  | Local/Inherit source modes in scene data         | command                                  | yes              | yes                                                  |
| Directional atmosphere assignment                             | Light component                 | Light command                                     | None/Primary/Secondary in light data             | command                                  | yes              | yes; scene summary is read-only                      |
| `DirectionalLightComponent.*`                                 | Scene component                 | `EditDirectionalLightAsync`                       | `DirectionalLightData`                           | command                                  | yes              | yes                                                  |
| `Scene.Environment.*`                                         | Scene                           | `EditSceneEnvironmentAsync`                       | `SceneData.Environment`                          | command                                  | yes              | per-field, see env LLD                               |
| Preview FPS                                                   | User/project                    | `PreviewSettingsService` → `IEngineService`       | project-scoped SQLite setting                    | preview preference                       | no               | yes (immediate)                                      |
| Native log verbosity                                          | User/project                    | `PreviewSettingsService` → `IEngineService`       | project-scoped SQLite setting                    | preview preference                       | no               | yes (immediate)                                      |
| `IEngineSettings` (startup)                                   | Editor preference               | `ISettingsService<IEngineSettings>`               | DroidNet user-local settings                     | `ISettingsService.Save`                  | no               | only on next engine init                             |
| Workspace docking, recent docs                                | Workspace                       | existing editor data services                     | user-local                                       | workspace services                       | no               | n/a                                                  |
| Editor Hide / Show All                                        | Workspace per project and scene | WorldEditor workspace-visibility service          | `IEditorSettingsManager`, project-scoped setting | workspace command, not authoring command | no               | editing main-view mask only                          |
| Project content roots, cook scope                             | Project                         | `Oxygen.Editor.Projects`                          | project metadata                                 | project/content commands                 | no scene changes | ordered mount/publication workflow                   |

The owning service enforces each mutation boundary.

### 5.1 Workspace visibility storage and lifetime

WorldEditor owns the typed `WorldEditor/SceneVisibility` setting through the
existing `IEditorSettingsManager`. Use `SettingContext.Project` with the
canonical project root. The versioned payload contains the project ID and a map
of scene IDs to sets of explicitly editor-hidden authored node IDs. The data
store is user-local; no Hide state is written to scene documents, authoring
mounts, cooked output or validation requests.

Hiding a node masks its geometry/gizmo representation and descendants in the
editing main view. It does not mutate native Scene Visibility, light properties
or caster eligibility. Showing a parent removes only that parent's local Hide
entry, leaving child entries intact; Show All clears the current scene's set.
These commands persist workspace state without authoring history/dirty changes
or automatic-cooking demand.

Apply the mask through the current runtime view generation and authored-to-native
node map. A callback for another project, scene activation or view generation is
discarded. Unknown node IDs have no rendering effect. Retain local entries during
an open document's undo lifetime; prune unresolved IDs when loading a saved scene
into a fresh document lifetime. Reject a stored project-ID mismatch even if its
path-based settings scope is reused by a different project.

Controlled qualification views omit workspace masks and restore the current
valid editing mask on return. Their opt-in adapters do not save a temporary
profile, camera ratio or Hide state. Qualification EvidenceRoot is an explicit
development-run option, not a persistent editor/project preference.

## 6. Mutation Path Rules

### 6.1 Scene-scope settings

Every scene-scope setting in §5 is mutated only through
`ISceneDocumentCommandService`. Direct property setters on `Scene`,
`SceneNode`, components, or slots are internal and only reachable from the
command implementation.

```csharp
// allowed
await commandService.EditTransformAsync(ctx, [nodeId], edit, session);

// forbidden: bypasses validation, history, revisions and live delivery
node.Components.OfType<TransformComponent>().First().LocalPosition = newPos;
```

The command path:

1. validates,
2. mutates,
3. records `HistoryKeeper` entry (one per `EditSessionToken`),
4. marks the scene dirty (existing document mechanism),
5. requests live sync,
6. publishes `OperationResult` for any warning or failure.

### 6.2 Project preview preferences

`PreviewSettingsService` owns FPS and native log verbosity for the active project.
The defaults are **60 FPS** and **Error (`-2`)**. Every scene tab binds to the same
service; edits apply through `IEngineService` and save automatically using
`IEditorSettingsManager`, key `WorldEditor/Preview`, with `SettingContext.Project`
for the canonical, case-insensitive Windows project root. The payload includes the project ID; a different
project reusing that path starts with defaults.

These are personal editing preferences, stored in the user-local SQLite database.
They never dirty scenes, enter `Project.oxy`, affect cooking or travel with a copied
project. Pause and single-step state remain temporary.

Restore before `StartAsync`, or before mounting the next project when the engine
is already running. A retired workspace's delayed load cannot change the active
runtime. Native startup uses the same defaults; startup JSON and CVar archives do
not own these toolbar preferences.

Use a dedicated settings-manager/DbContext instance and serialize its reads and
writes across project switches. Each write retains the originating project and
accepted values. Slider changes retain one active write and the latest pending
value; closing a scene commits focused input, then drains pending writes before
workspace teardown.
A rejected runtime write keeps the previous displayed value and publishes a
`Runtime.Settings.Apply` failure. A storage failure keeps the live value and reports
“Applied for this session, but could not save.” Load failures use the defaults and
publish the error. Both paths log the exception and project identity.

### 6.3 Editor preferences (`IEngineSettings`)

Read once at engine `InitializeAsync`. Editing the preference between
sessions is allowed via existing `ISettingsService` pathways but is not part
of the inspector. V0.1 does not add a settings panel for these preferences.

### 6.4 Forbidden cross-scope writes

| Forbidden                                                                                          | Why                                                                |
| -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| Storing scene environment in `IEngineSettings`.                                                    | Environment is scene authoring intent.                             |
| Storing `TargetFps` in `SceneData`.                                                                | Preview preference is per user/project, not per scene.             |
| Inspector code calling `ISettingsService<IEngineSettings>.Save` directly.                          | Inspector touches scene commands; preferences are not scene scope. |
| Project policy fields (cook scope, content roots) being edited from the inspector.                 | Owned by `Oxygen.Editor.Projects` and the content workflow.        |
| Diagnostic overrides (log level via env var, runtime DLL path override) becoming durable settings. | Bootstrap/diagnostic only.                                         |

## 7. UI surfaces

Settings reach the user through these surfaces:

- Inspector component sections — scene-scope fields per
  [property-inspector.md](./property-inspector.md).
- Inspector Environment section — scene-scope per
  [environment-authoring.md](./environment-authoring.md).
- Scene editor toolbar / runtime strip — `TargetFps`,
  `EngineLoggingVerbosity` (existing UI; wrap writes per §6.2).
- Output/log panel + inline error placement — for `Settings`-domain
  diagnostics.

V0.1 does not introduce a generic Settings panel.

## 8. Persistence Behavior

| Setting class                         | Persisted? | When                                             |
| ------------------------------------- | ---------- | ------------------------------------------------ |
| Scene-scope                           | yes        | on `Scene.Save` (existing document path)         |
| Preview FPS and logging               | yes        | automatically in project-scoped SQLite state     |
| Pause and single-step                 | no         | session-only                                     |
| Editor preference (`IEngineSettings`) | yes        | by `ISettingsService` save                       |
| Diagnostic override                   | no         | command-line / env var                           |
| Workspace layout                      | yes        | by existing editor data services                 |
| Workspace Hide                        | yes        | through the project-scoped typed setting in §5.1 |

Scene round trips preserve typed values, identities and source modes through
`SceneJsonContext`. Text formatting need not match input bytes. The property
pipeline owns conversion and numerical qualification rules.

## 9. Validation And Defaults

Per-setting validation lives in the command/setter, not in the UI control:

- Numeric ranges (camera near/far, exposure compensation, intensity ≥ 0,
  scale axes ≠ 0, FPS within `[1, MaxTargetFps]`) are enforced by the
  command/setter and surface diagnostics if rejected.
- Enums are constrained to the declared enum's defined members.
- The inspector, material and environment field tables define creation defaults.
  Constructors, schema defaults and canonical writers implement those values.
- Only fields declared optional by the current schema use its defaults. Missing
  required fields or obsolete representations require rejection or explicit
  migration; runtime readers do not infer a legacy format.

## 10. Operation Result Mapping

| Failure                          | Domain           | Code (existing prefix)              |
| -------------------------------- | ---------------- | ----------------------------------- |
| Scene-scope value invalid        | `SceneAuthoring` | `OXE.SCENE.*.Invalid`               |
| Cross-field constraint           | `SceneAuthoring` | `OXE.SCENE.*.<Constraint>`          |
| Runtime setter rejected          | `Settings`       | `OXE.SETTINGS.TARGET_FPS_REJECTED`  |
| Runtime not in `Ready`/`Running` | `Settings`       | `OXE.SETTINGS.RuntimeStateInvalid`  |
| Editor preference save failed    | `Settings`       | `OXE.SETTINGS.PreferenceSaveFailed` |
| Live sync rejected/unsupported   | `LiveSync`       | `OXE.LIVESYNC.*` (env LLD)          |

`OperationResult.AffectedScope`:

- scene-scope failures set `SceneId`/`NodeId`/`ComponentType`/`ComponentName`
  where applicable;
- runtime/editor preferences leave node/scene blank.

## 11. Dependency Rules

Allowed:

- Inspector → `ISceneDocumentCommandService` (scene-scope).
- Scene editor toolbar → `PreviewSettingsService` → `IEngineService` (live application) and `IEditorSettingsManager` (persistence).
- Existing editor preference UI → `ISettingsService<IEngineSettings>`.

Forbidden:

- Inspector ↔ `IEngineService` direct read/write of any setting.
- Inspector ↔ `ISettingsService` for any scope.
- Scene-scope write paths bypassing `ISceneDocumentCommandService`.
- Project-scope mutation outside the owning project/content services.

## 12. Validation Gates

1. Each row of §5 has a tested mutation path going through the named API.
   Tests mutate via API, save, reopen, assert equality (scene scope) or assert
   live application and reopen/project-switch restoration (preview preferences).
2. Static check (project references): inspector projects do not reference
   `Oxygen.Editor.Runtime.Engine.EngineService` or `IEngineSettings` types.
3. Scene save → reopen does not introduce any `Settings`-prefixed JSON outside
   the explicit scene authoring DTOs.
4. Mutating `TargetFps` in `Faulted` state surfaces `OXE.SETTINGS.TARGET_FPS_REJECTED`
   in the operation log; viewport keeps showing previous value.
5. Workspace Hide persists independently of authored data and project publication;
   stale project/node identities follow §5.1.

## 13. V0.1 boundary

Preview FPS and native log verbosity persist automatically per user/project; no
persistence toggle or project renderer preset is introduced. Scene settings use the existing empty-
selection Environment surface. There is no generic project settings panel.
Projects supplies mount/cook facts to ContentPipeline; native startup preferences
remain editor-local. Runtime capability and artifact compatibility follows the
runtime-integration contract.

The canonical [property-pipeline.md](./property-pipeline.md) governs typed
property entry points, shared sessions/history, current field diagnostics and
revision-aware runtime convergence. Existing record adapters implement the same
contract; they are not an alternative architecture.
