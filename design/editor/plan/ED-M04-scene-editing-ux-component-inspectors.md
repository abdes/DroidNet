# ED-M04 Scene Editing UX And Component Inspectors Detailed Plan

Status: `accepted for implementation`

## 1. Purpose

Make the V0.1 scene component set authorable through production-quality
inspector UI. ED-M04 moves the current Transform/Geometry inspector baseline
onto the ED-M03 command/document foundation, adds production editors for
PerspectiveCamera, DirectionalLight, and scene Environment, introduces the
Geometry material identity slot, and routes supported edits through dirty,
undo/redo, persistence, diagnostics, and live-sync result handling.

ED-M04 is not a material editor or content-pipeline milestone. It prepares the
scene-side material slot and authoring data that ED-M05 and ED-M07 consume.

## 2. Planning Prompt Used

Plan ED-M04 from the accepted LLDs as an implementation-ready milestone plan,
not as architecture prose. Trace every ED-M04 LLD gate and IMPLEMENTATION_STATUS
checklist item into a task or explicit non-scope item. Name real code files,
services, data contracts, UI surfaces, tests, and manual validation steps.
Do not invent fake proof. Do not silently drop scope. Validation gates must be
observable by the user or asserted by real tests. Keep ED-M05 material authoring,
ED-M07 cook policy, ED-M09 viewport tools, and multi-viewport engine work out
of this milestone unless an ED-M04 LLD explicitly requires a seam.

## 3. PRD Traceability

| ID | ED-M04 Coverage |
| --- | --- |
| `GOAL-002` | V0.1 component and environment values become persisted authoring data. |
| `GOAL-003` | Supported inspector edits request live preview sync when runtime is available. |
| `GOAL-006` | Invalid edits and sync/settings failures produce visible diagnostics. |
| `REQ-005` | Add/remove/edit behavior for scoped V0.1 components. |
| `REQ-007` | Supported component and environment edits save and reopen. |
| `REQ-008` | Supported edits request live sync or visible unsupported/skipped result. |
| `REQ-009` | Transform, Geometry, PerspectiveCamera, DirectionalLight, Environment, material slot identity. |
| `REQ-022` | Inspector/save/sync/settings failures are visible. |
| `REQ-024` | Diagnostics distinguish scene authoring, asset identity, live sync, settings, runtime causes. |
| `REQ-026` | Embedded preview reflects supported edits where engine APIs exist. |
| `REQ-037` | Supported data round-trips without manual repair. |
| `SUCCESS-002` | Supported scene edits survive save/reopen. |
| `SUCCESS-003` | Live preview is updated or visibly explains why it could not update. |
| `SUCCESS-004` | Environment/material-slot authoring data is available for later cook/parity. |

## 4. Required LLDs

Only these LLDs gate ED-M04 implementation:

- [property-inspector.md](../lld/property-inspector.md)
- [environment-authoring.md](../lld/environment-authoring.md)
- [settings-architecture.md](../lld/settings-architecture.md)
- [live-engine-sync.md](../lld/live-engine-sync.md)
- [runtime-integration.md](../lld/runtime-integration.md), ED-M04 sync-readiness sections only
- [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md), vocabulary consumed by the above

Supporting context, not ED-M04 implementation gates:

- [material-editor.md](../lld/material-editor.md), for the ED-M04 to ED-M05 material-slot seam.
- [content-browser-asset-identity.md](../lld/content-browser-asset-identity.md), for future material picker compatibility.
- [documents-and-commands.md](../lld/documents-and-commands.md), for ED-M03 command/dirty/undo contracts.

## 5. Scope

ED-M04 includes:

- Extend `ISceneDocumentCommandService` with command-shaped component and
  environment edits.
- Move Transform and Geometry edits away from direct VM mutation/message
  handlers and onto command service methods.
- Add production inspector sections for PerspectiveCamera and
  DirectionalLight using the existing Transform/Geometry section style.
- Add scene-level Environment authoring data, inspector section, command,
  serialization, undo/redo, dirty state, and live-sync request boundary.
- Add Geometry material slot identity editing for component-level slot index 0,
  including unresolved/missing state preservation. ED-M04 implements a local
  identity field: list existing `MaterialAsset` records from `IAssetCatalog`
  when present and provide a raw/paste URI fallback so unresolved identity can
  be authored and validated before the ED-M05 Content Browser picker exists.
  ED-M05 owns real material creation, material editing, and reusable picker UX.
- Add `SyncOutcome`/`EnvironmentSyncResult`-style live-sync results and convert
  unsupported material/environment operations from throws/log-only behavior to
  visible warnings.
- Wrap runtime-session setting writes (`TargetFps`, logging verbosity) with
  `Runtime.Settings.Apply` result handling where those controls are touched by
  ED-M04.
