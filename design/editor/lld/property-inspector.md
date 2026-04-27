# Property Inspector LLD

Status: `ED-M04 implementation-ready`

## 1. Purpose

Concrete editor design for the V0.1 property inspector. ED-M04 must turn the
current `SceneNodeEditorViewModel` / `TransformViewModel` / `GeometryViewModel`
baseline into a command-backed, undoable, dirty-aware, multi-selection-aware
inspector for the V0.1 component set: `TransformComponent`,
`GeometryComponent` (with `MaterialsSlot` reference only), `PerspectiveCamera`,
`DirectionalLightComponent`, and the scene-level Environment section.

Out of scope here: Project Browser, asset import/cook, the real Material Editor
(see [material-editor.md](./material-editor.md)), Content Browser picker
internals (see [content-browser-asset-identity.md](./content-browser-asset-identity.md)),
viewport gizmos.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-002` | Inspector edits make V0.1 static scene authoring usable. |
| `GOAL-003` | Supported edits request live preview sync when the embedded engine is available. |
| `GOAL-006` | Invalid edits and sync failures surface operation results or field diagnostics. |
| `REQ-005` | Add, remove, and edit operations exist for V0.1 components in scope. |
| `REQ-006` | Component edits use command-shaped mutation paths and update dirty state. |
| `REQ-007` | Supported component and environment values save and reopen. |
| `REQ-008` | Supported mutations request live sync. |
| `REQ-009` | V0.1 component scope: Transform, Geometry, PerspectiveCamera, DirectionalLight, Environment, and material assignment/override slot. |
| `REQ-022` | Save/sync failures triggered by inspector edits are visible. |
| `REQ-024` | Diagnostics identify authoring, missing content, sync, settings, or runtime failure domains. |
| `REQ-026` | Embedded preview reflects supported component and environment edits where engine APIs exist. |
| `REQ-037` | Supported V0.1 data survives save/reopen without manual repair. |
| `SUCCESS-002` | Supported scene edits survive save/reopen. |
| `SUCCESS-003` | Live preview shows authored scene content. |

## 3. Architecture Links

- `ARCHITECTURE.md`: scene-authoring command path, diagnostics policy, and
  dependency direction.
- `DESIGN.md`: property inspector LLD ownership and cross-LLD workflow chains.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.WorldEditor` owns inspector UI;
  `Oxygen.Editor.World` owns scene/component authoring data.
- `documents-and-commands.md`: command, dirty-state, undo/redo, and save
  contracts consumed by inspector edits.
- `live-engine-sync.md`: sync request contract consumed after command commits.
- `diagnostics-operation-results.md`: operation result and failure-domain
  vocabulary.

## 4. Current Baseline

Concrete code in scope:

- `Oxygen.Editor.WorldEditor/src/Inspector/SceneNodeEditorViewModel.cs` — host
  for component editors. Currently selects per-type editors via a static
  `AllPropertyEditorFactories` dictionary, registers undo entries directly
  through `HistoryKeeper`, and dispatches sync via `ISceneEngineSync` from
  message handlers.
- `Inspector/TransformViewModel.cs` — exposes `PositionX/Y/Z`, `RotationX/Y/Z`,
  `ScaleX/Y/Z` (all `float`) plus matching `*IsIndeterminate` booleans for
  multi-selection mixed-value display. Sends `SceneNodeTransformAppliedMessage`.
- `Inspector/Geometry/GeometryViewModel.cs` — populates `Groups : AssetGroup[]`
  ("Engine"/"Content") via `IAssetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All))`
  and reacts to `IAssetCatalog.Changes`. Sends
  `SceneNodeGeometryAppliedMessage`.
- `Inspector/SceneNodeDetailsViewModel.cs` — owns `Add*Command` /
  `DeleteComponentCommand` for Geometry, Perspective/Orthographic camera, and
  Directional/Point/Spot light, plus node `Name` editing.
- Domain types in `Oxygen.Editor.World/src/Components/`: `TransformComponent`,
  `GeometryComponent`, `PerspectiveCamera`, `OrthographicCamera`,
  `DirectionalLightComponent`, `PointLightComponent`, `SpotLightComponent`.
- Slot types in `Oxygen.Editor.World/src/Slots/`: `MaterialsSlot` (holds
  `AssetReference<MaterialAsset> Material`), `RenderingSlot`, `LightingSlot`,
  `LevelOfDetailSlot`.
- ED-M03 command surface in `WorldEditor/src/Documents/Commands/`:
  `ISceneDocumentCommandService`, `SceneCommandResult`,
  `SceneDocumentCommandContext(DocumentId, Metadata, Scene, History)`,
  `SceneOperationKinds` (string constants).
- ED-M03 selection in `WorldEditor/src/Documents/Selection/`:
  `ISceneSelectionService.GetSelectedNodes(Guid documentId, Scene scene)`.

Concrete gaps ED-M04 closes:

1. No editor exists for `PerspectiveCamera`, `DirectionalLightComponent`, or
   scene-level Environment beyond default-construction defaults.
2. `MaterialsSlot` is not surfaced in the Geometry editor at all; today the
   Geometry editor only edits `GeometryComponent.Geometry`.
3. Edits flow through `*AppliedMessage` types and `HistoryKeeper` directly;
   `ISceneDocumentCommandService` does not yet expose component-edit operations.
4. Sync failures from message handlers log only; ED-M04 must route them to
   `OperationResult` with `FailureDomain.LiveSync`.
5. Multi-selection support exists for Transform via `MixedValues` /
   `*IsIndeterminate` flags. The same pattern must be extended to camera/light
   editors. Geometry/material editors must define explicit mixed-value
   semantics.
6. Drag/text-edit interactions commit per-keystroke in the current
   `TransformViewModel`. ED-M04 introduces a single coalesced undo entry per
   interaction.

## 5. Target Design

```text
field control (drag/text/menu)
  -> field VM (edit buffer + IsIndeterminate)
  -> EditSession (begin/commit/cancel)
  -> ISceneDocumentCommandService.<EditXxx>Async
        validates -> mutates -> records HistoryKeeper entry
        marks Scene dirty -> requests ISceneEngineSync.<UpdateXxx>Async
  -> SceneCommandResult { Succeeded, OperationResultId? }
  -> inspector binds OperationResult diagnostics to section/field
```

Invariants:

1. No inspector VM calls `ISceneEngineSync` directly. Sync is owned by the
   command service.
2. No inspector VM calls `Oxygen.Editor.Interop` directly.
3. Exactly one `HistoryKeeper` entry per committed user interaction (one drag,
   one Enter on a text field, one menu pick). Keystroke-by-keystroke buffering
   does not produce undo entries. Live-preview sync during an active edit is
   throttled by [live-engine-sync.md](./live-engine-sync.md); it is not the
   undo granularity.
4. Authoring state survives sync failure; sync failure is a non-blocking
   `LiveSync` warning attached to the section.
5. Mixed-selection display uses an explicit indeterminate state. Editing a
   field commits to all selected components; untouched fields retain their
   per-component value.
6. Locked components (`GameComponent.IsLocked == true`, e.g.,
   `TransformComponent`) hide remove/replace affordances.
7. Material assignment editing in ED-M04 is identity-only: the slot stores an
   `AssetReference<MaterialAsset>` URI; opening/creating/picking real material
   assets is ED-M05.

### UI Quality Bar