- Focused tests for command mutation, validation, undo/redo, scene JSON
  round-trip, sync result mapping, and settings rejection where the existing
  test projects can host them.

## 6. Non-Scope

ED-M04 does not include:

- Material asset create/open/edit UI, scalar PBR material editing, material
  preview, or material cook workflow. Those are ED-M05.
- Texture authoring, texture picking, shader graphs, custom shaders, or
  material thumbnails.
- Full Content Browser state model or broad asset browser workflows. Those are
  ED-M06.
- Cooked descriptor generation, cook orchestration, cooked-index policy, or
  mount-after-cook refresh. Those are ED-M07.
- Standalone runtime parity. That is ED-M08.
- Viewport selection highlight, gizmos, icons, frame all/selected, or camera
  navigation. Those are ED-M09.
- Multi-viewport engine support. It remains deferred and must not block ED-M04.
- Production editors for OrthographicCamera, PointLightComponent, or
  SpotLightComponent. Existing add/remove can remain; missing editors show a
  raw/read-only or "not implemented in V0.1" block.
- A generic Settings panel.
- `IMaterialPickerService`, `MaterialPickerResult`, or any other ED-M05
  picker types. The ED-M04 material slot consumes `IAssetCatalog` directly
  with an `AssetReference<MaterialAsset>` filter; pre-paving ED-M05 picker
  contracts is forbidden.
- Per-component live-sync sub-adapters (`TransformSyncAdapter`,
  `LightSyncAdapter`, etc.). ED-M04 keeps a single `SceneEngineSync` per
  [live-engine-sync.md §15](../lld/live-engine-sync.md).
- Adding `FailureDomain.MaterialAuthoring`. That domain is owned by ED-M05
  ([material-editor.md §12.1](../lld/material-editor.md)). Material-slot
  authoring validation in ED-M04 uses `FailureDomain.SceneAuthoring`
  because the slot lives in scene data.

## 7. Implementation Sequence

### ED-M04.1 - Baseline Audit And LLD Lock

Goal: enter implementation with the exact migration surface known.

Tasks:

- Re-review the six gating LLDs and record review acceptance before code
  changes.
- Inventory current inspector mutation paths:
  - `SceneNodeEditorViewModel.OnTransformApplied`
  - `SceneNodeEditorViewModel.OnGeometryApplied`
  - `OnComponentAddRequested` / `OnComponentRemoveRequested`
  - `TransformViewModel` property change handlers
  - `GeometryViewModel.ApplyAssetAsync`
  - `SceneNodeDetailsViewModel` add/remove commands
- Inventory current sync throw/log-only paths in `SceneEngineSync`, especially
  material override and unsupported slot methods.
- Confirm exact test host:
  - `projects/Oxygen.Editor.World/tests/Oxygen.Editor.World.Tests.csproj`
  - `projects/Oxygen.Editor.WorldEditor/tests/SceneExplorer/Oxygen.Editor.WorldEditor.SceneExplorer.Tests.csproj`
  - `projects/Oxygen.Core/tests` for diagnostics vocabulary only if new
    constants are added.
- Record any remaining direct inspector mutation paths in this plan before
  implementation continues.

Exit:

- Gating LLDs accepted.
- File touch list and test target list are confirmed.

Audit result:

- Gating LLDs are accepted as the ED-M04 contract:
  `property-inspector`, `environment-authoring`, `settings-architecture`,
  `live-engine-sync`, `runtime-integration`, and
  `diagnostics-operation-results`.
- Confirmed test hosts:
  - `projects/Oxygen.Editor.World/tests/Oxygen.Editor.World.Tests.csproj`
  - `projects/Oxygen.Editor.WorldEditor/tests/SceneExplorer/Oxygen.Editor.WorldEditor.SceneExplorer.Tests.csproj`
  - `projects/Oxygen.Core/tests/Oxygen.Core.Tests.csproj`
- Current direct inspector mutation paths to migrate:
  - `TransformViewModel.OnPosition*/OnRotation*/OnScale*Changed` mutates
    `TransformComponent` directly, then sends
    `SceneNodeTransformAppliedMessage`.
  - `GeometryViewModel.ApplyAssetAsync` mutates
    `GeometryComponent.Geometry` directly, then sends
    `SceneNodeGeometryAppliedMessage`.
  - `SceneNodeDetailsViewModel` add/remove commands send global component
    messages; `SceneNodeEditorViewModel.ApplyAddComponent` /
    `ApplyRemoveComponent` mutate `SceneNode.Components`, write undo entries,
    and call `ISceneEngineSync` directly.
  - `SceneNodeDetailsViewModel.ApplyDefaultDirectionalLightTransform` mutates
    `TransformComponent` during directional-light add; ED-M04 command
    implementation must own that default so add-light remains one undoable
    command.
  - `SceneNodeDetailsViewModel.OnNameChanged` still mutates `SceneNode.Name`
    directly. Node rename is not an ED-M04 component-inspector gate, but this
    path must not be confused with the component edit command surface.
- Current sync paths to normalize:
  - `SceneEngineSync` per-component methods return legacy `Task`/`Task<bool>`
    and mostly log or swallow failures.
  - Material override, targeted material override, LOD, rendering, and lighting
    slot update methods throw `NotImplementedException`; ED-M04 must convert
    material-slot behavior to typed `Unsupported` outcomes instead of throws.
  - Per-call sync methods do not yet enforce the ED-M04 four-precondition
    runtime-readiness contract consistently (`Running`, non-null `World`,
    non-`Faulted`, non-cancelled).
- Current diagnostics alignment needed:
  - `SceneOperationKinds` does not yet contain ED-M04 component/environment
    operation constants.
  - Runtime setting rejection code currently uses specific
    `OXE.SETTINGS.*_REJECTED` strings; ED-M04 aligns touched runtime-session
    writes to `OXE.SETTINGS.RUNTIME.Rejected`.
- Current scene-domain gap:
  - `SceneData` has no `Environment` member and `Scene` has no environment
    authoring accessor. ED-M04.3 adds this with missing-field defaults for
    existing scene JSON.
- Current component-identity gap:
  - `RemoveComponentAsync(ctx, nodeId, componentId)` needs stable component
    identity, but `GameComponent` / `ComponentData` had no ID before ED-M04.2.
    ED-M04.2 adds persisted component IDs so the accepted command contract is
    implementable.

### ED-M04.2 - Command Contracts, Edit Records, And Operation Vocabulary

Goal: define the command seam all inspector sections consume.

Tasks:

- Extend `ISceneDocumentCommandService` with:
  - `EditTransformAsync`
  - `EditGeometryAsync`
  - `EditMaterialSlotAsync`
  - `EditPerspectiveCameraAsync`
  - `EditDirectionalLightAsync`
  - `AddComponentAsync`
  - `RemoveComponentAsync`
  - `EditSceneEnvironmentAsync`
- Add partial-write edit records:
  - `TransformEdit`
  - `GeometryEdit`
  - `PerspectiveCameraEdit`
  - `DirectionalLightEdit`
  - `SceneEnvironmentEdit`
  - an `Optional<T>` or equivalent field-presence wrapper.
- Add stable component identity to `GameComponent` / `ComponentData` so
  `RemoveComponentAsync(ctx, nodeId, componentId)` can target a component
  without passing UI object references.
- Add `EditSessionToken` as a command-layer concept:
  - one-shot for menu/button commits;
  - begin/update/commit/cancel for drag/text sessions;
  - one `HistoryKeeper` entry per committed session.
- Add operation constants to `SceneOperationKinds`:
  - `Scene.Component.EditTransform`
  - `Scene.Component.EditGeometry`
  - `Scene.Component.EditMaterialSlot`
  - `Scene.Component.EditCamera`
  - `Scene.Component.EditLight`
  - `Scene.Component.Add`
  - `Scene.Component.Remove`
  - `Scene.Environment.Edit`
- Add or verify diagnostic code prefixes in `Oxygen.Core.Diagnostics`:
  - `OXE.SCENE.*`
  - `OXE.DOCUMENT.*`
  - `OXE.LIVESYNC.*`
  - `OXE.SETTINGS.*`
  - `OXE.ASSETID.*`

Exit:

- Command service has compile-time contracts for every ED-M04 producer.
- No inspector UI needs to know sync, undo, or persistence details to perform
  an edit.

Implementation note:

- ED-M04.2 code currently defines the command contracts, edit records,
  `EditSessionToken`, operation vocabulary, and persisted component identity.
  Review is complete; ED-M04.3 may start.

### ED-M04.3 - Scene Environment Domain And Serialization

Goal: make environment real scene authoring data.

Tasks:

- Add `SceneEnvironmentData` in `Oxygen.Editor.World/src/Serialization/`.
- Extend `SceneData` with non-null `Environment` defaulting to
  `new SceneEnvironmentData()`.
- Add `Scene.Environment` accessor/mutation support according to the LLD.
- Update `Scene.Hydrate` / `Scene.Dehydrate` and `SceneJsonContext`.
- Implement environment defaults:
  - `AtmosphereEnabled = true`
  - `SunNodeId = null`
  - `ExposureMode = Auto`
  - `ExposureCompensation = 0`
  - `ToneMapping = Aces`
  - `BackgroundColor = (0,0,0)`
- Preserve stale `SunNodeId` on load; do not silently clear it.
- Add `Oxygen.Editor.World.Tests` round-trip tests:
  - missing environment field loads defaults;
  - all environment fields serialize/deserialize with equality;
  - stale `SunNodeId` survives load/save.