Match the current Transform/Geometry inspector exactly (see screenshot in this
LLD's referenced UI baseline):

- per-section `[icon] Title / one-line description` header + collapse chevron.
- vector control: label column, then `X [field] Y [field] Z [field]` with
  drag-edit, mouse-wheel step, and inline text edit. `MixedValues` shows `--`
  in indeterminate cells.
- asset reference control: `[thumbnail] [name] [v]` flyout menu populated from
  `IAssetCatalog`, with state badge for unresolved/missing.
- dense layout, no shifting on validation/mixed/expand.

Disclosure tiers per section:

- **Primary** — fields users edit during normal authoring.
- **Advanced** — uncommon authored fields (e.g., camera aspect-ratio policy,
  light angular size, override slot details).
- **Raw / Diagnostics** — read-only persisted authored values not yet wired to
  primary controls; shown so the inspector never silently hides authored data.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.WorldEditor` Inspector | View models, field controls, layout, validation surface, mixed-value display. |
| `Oxygen.Editor.WorldEditor` Documents/Commands | New `Component.*` operation kinds, dirty/undo, `OperationResult` publication. |
| `Oxygen.Editor.WorldEditor` Services | `ISceneEngineSync` adapter calls, runtime readiness checks. |
| `Oxygen.Editor.World` | Component data shape, defaults, `Hydrate/Dehydrate` round trip via `SceneJsonContext`. |
| `Oxygen.Editor.UI` | Reusable controls (vector edit, asset field) when extracted. |
| `Oxygen.Assets` | `AssetReference<T>`, `IAssetCatalog`, `AssetRecord` consumed by asset fields. |

## 7. Data Contracts

### 7.1 Component Editor Descriptor

Static metadata per component-editor type, registered with the inspector host:

| Field | Type | Notes |
| --- | --- | --- |
| `ComponentType` | `Type` | e.g., `typeof(TransformComponent)`. |
| `DisplayName` | `string` | Localized title. |
| `IconKey` | `string` | Resolved by `ComponentToGlyphConverter`. |
| `Description` | `string` | One-line description shown under the title. |
| `Group` | `enum { Primary, Scene }` | Scene-level editors (Environment) vs. node-level. |
| `SortOrder` | `int` | Render order; Transform is first. |
| `Selection` | `SelectionPolicy` | See 7.3. |
| `Fields` | `IReadOnlyList<FieldDescriptor>` | See 7.2. |
| `OperationKind` | `string` | One of `Component.Edit*`. |
| `SyncScope` | `enum` | Maps to a `SceneEngineSync` adapter call. |

### 7.2 Field Descriptor

| Field | Type | Notes |
| --- | --- | --- |
| `Key` | `string` | Stable key (e.g., `"FieldOfView"`). |
| `DisplayLabel` | `string` | Column label. |
| `Tier` | `enum { Primary, Advanced, Raw }` | Disclosure placement. |
| `ValueType` | `Type` | `float`, `Vector3`, `Quaternion`, `bool`, enum, `AssetReference<T>`. |
| `Unit` | `string?` | `"m"`, `"deg"`, `"lux"`, `null`. |
| `Range` | `(min, max)?` | Soft drag range. |
| `Validation` | `ValidationPolicy` | `Reject`, `Clamp(min,max)`, `Custom(delegate)`. |
| `MixedBehavior` | `enum { Indeterminate, FirstWins }` | Multi-selection. |
| `Sync` | `bool` | Whether commit triggers live sync. |
| `DiagnosticCode` | `string` | `"OXE.SCENE.<Component>.<Field>.Invalid"`. |

### 7.3 Selection Policy

| Policy | Description |
| --- | --- |
| `SingleOnly` | Editor visible iff `selection.Count == 1`. |
| `CommonComponent` | Editor visible iff every selected node has the component. |
| `OptionalCommon` | Editor visible always for the type; offers Add when missing. |
| `SceneOnly` | Editor visible regardless of node selection (Environment). |

Resolved against `ISceneSelectionService.GetSelectedNodes(documentId, scene)`.

### 7.4 V0.1 Component Editors — Field Tables

#### `TransformComponent` (`SelectionPolicy.CommonComponent`, locked, not removable)

| Field | Tier | Type / Range | Validation | Mixed | Sync |
| --- | --- | --- | --- | --- | --- |
| `LocalPosition` (X,Y,Z) | Primary | `Vector3`, no range | finite floats | Indeterminate per axis | yes |
| `LocalRotation` (X,Y,Z° Euler) | Primary | `Vector3` deg, displayed as Euler, stored as `Quaternion` | wrap to `[-180,180]` | Indeterminate per axis | yes |
| `LocalScale` (X,Y,Z) | Primary | `Vector3`, default `1`, no zero unless explicit | reject `0` axis | Indeterminate per axis | yes |

Rotation editing rule: VM converts Euler degrees ↔ `Quaternion`. Drag preserves
edited Euler component during the interaction; commit re-derives quaternion.

#### `GeometryComponent` (`SelectionPolicy.OptionalCommon`)

| Field | Tier | Type | Notes |
| --- | --- | --- | --- |
| `Geometry` | Primary | `AssetReference<GeometryAsset>` | Asset field, populated from `IAssetCatalog`; unresolved URI shown with warning badge. |
| Material slot 0 | Primary | `AssetReference<MaterialAsset>` (via `MaterialsSlot.Material`) | Identity only in ED-M04. Picker is a flat list from `IAssetCatalog` filtered to `MaterialAsset`. |
| Override slot summary | Advanced | counts of `RenderingSlot`, `LightingSlot`, `LevelOfDetailSlot` | Read-only count + "Open in scene authoring" stub. |
| Submesh / LOD count | Advanced | `int`, `int` | Read-only from resolved `GeometryAsset.Lods`. |
| `GeometryUri` raw | Raw | `string` | The persisted URI; copy-friendly diagnostic. |

ED-M04 closure does not include creating/picking material assets, only storing
identity. See ED-M04 ↔ ED-M05 seam in §7.6.

#### `PerspectiveCamera` (`SelectionPolicy.CommonComponent`)

| Field | Tier | Type / Range | Validation | Mixed | Sync |
| --- | --- | --- | --- | --- | --- |
| `FieldOfView` | Primary | `float` deg, `[1, 179]`, default `60` | Clamp | Indeterminate | yes |
| `NearPlane` | Primary | `float` m, `> 0`, default `0.1` | Reject `<= 0`; reject `>= FarPlane` | Indeterminate | yes |
| `FarPlane` | Primary | `float` m, `> NearPlane`, default `1000` | Reject `<= NearPlane` | Indeterminate | yes |
| `AspectRatio` | Advanced | `float`, default `16/9` | Reject `<= 0` | Indeterminate | yes |

Cross-field rule: editing `NearPlane >= FarPlane` rejects the commit; the field
shows the diagnostic from `OXE.SCENE.PerspectiveCamera.NearFar.Invalid`.

#### `DirectionalLightComponent` (`SelectionPolicy.CommonComponent`)

| Field | Tier | Type / Range | Validation | Mixed | Sync |
| --- | --- | --- | --- | --- | --- |
| `Color` | Primary | `Vector3` linear RGB `[0,1]^3` | Clamp | Indeterminate per channel | yes |
| `IntensityLux` | Primary | `float >= 0`, default `100_000` | Clamp `>= 0` | Indeterminate | yes |
| `IsSunLight` | Primary | `bool`, default `true` | exclusivity in scene: see Environment | Indeterminate (mixed→`false`) | yes |
| `EnvironmentContribution` | Primary | `bool`, default `true` | — | Indeterminate | yes |
| `CastsShadows` | Primary | `bool` | — | Indeterminate | yes |
| `AffectsWorld` | Advanced | `bool`, default `true` | — | Indeterminate | yes |
| `AngularSizeRadians` | Advanced | `float >= 0`, default `0.00935` | Clamp `>= 0` | Indeterminate | yes |
| `ExposureCompensation` | Advanced | `float`, EV stops `[-10, 10]` | Clamp | Indeterminate | yes |

Sun exclusivity rule: setting `IsSunLight = true` clears the flag on every
other `DirectionalLightComponent` in the scene as part of the same command
(single undo entry). See [environment-authoring.md](./environment-authoring.md).

#### Environment (scene-level, `SelectionPolicy.SceneOnly`)

Field set is owned by [environment-authoring.md](./environment-authoring.md).
The inspector reserves a "Scene → Environment" section that is reachable when
no node is selected and via a "Scene" entry in the breadcrumb.

#### Best-effort, non-gating editors

`OrthographicCamera`, `PointLightComponent`, `SpotLightComponent` keep their
existing add/remove affordances. ED-M04 does not block on production-quality
inspectors for them. If an editor is missing, the inspector shows a
"Editing not implemented in V0.1" raw block listing persisted fields.

### 7.5 Mixed-Value Semantics

- Numeric/scalar/enum/bool: `IsIndeterminate = true` when selected components
  disagree. The field shows `--` (or empty placeholder) and a tri-state for
  bools. Committing writes the typed value to all selected components.
- `Vector3` / `Quaternion`: per-component indeterminate (axis-wise).
- `AssetReference<T>`: indeterminate when URIs differ. The field shows
  "(multiple)"; committing replaces URI on every selected component.

### 7.6 Material Slot — ED-M04 ↔ ED-M05 Seam

ED-M04 owns:

- Surfacing the first `MaterialsSlot` (component-scope, index 0) inside the
  Geometry section as an asset reference field.
- Persisting `MaterialsSlotData.MaterialUri : string` through the
  `GeometryComponentData.OverrideSlots` round trip.
- Showing unresolved / placeholder / missing state in the field. Unresolved is
  a valid authoring state; the URI is preserved verbatim.
- The picker menu surface compatible with the ED-M05 Content Browser picker.
  In ED-M04 the menu lists `MaterialAsset` records returned by
  `IAssetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All))` filtered to
  asset type `MaterialAsset`; entries authored manually as `*.omat.json`
  (`oxygen.material.v1`) appear naturally.

ED-M04 does NOT own:

- Creating new material assets from the inspector.
- Editing material scalar properties anywhere in the inspector.
- Showing thumbnails generated from runtime preview.

If a user tries to clear the material slot, ED-M04 stores the empty/sentinel
URI defined by `MaterialsSlot` defaults. There is no inspector-side fallback
to a generated default.

### 7.6.1 Schema Decision

`MaterialsSlot` already persists a `MaterialUri` via `MaterialsSlotData`. The
slot does not embed material data. ED-M04 introduces no new editor-side schema:
the authored material identity is the persisted contract. Material descriptor
schema (`oxygen.material.v1`) remains the authoring source of truth and is
owned by ED-M05 / `Oxygen.Assets`. Decision: **no editor schema; reuse engine
descriptor**.

### 7.7 Metadata Visibility Rule

For every persisted field present in `*Data` records:

1. If ED-M04 wires a primary editor, the field appears in Primary or Advanced.
2. Otherwise, the inspector renders a Raw row showing the persisted value as
   read-only text.
3. Derived runtime state (resolved asset, computed AABB) is not editable; it
   appears under "Diagnostics" only when useful.
4. Stale references render as `[warn] <uri> — Missing` with a "Clear" action.

This prevents authored data from disappearing because no UI was wired.

## 8. Commands, Services, Adapters

### 8.1 Extensions to `ISceneDocumentCommandService`

New methods (all return `SceneCommandResult` or `SceneCommandResult<T>`):

```csharp
Task<SceneCommandResult> EditTransformAsync(
    SceneDocumentCommandContext ctx,
    IReadOnlyList<Guid> nodeIds,
    TransformEdit edit,
    EditSessionToken session);