Exit:

- Scene files can persist environment data before any UI is wired.

Implementation note:

- ED-M04.3 code adds `SceneEnvironmentData`, environment modes,
  `SceneData.Environment`, command-owned `Scene.SetEnvironment`, source
  generator registration, and serializer tests for missing-field defaults,
  all-field round trip, and stale sun references. Review is complete; ED-M04.4
  may start.

### ED-M04.4 - Live Sync Result Adapter

Goal: make sync outcomes typed and non-crashing for inspector commands.

Tasks:

- Add `SyncStatus`, `SyncOutcome`, and `EnvironmentSyncResult` per
  `live-engine-sync.md`.
- Add typed `ISceneEngineSync` methods that return `SyncOutcome` for ED-M04
  operations while keeping legacy methods only where existing callers still
  require them.
- Implement runtime-readiness contract per
  [runtime-integration.md §12.1](../lld/runtime-integration.md). Each
  `Update*`/`Attach*`/`Detach*` adapter call MUST observe **all four**
  preconditions before invoking `OxygenWorld`, with no runtime locks beyond
  reading snapshot state:
  - `IEngineService.State == Running` → else `SkippedNotRunning` /
    `OXE.LIVESYNC.NotRunning`.
  - `IEngineService.World is not null` → else `SkippedNotRunning` /
    `OXE.LIVESYNC.NotRunning` (covers shutdown race; do **not** let it become
    a `NullReferenceException`).
  - `IEngineService.State != Faulted` → else `SkippedNotRunning` /
    `OXE.LIVESYNC.RuntimeFaulted`.
  - Caller-supplied `CancellationToken` not cancelled → else `Failed` /
    `OXE.LIVESYNC.Cancelled`.
- Implement engine-call classification:
  - missing engine API → `Unsupported`;
  - invalid engine rejection → `Rejected`;
  - unexpected exception → `Failed`.
- Implement the per-`(SceneId, NodeId)` coalescer in `SceneEngineSync` per
  [live-engine-sync.md §8.4](../lld/live-engine-sync.md):
  - default 16 ms idle window, configurable;
  - sub-commits during an open `EditSessionToken` update the in-flight value
    but do not call sync until the idle window elapses or the session
    commits;
  - `session.Commit()` cancels the timer and issues exactly one final sync;
  - `session.Cancel()` issues one sync with the pre-Begin value;
  - the coalescer is owned by `SceneEngineSync`, NOT by inspector view
    models.
- Convert current material override paths to typed `Accepted`/`Rejected`/`Failed`
  outcomes.
- Add `UpdateMaterialSlotAsync` that maps descriptor URIs to cooked `.omat`
  engine paths and queues the runtime material override with the material URI
  in affected scope.
- Add `UpdateEnvironmentAsync` with per-field `Unsupported` outcomes where
  engine APIs are absent.
- Ensure sync never starts the engine and never rolls back authoring state.
- Add tests with fake/mock runtime services in the existing test projects. If a
  seam cannot be instantiated without WinUI/native runtime, extract and test
  the pure classifier/helper instead of replacing the test with source-text
  assertions:
  - runtime not running maps to `SkippedNotRunning`;
  - `World == null` (engine in `Running` but world reference torn down) maps
    to `SkippedNotRunning`, not exception;
  - material slot maps descriptor URI to cooked `.omat` path and does not throw;
  - cancellation maps to `Cancelled`;
  - environment missing APIs aggregate unsupported fields;
  - coalescer test: 100 sub-commits across 100 ms produce ≤7 engine calls
    and exactly 1 final call after `session.Commit()`.

Exit:

- Command service can map sync results into `OperationResult` without catching
  raw unsupported exceptions from normal ED-M04 paths.
- Coalescer is implemented and tested at the `SceneEngineSync` layer; ED-M04.6
  inspector work consumes it without re-implementing it.

Implementation note:

- ED-M04.4 code adds typed `SyncOutcome` contracts, live-sync diagnostic
  codes, runtime-readiness classification, material override sync,
  environment sync aggregation with sun binding falling through light
  sync where possible, and executable preview/terminal/cancel coalescer
  methods on `ISceneEngineSync`. Tests cover not-running, faulted,
  cancellation, world-null race, legacy unsupported material paths, and
  preview throttling plus exactly one terminal sync delegate call. Review is
  complete; ED-M04.5 may start.

### ED-M04.5 - Command Implementation For Component Edits

Goal: make component edits dirty, undoable, validated, and diagnosable.

Tasks:

- Implement `EditTransformAsync`:
  - finite float validation;
  - reject zero scale axis;
  - preserve existing Euler-to-quaternion behavior;
  - multi-selection writes only supplied fields.
- Implement `EditGeometryAsync`:
  - update `GeometryComponent.Geometry`;
  - preserve unresolved geometry URIs;
  - request geometry attach/detach sync.
- Implement `EditMaterialSlotAsync`:
  - ensure `MaterialsSlot` at component override slot index 0 exists as needed;
  - set/clear `AssetReference<MaterialAsset>`;
  - preserve unresolved URI verbatim;
  - request material slot sync through the runtime override path.
- Implement `EditPerspectiveCameraAsync`:
  - clamp FOV to `[1,179]`;
  - reject `NearPlane <= 0`;
  - reject `NearPlane >= FarPlane`;
  - reject `FarPlane <= NearPlane`;
  - reject `AspectRatio <= 0`.
- Implement `EditDirectionalLightAsync`:
  - clamp color `[0,1]`;
  - clamp intensity and angular size to `>= 0`;
  - clamp exposure compensation `[-10,10]`;
  - enforce `IsSunLight = true` exclusivity across the scene in the same
    command and undo entry.
- Implement `AddComponentAsync` / `RemoveComponentAsync` cardinality:
  - Transform locked, never removable;
  - one Geometry per node;
  - one Camera subclass per node;
  - one Light subclass per node.
- Publish `OperationResult` for validation failures and sync warnings.
- Mark dirty only after successful authoring mutation.
- Register the validation diagnostic codes consumed by the inspector and
  asserted by tests in `Oxygen.Core/src/Diagnostics/DiagnosticCodes.cs` (or
  the existing scene-codes file). At minimum:
  - `OXE.SCENE.TransformComponent.Scale.ZeroAxis`
  - `OXE.SCENE.TransformComponent.Field.NotFinite`
  - `OXE.SCENE.PerspectiveCamera.NearFar.Invalid`
  - `OXE.SCENE.PerspectiveCamera.NearPlane.NonPositive`
  - `OXE.SCENE.PerspectiveCamera.AspectRatio.NonPositive`
  - `OXE.SCENE.DirectionalLight.Sun.Exclusivity` (informational)
  - `OXE.SCENE.ENVIRONMENT.SunRefStale` (warning)
  - `OXE.SCENE.ENVIRONMENT.<Field>.Invalid` for each environment enum/value
    field.
  Tests assert against these constants, never against magic strings.
- Material-slot validation diagnostics (e.g. malformed URI on paste) use
  `FailureDomain.SceneAuthoring`. Do **not** introduce
  `FailureDomain.MaterialAuthoring` in ED-M04; that is owned by ED-M05.

Exit:

- Component mutation no longer depends on direct inspector VM mutation for the
  ED-M04-supported fields.

Implementation note:

- `SceneDocumentCommandService` implements the ED-M04 component/environment
  command set for one-shot and committed sessions: Transform, Geometry,
  Material slot, PerspectiveCamera, DirectionalLight, Environment,
  AddComponent, and RemoveComponent.
- Geometry asset clear is rejected because `GeometryComponent` cannot dehydrate
  without a geometry reference; users detach geometry by removing the component.
- Material slot clear persists the `MaterialsSlot` sentinel URI so the authored
  slot shape remains stable for ED-M05.
- Live-sync outcomes are reduced per `live-engine-sync.md`: accepted publishes
  no result; skipped/unsupported publish warnings; rejected/failed publish
  partial-success results; cancelled publishes cancelled/info.
- Interactive edit-session coalescing remains the ED-M04.6 host migration
  responsibility. ED-M04.5 no-ops uncommitted sessions so preview samples do
  not create false history, dirty state, or terminal sync.
- Targeted tests cover locked transform removal denial, geometry clear
  rejection, material-slot sentinel persistence, directional sun exclusivity,
  and directional-light component add behavior.
- Subagent review completed; the only remaining re-review item was a stale
  `GeometryEdit` XML comment, now fixed. ED-M04.6 may start.

### ED-M04.6 - Inspector Host Migration And Edit Sessions

Goal: keep the existing good UI, but route it through commands.

Tasks:

- Preserve the current Transform/Geometry visual design:
  - collapsible section cards;
  - icon/title/description headers;
  - dense vector rows;
  - asset row with thumbnail/swatch placeholder and menu affordance.
- Introduce a component editor descriptor registry or keep the current host
  dictionary with explicit ED-M04 metadata. Do not leave ad hoc one-off editor
  discovery hidden in view-model constructors.
- Pass `ISceneDocumentCommandService`, `SceneDocumentCommandContext`, and
  `ISceneSelectionService` into inspector editors through the host.
- Remove direct `ISceneEngineSync` calls from inspector view models for
  ED-M04-supported fields.
- Replace `SceneNodeTransformAppliedMessage` and `SceneNodeGeometryAppliedMessage`
  as mutation paths for migrated fields. If adapter messages remain temporarily,
  they must delegate to the command service and be recorded as temporary.