Task<SceneCommandResult> EditGeometryAsync(
    SceneDocumentCommandContext ctx,
    IReadOnlyList<Guid> nodeIds,
    GeometryEdit edit,
    EditSessionToken session);

Task<SceneCommandResult> EditMaterialSlotAsync(
    SceneDocumentCommandContext ctx,
    IReadOnlyList<Guid> nodeIds,
    int slotIndex,
    Uri? newMaterialUri,
    EditSessionToken session);

Task<SceneCommandResult> EditPerspectiveCameraAsync(
    SceneDocumentCommandContext ctx,
    IReadOnlyList<Guid> nodeIds,
    PerspectiveCameraEdit edit,
    EditSessionToken session);

Task<SceneCommandResult> EditDirectionalLightAsync(
    SceneDocumentCommandContext ctx,
    IReadOnlyList<Guid> nodeIds,
    DirectionalLightEdit edit,
    EditSessionToken session);

Task<SceneCommandResult<GameComponent>> AddComponentAsync(
    SceneDocumentCommandContext ctx,
    Guid nodeId,
    Type componentType);

Task<SceneCommandResult> RemoveComponentAsync(
    SceneDocumentCommandContext ctx,
    Guid nodeId,
    Guid componentId);

Task<SceneCommandResult> EditSceneEnvironmentAsync(
    SceneDocumentCommandContext ctx,
    SceneEnvironmentEdit edit,
    EditSessionToken session);