- Add edit-session handling for the controls:
  - drag begin/update/commit/cancel;
  - text focus/Enter/focus-out;
  - mouse wheel idle coalescing.
- Keep `VectorBox`/`NumberBox` behavior. Do not replace them with plain text
  boxes.
- Asset-field mixed-value semantics for multi-selection (per
  [property-inspector.md §7.5](../lld/property-inspector.md)):
  - if all selected nodes share the same URI → show that asset;
  - otherwise → show `--` (indeterminate);
  - picking an asset writes that URI to all selected nodes in one command
    with one undo entry;
  - `<None>` writes `null` to all selected nodes.

Exit:

- Transform and Geometry editors look at least as good as the current UI and
  invoke commands for supported edits.

### ED-M04.7 - Transform, Geometry, And Material Slot UI

Goal: complete the upgraded existing sections.

Tasks:

- Transform:
  - show mixed values per axis;
  - commit per field/session through `EditTransformAsync`;
  - reject invalid scale with inline diagnostic;
  - undo/redo restores one user interaction.
- Geometry:
  - keep dynamic geometry asset menu from `IAssetCatalog`;
  - apply geometry URI through `EditGeometryAsync`;
  - show raw persisted URI in Raw/Diagnostics disclosure;
  - show missing/unresolved geometry warning without clearing the URI.
- Material slot:
  - render slot 0 in Geometry section as `[swatch/placeholder] [name/uri] [v]`;
  - flat list of `MaterialAsset` records pulled directly from
    `IAssetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All))` filtered
    to `MaterialAsset`. Do **not** introduce or consume
    `IMaterialPickerService` / `MaterialPickerResult`; those types are owned
    by ED-M05;
  - support `<None>` / clear action that calls
    `EditMaterialSlotAsync(ctx, nodeId, slotIndex: 0, materialUri: null, ct)`;
  - preserve unresolved URI verbatim across save/reopen;
  - provide a raw/paste URI fallback for unresolved material identities
    until ED-M05 ships the reusable picker;
  - do not create material assets or open Material Editor in ED-M04;
  - sync result is visible `Unsupported`, not an exception.

Exit:

- Existing Transform/Geometry quality is preserved and material identity is now
  visible and persistent.

### ED-M04.8 - PerspectiveCamera And DirectionalLight Editors

Goal: add the V0.1 camera/light component inspectors.

Tasks:

- Add `PerspectiveCameraViewModel` and view:
  - FOV;
  - Near/Far;
  - Aspect ratio under Advanced;
  - inline near/far cross-field error.
- Add `DirectionalLightViewModel` and view:
  - Color;
  - IntensityLux;
  - IsSunLight;
  - EnvironmentContribution;
  - CastsShadows;
  - AffectsWorld, AngularSizeRadians, ExposureCompensation under Advanced.
- Register both editors in the inspector host.
- Use the existing icon converter/style conventions.
- Support multi-selection mixed values.
- Route all edits through command service methods.
- Add/remove component menu respects cardinality:
  - hide or disable invalid component additions;
  - no remove affordance for locked Transform.

Exit:

- PerspectiveCamera and DirectionalLight have production editor sections with
  validation, undo/redo, dirty state, persistence, and sync request behavior.

### ED-M04.9 - Environment Section And Runtime Settings

Goal: make scene Environment authoring visible and separate from settings.

Tasks:

- Add Environment inspector section reachable from empty selection and the
  chosen Scene/Environment affordance.
- Implement fields:
  - AtmosphereEnabled;
  - Sun binding picker over nodes with `DirectionalLightComponent`;
  - ExposureMode;
  - ExposureCompensation;
  - ToneMapping;
  - BackgroundColor under Advanced.
- Route all environment edits through `EditSceneEnvironmentAsync`.
- Implement sun binding rules:
  - selecting a sun sets that light's `IsSunLight = true`;
  - clears every other directional light's `IsSunLight`;
  - undo restores the prior configuration in one step;
  - deleting the sun node leaves a stale reference warning until user clears or
    rebinds.
- Wrap runtime-session setting writes touched in ED-M04:
  - `TargetFps`;
  - `EngineLoggingVerbosity`;
  - failed writes restore displayed value and publish a `Settings`-domain
    `OperationResult` with code `OXE.SETTINGS.RUNTIME.Rejected`;
  - runtime-session setting writes do **not** mark the scene dirty.
- Do not add a generic Settings panel.

Exit:

- Environment authoring round-trips and unsupported runtime preview fields
  surface warnings without losing data.

### ED-M04.10 - Tests And Manual Validation Closure

Goal: close with real evidence.

Tasks:

- Add or update `Oxygen.Editor.World.Tests`:
  - `SceneEnvironmentData` defaults and all-field round trip;
  - `DirectionalLightData` / `PerspectiveCameraData` round-trip if uncovered;
  - material slot URI round-trip through `GeometryComponentData.OverrideSlots`.
- Add or update WorldEditor tests where WinUI-independent:
  - command validation rejects invalid scale;
  - command validation rejects camera near/far conflict;
  - directional light sun exclusivity and undo/redo;
  - material slot unresolved URI persists and produces unsupported sync outcome;
  - sync outcome mapping for not-running, faulted, unsupported, cancelled.
- Do not write tests that only assert comments, file existence, or source text.
- Prepare manual validation script for the user with exact expected visuals.
- Update IMPLEMENTATION_STATUS only after implementation and user validation.

Exit:

- Real tests cover non-UI logic.
- Manual validation steps are ready before claiming the milestone is landed.

## 8. Project/File Touch Points

Likely World domain files:

- `projects/Oxygen.Editor.World/src/Scene.cs`
- `projects/Oxygen.Editor.World/src/Serialization/SceneData.cs`
- `projects/Oxygen.Editor.World/src/Serialization/SceneEnvironmentData.cs` (new)
- `projects/Oxygen.Editor.World/src/Serialization/SceneJsonContext.cs`
- `projects/Oxygen.Editor.World/src/Components/PerspectiveCamera.cs`
- `projects/Oxygen.Editor.World/src/Components/LightComponents.cs`
- `projects/Oxygen.Editor.World/src/Components/GeometryComponent.cs`
- `projects/Oxygen.Editor.World/src/Slots/MaterialsSlot.cs`

Likely WorldEditor command/sync files:

- `projects/Oxygen.Editor.WorldEditor/src/Documents/Commands/ISceneDocumentCommandService.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Documents/Commands/SceneDocumentCommandService.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Documents/Commands/SceneCommandResult.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Documents/Commands/SceneDocumentCommandContext.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Services/ISceneEngineSync.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Services/SceneEngineSync.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Diagnostics/SceneOperationResults.cs`

Likely inspector files:

- `projects/Oxygen.Editor.WorldEditor/src/Inspector/SceneNodeEditorViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/SceneNodeDetailsViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/TransformViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/TransformView.xaml`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/Geometry/GeometryViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/Geometry/GeometryView.xaml`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/ComponentToGlyphConverter.cs`
- new `Inspector/Camera/*` files for PerspectiveCamera.
- new `Inspector/Lighting/*` files for DirectionalLight.
- new `Inspector/Environment/*` files for Environment.

Likely diagnostics files:

- `projects/Oxygen.Core/src/Diagnostics/SceneOperationKinds.cs`
- `projects/Oxygen.Core/src/Diagnostics/DiagnosticCodes.cs`
- `projects/Oxygen.Core/src/Diagnostics/FailureDomain.cs` only if the ED-M05
  domains already added in docs need code alignment before ED-M04 tests touch
  them.

Likely test files:

- `projects/Oxygen.Editor.World/tests/*`
- `projects/Oxygen.Editor.WorldEditor/tests/SceneExplorer/*`
- `projects/Oxygen.Core/tests/*` for diagnostics vocabulary constants.

## 9. Dependency And Migration Risks

| Risk | Mitigation |
| --- | --- |
| Inspector VMs keep direct mutation and sync calls. | ED-M04-supported fields must route through command service. Direct message handlers are deleted or made temporary command adapters. |
| Edit-session batching breaks `VectorBox` UX. | Preserve existing controls and add command/session behavior around them. Do not replace with lower-quality controls. |
| Sync throttling creates many undo entries. | Undo is owned by `EditSessionToken`; preview sync cadence never determines undo granularity. |
| Material slot turns into a hidden material editor. | ED-M04 stores identity only. No scalar material fields, no material document UI, no material cook. |
| Environment schema drifts from engine concepts. | ED-M04 fields use engine-aligned names/units and persist as scene authoring data; ED-M07 chooses cooked descriptor shape. |
| Runtime not running blocks authoring. | Commands commit authoring state and publish `LiveSync.SkippedNotRunning` warning. |
| Unsupported engine APIs throw. | Unsupported environment paths return `SyncOutcome.Unsupported`; material slots use the runtime override path and classify rejection/failure. |
| Point/spot/orthographic editor scope creeps in. | Existing add/remove can remain. Production editors are deferred unless trivial raw/read-only blocks are added. |
| Tests become fake because WinUI is hard to automate. | Test pure command/domain/sync seams. Mark true visual checks as manual with exact expected result. |
| ED-M02 multi-viewport remains deferred. | ED-M04 validates only the supported single live viewport path. |

## 10. Manual Validation Script

The user should validate these visually after implementation and build:

1. Open Vortex and open a scene.
2. Select a geometry node. The inspector shows the same quality Transform and
   Geometry sections as the current baseline: collapsible cards, dense vector
   rows, asset field with thumbnail/placeholder. (Layout-stability under
   validation/mixed/expand is a manual-only gate; not a coded test.)
3. Transform:
   - drag Position X;
   - press Undo once: the whole drag reverts;
   - press Redo once: the final drag value returns;
   - save/reopen: value persists.
4. Multi-selection:
   - select two nodes with different Position X;
   - Position X shows mixed/indeterminate;
   - enter a new X value;
   - both nodes receive it and one Undo reverts both.
5. Geometry:
   - pick a different geometry asset from the existing dynamic menu;
   - save/reopen: geometry identity persists;
   - load a seeded scene containing an unresolved geometry URI; the field
     shows a warning badge and preserves the URI verbatim.
6. Material slot:
   - Geometry section shows a Material row;
   - pick an existing material identity from the `IAssetCatalog` flat list
     when the catalog has one;
   - load a seeded scene containing an unresolved material URI such as
     `asset:///Content/Materials/Missing.omat`; the field shows the URI with
     a missing badge;
   - clear the material slot via `<None>`;
   - multi-select two geometry nodes with different material URIs: the slot
     shows `--`; picking an asset assigns it to both with one undo entry;
     `<None>` clears both;
   - unresolved identity is preserved across save/reopen;
  - live sync maps the descriptor URI to the cooked `.omat` runtime path and
    does not crash or block authoring.
7. PerspectiveCamera:
   - add/select a node with PerspectiveCamera;
   - edit FOV and Near/Far;
   - invalid Near >= Far rejects visibly and does not mutate state;
   - save/reopen preserves valid values.
8. DirectionalLight:
   - add/select directional lights A and B;
   - set A as Sun;
   - B is cleared as Sun in the same command;
   - Undo restores prior sun flags;
   - save/reopen preserves the final flags.
9. Environment:
   - open the Environment section from empty selection or Scene affordance;
   - toggle Atmosphere;
   - bind Sun to a directional light;
   - edit exposure compensation and tone map;
   - save/reopen preserves values;
   - if engine API is absent, unsupported warning appears without data loss.
10. Runtime/session settings:
    - change Target FPS/logging if the controls are visible;
    - invalid-state failure appears in output/log or inline near the control;
    - no scene dirty marker is set by runtime-session settings.

## 11. Code/Test Validation Gates

ED-M04 cannot be called landed until these are true:

1. Static/reference check: ED-M04-supported inspector VMs do not reference
   `ISceneEngineSync`, `IEngineService`, or `Oxygen.Editor.Interop` behavior
   APIs.
2. Transform command test: valid transform edit mutates selected nodes, marks
   dirty, records one undo entry per session, and sync result is captured.
3. Transform validation test: zero scale axis is rejected and authoring state
   is unchanged.
4. Camera validation test: `NearPlane >= FarPlane` is rejected and authoring
   state is unchanged.
5. Directional light test: setting `IsSunLight = true` clears other directional
   lights in the same scene and undo restores the prior state.
6. Environment serialization test: all fields round-trip, including stale
   `SunNodeId`.
7. Material slot test: unresolved URI persists through
   `GeometryComponentData.OverrideSlots` and sync returns `Unsupported`.
8. Sync mapping tests: not-running, faulted, unsupported, cancelled, rejected,
   and failed outcomes reduce to the documented operation statuses.
9. Settings test (required, not optional): a unit test against the runtime
   settings adapter with a mock `IEngineService` that rejects the write
   asserts (a) the rejection produces a `Settings`-domain `OperationResult`
   with code `OXE.SETTINGS.RUNTIME.Rejected`, (b) the document dirty tracker
   is **not** invoked, and (c) the displayed value is restored to the prior
   effective value.
10. Coalescer gate: a `SceneEngineSync` test drives 100 sub-commits across
    100 ms inside one `EditSessionToken` and asserts ≤7 engine calls during
    the session and exactly 1 final call after `session.Commit()`.
11. Manual validation script above is completed by the user, with failures
    recorded before closure.

## 12. Status Ledger Hook

Before implementation:

- mark ED-M04 required LLDs accepted only after review acceptance.
- mark this detailed plan accepted in `IMPLEMENTATION_STATUS.md` as a
  separate ledger row before any ED-M04.2 code lands. ED-M04.1's exit gate
  does not bind without this row.
- mark this detailed plan accepted only after review acceptance.

After implementation:

- update the ED-M04 checklist in `IMPLEMENTATION_STATUS.md`.
- record one ED-M04 validation ledger row after user visual validation and
  real test evidence.
- keep ED-M05 material editor and ED-M07 content pipeline items planned, not
  silently absorbed into ED-M04.