```

`*Edit` records are partial-write structures: every property is `Optional<T>`
(or nullable) so the inspector can commit a single field without overwriting
others. Cross-field validation (camera near/far, sun exclusivity) is applied
by the command before mutation.

### 8.2 Operation Kinds

Add to `SceneOperationKinds`:

| Constant | String |
| --- | --- |
| `EditTransform` | `"Scene.Component.EditTransform"` |
| `EditGeometry` | `"Scene.Component.EditGeometry"` |
| `EditMaterialSlot` | `"Scene.Component.EditMaterialSlot"` |
| `EditPerspectiveCamera` | `"Scene.Component.EditCamera"` |
| `EditDirectionalLight` | `"Scene.Component.EditLight"` |
| `AddComponent` | `"Scene.Component.Add"` |
| `RemoveComponent` | `"Scene.Component.Remove"` |
| `EditEnvironment` | `"Scene.Environment.Edit"` |

### 8.3 EditSession Coalescing

`EditSessionToken` groups multiple sub-commits into one undo step:

- Drag begin → `EditSessionToken.Begin(operationKind, nodeIds, fieldKey)`.
- Each intermediate sample → `command.Edit*Async(..., session)` updates the
  current authoring value; the command service treats samples after `Begin`
  as updates to a single in-progress `HistoryKeeper` entry.
- Drag end → `session.Commit()` finalizes the entry and triggers the terminal
  `LiveSync` request with the final value. Any intermediate preview sync calls
  are owned by the live-sync coalescer and do not create undo entries or
  separate operation results.
- Cancel (Esc) → `session.Cancel()` reverts to the pre-Begin value via the
  in-progress entry.

Text fields use the same token scoped from focus-in to Enter / focus-out.
Menu picks use a one-shot `EditSessionToken.OneShot`. Mouse-wheel ticks
coalesce inside a small idle window (e.g., 250 ms).

### 8.4 Add/Remove Cardinality

Enforced by command service against `SceneNode.Components`:

- At most one `TransformComponent` per node, locked, never removable.
- At most one `CameraComponent` (any subclass) per node.
- At most one `LightComponent` (any subclass) per node.
- `GeometryComponent`: at most one per node in V0.1.
- Removal of a non-existent or locked component → `SceneCommandResult` with
  `OperationResult` (`SceneAuthoring`, code `OXE.SCENE.COMPONENT.RemoveDenied`).

## 9. UI Surfaces

```text
Inspector pane
+--------------------------------------------------------------+
| [glyph] <Node Name>                       [Add v]   [delete] |
| TransformComponent  GeometryComponent  PerspectiveCamera     |
+--------------------------------------------------------------+
| [icon] Transform                                  [^/v]      |
|        Defines local position, rotation, and scale.          |
+--------------------------------------------------------------+
| Position    X [ -1.0 ]  Y [  0.0 ]  Z [  8.0 ]               |
| Rotation°   X [  0.0 ]  Y [  0.0 ]  Z [  0.0 ]               |
| Scale       X [  9.0 ]  Y [  9.0 ]  Z [  7.0 ]               |
+--------------------------------------------------------------+
| [icon] Geometry                                   [^/v]      |
|        Renderable mesh and material assignment.              |
+--------------------------------------------------------------+
| Asset       [thumb] Cube                           [v]       |
| Material    [swatch] —                             [v]       |
|             ! Awaiting material picker (ED-M05).             |
| > Advanced   submeshes 1, lods 1, override slots 0           |
| > Raw        GeometryUri = asset:///Engine/Geometry/Cube     |
+--------------------------------------------------------------+
| [icon] Perspective Camera                         [^/v]      |
|        Projection used by viewport when active.              |
+--------------------------------------------------------------+
| FOV°         [ 60.0 ]                                        |
| Near         [ 0.10 ] m   Far  [ 1000.0 ] m                  |
| > Advanced   Aspect [ 1.778 ]                                |
+--------------------------------------------------------------+
| [icon] Directional Light                          [^/v]      |
|        Sun-style light, environment contribution.            |
+--------------------------------------------------------------+
| Color        [swatch] 1.00 0.95 0.82                         |
| Intensity    [ 100000 ] lux                                  |
| Sun          [x]    Env. contribution [x]   Shadows [x]      |
| > Advanced   Affects world [x]   Angular size [ 0.00935 ]   |
|              Exposure comp [ 0.0 ] EV                        |
+--------------------------------------------------------------+
| ! Live preview unavailable: engine not running.   [details]  |
+--------------------------------------------------------------+
```

UI rules:

- The header strip is rendered by `SceneNodeDetailsViewModel`. Component
  add menu lists only types not already present (cardinality respecting).
- Locked components show no remove glyph. `TransformComponent` is locked.
- Mixed-value rows render `--` per-axis.
- Validation errors render inline under the field with the diagnostic code's
  short message, focusable for keyboard nav.
- Section-level `LiveSync` warnings render at the top of the section, do not
  obscure fields, and do not block editing.
- Empty-selection state shows "Scene" with the Environment section visible.

## 10. Persistence And Round Trip

All ED-M04 edits round-trip through existing `*Data` DTOs in
`Oxygen.Editor.World/src/Serialization/` via `SceneJsonContext`:

| Edit | DTO Field |
| --- | --- |
| Transform | `TransformData.Position/Rotation/Scale` |
| Geometry asset | `GeometryComponentData.GeometryUri` |
| Material slot | `MaterialsSlotData.MaterialUri` (under `GeometryComponentData.OverrideSlots`) |
| Perspective camera | `PerspectiveCameraData.{FieldOfView, AspectRatio, NearPlane, FarPlane}` |
| Directional light | `DirectionalLightData.{Color, IntensityLux, IsSunLight, EnvironmentContribution, CastsShadows, AffectsWorld, AngularSizeRadians, ExposureCompensation}` |
| Environment | scene-level fields owned by [environment-authoring.md](./environment-authoring.md) |

Round-trip rules:

1. After save+reopen, every edited field equals its in-memory value
   bit-for-bit (within float ULP for `float`/`Vector3`/`Quaternion`).
2. Unresolved `MaterialUri` / `GeometryUri` strings are preserved verbatim.
3. Add/remove operations remove the component DTO entry. Cardinality is
   enforced by the command service for ED-M04-authored changes. Current hydrate
   behavior guarantees exactly one `TransformComponent`; duplicate camera,
   light, or geometry components in pre-existing malformed scene JSON are a
   validation/repair concern, not something the inspector silently fixes.
4. Undo→Redo→Save→Reopen produces the same JSON bytes (ignoring
   formatting-stable serialization) as the equivalent direct edit.

## 11. Live Sync / Cook / Runtime Behavior

Mapping (consumed by [live-engine-sync.md](./live-engine-sync.md)):

| Operation | Sync call | If runtime not `Running` | If unsupported |
| --- | --- | --- | --- |
| `EditTransform` | `UpdateNodeTransformAsync` | `SkippedNotRunning` | n/a |
| `EditGeometry` | `Attach/DetachGeometryAsync` | `SkippedNotRunning` | n/a |
| `EditMaterialSlot` | none in V0.1 | n/a | `Unsupported` warning |
| `EditPerspectiveCamera` | `AttachCameraAsync` (re-apply) | `SkippedNotRunning` | n/a |
| `EditDirectionalLight` | `AttachLightAsync` (re-apply) | `SkippedNotRunning` | n/a |
| `AddComponent` | matching attach | `SkippedNotRunning` | `Unsupported` for unmapped types |
| `RemoveComponent` | matching detach | `SkippedNotRunning` | `Unsupported` for unmapped types |
| `EditEnvironment` | env adapter (see env LLD) | `SkippedNotRunning` | `Unsupported` per field |

Cook behavior is out of ED-M04. Inspector edits must produce persisted
authoring state that ED-M07 can cook unchanged.

## 12. Operation Results And Diagnostics

`SceneCommandResult.OperationResultId` is set whenever the command produces a
warning/failure. The inspector resolves the `OperationResult` through the
existing diagnostics service and renders:

- field-level: `OperationStatus.Failed` + `FailureDomain.SceneAuthoring` →
  inline error under the field.
- section-level: `SucceededWithWarnings` + `FailureDomain.LiveSync` →
  warning bar above section fields.
- pane-level: `Failed` for save/reopen → existing document operation result UI.

Failure-domain mapping:

| Cause | Domain | Code prefix |
| --- | --- | --- |
| Invalid scalar / out-of-range | `SceneAuthoring` | `OXE.SCENE.<Component>.<Field>.Invalid` |
| Cross-field constraint (near≥far) | `SceneAuthoring` | `OXE.SCENE.<Component>.<Constraint>` |
| Cardinality (add when present) | `SceneAuthoring` | `OXE.SCENE.COMPONENT.AddDenied` / `RemoveDenied` |
| Unresolved `Material`/`Geometry` URI | `AssetIdentity` | `OXE.ASSETID.UnresolvedReference` |
| Sync rejected / unsupported | `LiveSync` | `OXE.LIVESYNC.*` |
| Runtime not running | `LiveSync` (with `RuntimeView` detail) | `OXE.LIVESYNC.NotRunning` |
| Save failure | `Document` | `OXE.DOCUMENT.SaveFailed` |

`AffectedScope` is filled with `SceneId`, `NodeId`, `NodeName`, `ComponentType`,
`ComponentName`, and (for material slot) `AssetVirtualPath = MaterialUri`.

## 13. Dependency Rules

Allowed:

- `Inspector.*` → `Documents.Commands.ISceneDocumentCommandService`,
  `Documents.Selection.ISceneSelectionService`, `Oxygen.Editor.World`,
  `Oxygen.Editor.UI`, `Oxygen.Assets` (catalog/identity only),
  `Oxygen.Core.Diagnostics`.
- `Documents.Commands.*` → `ISceneEngineSync`.

Forbidden:

- `Inspector.*` must not reference `ISceneEngineSync`, `Oxygen.Editor.Runtime`,
  `Oxygen.Editor.Interop`, `OxygenWorld`, or `EngineService`.
- `Inspector.*` must not call `IAssetCatalog` mutation methods (read-only).
- Inspector must not write JSON or call `Scene.Dehydrate` directly.
- The Geometry editor must not embed any material editing UI beyond identity.

## 14. Validation Gates

ED-M04 inspector closure requires:

1. For each of Transform, Geometry, PerspectiveCamera, DirectionalLight,
   Environment, and the Geometry material slot: a primary edit, an advanced
   edit (when applicable), undo+redo, save+reopen, all return identical
   in-memory values.
2. Drag a position axis 100 samples → exactly 1 `HistoryKeeper` entry; preview
   sync follows the live-sync throttle contract and commit produces one
   terminal sync result.
3. Multi-selection of two nodes with different `LocalPosition.X` shows `--`;
   committing writes the typed value to both, with one shared undo entry.
4. Editing `NearPlane` to `>= FarPlane` rejects with inline error and does not
   mutate authoring state.
5. Setting `IsSunLight = true` on light A clears it on lights B, C in the
   same scene as a single undoable command.
6. `RemoveComponent` against `TransformComponent` is denied with
   `OXE.SCENE.COMPONENT.RemoveDenied`.
7. Setting `MaterialUri` to a URI not present in `IAssetCatalog` persists the
   URI; UI shows missing badge; no exception is thrown; no live sync attempt.
8. With engine not `Running`, every primary edit succeeds in authoring and
   surfaces a `LiveSync` `SkippedNotRunning` warning at the section.
9. A material-slot edit produces no exception even if `EditMaterialSlot` sync
   is `Unsupported`; authoring value persists; warning appears.
10. Inspector references no `Oxygen.Editor.Runtime` / `Interop` types
    (verified via project references / static check).

## 15. Open Issues

- Should the component-editor descriptor registry move to a shared static
  registry (replacing `AllPropertyEditorFactories`) or remain composed inside
  `SceneNodeEditorViewModel` for ED-M04? Default: keep inside the host;
  re-evaluate when ED-M05/ED-M06 add editors.
- Where to render the Scene/Environment section when a node is selected: an
  always-visible "Scene" tab in the inspector header vs. a breadcrumb-driven
  switch. Default: breadcrumb switch + dedicated "Edit Scene" affordance in
  the empty-selection state.
- Mouse-wheel coalescing window (default 250 ms) — confirm during
  implementation against actual UX feel.
