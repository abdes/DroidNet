# Property Inspector LLD

Status: `V0.1 contract; named gaps execute in ED-M07A`

## 1. Purpose

Concrete editor design for the V0.1 property inspector. ED-M07A must turn the
current `SceneNodeEditorViewModel` / `TransformViewModel` / `GeometryViewModel`
baseline into a command-backed, undoable, dirty-aware, multi-selection-aware
inspector for the V0.1 component set: `TransformComponent`,
`GeometryComponent` (with `MaterialsSlot` reference only), `PerspectiveCamera`,
`DirectionalLightComponent`, and scene-level settings sections.

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

The committed source has Transform, Geometry/material slot, PerspectiveCamera,
DirectionalLight and Environment views, a scene-level empty-selection host,
schema-property command entry points, and mixed-value bindings. The concrete
remaining omissions are background dispatch/results, one-shot gesture wiring,
rejected-value field feedback, lifetime/revision-aware replay, and complete UI/
native evidence, as identified in ED-M07A's source-backed gap table. These are
bounded fixes; an inspector rewrite or another baseline audit is not required.

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
7. Material assignment stores an `AssetReference<MaterialAsset>` URI and
   consumes the existing real material picker delivered by ED-M05/06. ED-M07A
   fixes gesture/result/native behavior, not a replacement picker.

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
| `Oxygen.Managed.Assets` | `AssetReference<T>`, `IAssetCatalog`, `AssetRecord` consumed by asset fields. |

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
| `SceneOnly` | Scene-level editor visible only when the scene/no-node selection is active. |

Resolved against `ISceneSelectionService.GetSelectedNodes(documentId, scene)`.

### 7.4 V0.1 Component Editors — Field Tables

#### Node visibility and geometry shadows — approved ED-M08 contract

Approved 2026-09-15; **implementation and rendered qualification pending**.
The [visibility review](../review/ED-M08-node-light-visibility-review.md)
defines the accepted behavior. Its Sun selector row and the associated
single-sun, secondary/moon and sky-only boundaries remain open; this approval
does not settle those roles.

| Control | Presentation and source value | Required effect |
| --- | --- | --- |
| Scene Visibility | Primary: `Inherit / Shown / Hidden` | Resolves the node's native Local/Inherit flag. New roots default Shown; new children default Inherit. Effective Hidden removes that node's geometry, shadow casting and light contribution. A locally Shown child can override a hidden parent. |
| Geometry: Cast Shadows | `Inherit / On / Off`, independent node flag | New roots resolve On; children inherit unless explicitly overridden. Off removes geometry as an occluder while preserving its visible surface, lighting and receiving. Supported opaque/masked casters qualify; blended shadow casting is excluded. |
| Geometry: Receive Shadows | Advanced: `Inherit / On / Off`, independent node flag | New roots resolve On; children inherit unless explicitly overridden. Off skips direct-light shadow attenuation on the surface without changing its visibility, lighting or casting. It does not disable ambient occlusion or select Unlit. A real GPU consumer is required. |

These are source modes, not three independent booleans or an ancestor-AND
activation rule. Display the resolved value and its inherited origin; do not
replace a stored Inherit value with the parent's current boolean. Multi-selection
shows mixed source modes separately from differing resolved values. An edit or
reparent resolves affected descendants coherently through the normal command,
Undo/Redo and live-sync contracts. Geometry and lights use the same resolved
visibility; light traversal must not add a separate hidden-ancestor gate.

The **Hide in editor** eye is separate from these authored controls. It masks
geometry/gizmo representations and their descendants in the editing main view,
while retaining light contribution and shadow-caster eligibility. Store its
choices in per-user/project workspace state outside authored content. It never
dirties a scene or schedules cooking. Showing a parent retains individual child
hide choices; Show All clears view overrides. It is not a light on/off switch.

Cameras remain selectable and usable when their node is Hidden or its editor
representation is hidden. Runtime-loaded status (`IsActive`) is derived, not a
saved activation control. No generic node/simulation enablement, authored
Shadows Only or Hidden Shadow mode is added. Camera-frustum exclusion can still
retain a visible caster for shadows; authored Hidden cannot.

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
| Every existing material slot | Primary | Per-slot material asset reference or no instance override | Independent assignment/clearing through the existing material picker; clearing uses the mesh-assigned material. ED-M08 scope approved 2026-09-15; implementation pending. |
| Cast Shadows | Primary | `Inherit / On / Off` node flag | Approved ED-M08 behavior above; independent of light Cast Shadows and material sidedness. Implementation pending. |
| Receive Shadows | Advanced | `Inherit / On / Off` node flag | Approved ED-M08 receiver effect above; CPU state alone does not satisfy this control. Implementation pending. |
| Override slot summary | Advanced | counts of `RenderingSlot`, `LightingSlot`, `LevelOfDetailSlot` | Read-only count; do not expose an unimplemented action. |
| Submesh / LOD count | Advanced | `int`, `int` | Read-only from resolved `GeometryAsset.Lods`. |
| `GeometryUri` raw | Raw | `string` | The persisted URI; copy-friendly diagnostic. |

ED-M05 already owns real material creation/picking. ED-M07A fixes and proves
consumption of that picker and the runtime override path; it does not restore
the earlier raw-identity-only milestone limitation.

ED-M08's approved creation palette is Cube, Sphere, Capsule, Cylinder, Cone,
Plane, Quad, IcoSphere and Torus, with SubdividedCube under Advanced. Discover
canonical authoring choices from the native catalog; the editor must not
maintain its own generator/default list. ArrowGizmo is an internal tool resource.
Migrate useful GeodesicSphere identities to IcoSphere and remove the alias.
All ten creation/assignment paths require cooking and native/editor rendering;
Capsule implementation and this broader qualification remain pending.

Approved primitive defaults (2026-09-15): all use metres and centred pivots in
Oxygen's Z-up space. Cube/SubdividedCube have 1 m edges; Sphere/IcoSphere have
1 m diameter; Cylinder/Cone have 1 m height and diameter along Z (cone tip +Z);
Capsule has 2 m total height and 1 m diameter along Z; Torus lies in XY with
1 m outer and 0.2 m tube diameter. Plane is a 1 × 1 m XY ground surface facing
+Z; Quad is a 1 × 1 m upright XZ card facing −Y. Native recipes own these
defaults; no editor-only corrective rotation or scale is permitted.

Quad/Torus changes and Capsule generation remain implementation work. Source
and API prose must describe actual implemented axes, dimensions, parameters
and return types, and update together with geometry. A design table is not
evidence that the current buffers satisfy it.

#### `PerspectiveCamera` (`SelectionPolicy.CommonComponent`)

| Field | Tier | Type / Range | Validation | Mixed | Sync |
| --- | --- | --- | --- | --- | --- |
| Vertical Field of View (`FieldOfView`) | Primary | `float` deg, `[1, 179]`, default `60` | Clamp | Indeterminate | yes |
| `NearPlane` | Primary | `float` m, `> 0`, default `0.1` | Reject `<= 0`; reject `>= FarPlane` | Indeterminate | yes |
| `FarPlane` | Primary | `float` m, `> NearPlane`, default `1000` | Reject `<= NearPlane` | Indeterminate | yes |
| Aspect Mode | Primary | `Auto` / `Fixed`, new-camera default `Auto` | Reject unsupported modes | Indeterminate | yes |
| Fixed Aspect Ratio | Primary, Fixed mode only | finite positive width/height ratio; initial Fixed selection `16/9` | Reject non-positive/non-finite values | Indeterminate | yes |

Cross-field rule: editing `NearPlane >= FarPlane` rejects the commit; the field
shows the diagnostic from `OXE.SCENE.PerspectiveCamera.NearFar.Invalid`.

The user approved this basic perspective model for V0.1 on 2026-09-15, with pose
provided by Transform. Do not add focal-length/sensor or aperture/shutter/ISO
authoring; physical-camera authoring is deferred. The user subsequently approved
Auto/Fixed aspect fitting with Auto as the new-camera default. Auto derives the
current target's ratio per view and keeps vertical FOV unchanged. Fixed preserves
the authored ratio and full composition in a centred image rectangle with
letterbox/pillarbox bars, without cropping or stretching. Resize never changes
saved camera values or dirty/history state. Editor navigation remains independent.

The effective Auto ratio is derived/read-only information; do not offer an
editable ratio that runtime ignores. Native view composition owns fitting and
adds bars after scene exposure/post-processing so they do not affect metering.
Map explicit glTF camera ratios to Fixed and omitted ratios to Auto. Migrate
useful explicit ratios in prior Oxygen documents to Fixed using development
tooling; the shipping path consumes only the canonical policy.

ED-M08 must load the explicitly chosen authored camera, including parented pose
and non-default projection, through real native hydration. Do not select the
first camera instead, silently normalize invalid clipping or substitute editor
navigation. Carry all fields through commands, Undo/Redo, Save/reopen, cooking
and observed native/editor rendering. These expanded loading/qualification gates
remain pending; the approval is a scope decision, not evidence of implementation.
Include wide/tall Auto targets, Fixed 4:3/16:9, mode changes and resize in the
engine-first/editor-second qualification. Record both authored policy and actual
view projection/content rectangle in development-only evidence.

#### `DirectionalLightComponent` (`SelectionPolicy.CommonComponent`)

| Field | Tier | Type / Range | Validation | Mixed | Sync |
| --- | --- | --- | --- | --- | --- |
| `Color` | Primary | `Vector3` linear RGB `[0,1]^3` | Clamp | Indeterminate per channel | yes |
| `IntensityLux` | Primary | `float >= 0`, default `100_000` | Clamp `>= 0` | Indeterminate | yes |
| `IsSunLight` | Primary | `bool`, default `true` | exclusivity in scene: see Environment | Indeterminate (mixed→`false`) | yes |
| `EnvironmentContribution` | Primary | `bool`, default `true` | — | Indeterminate | yes |
| Light: Cast Shadows (`CastsShadows`) | Primary | `bool` | Off preserves illumination and other geometry/light shadow settings | Indeterminate | yes |
| Affects Scene (`AffectsWorld`) | Primary | `bool`, default `true` | Runtime light participation, separately gated by effective Scene Visibility | Indeterminate | yes |
| `AngularSizeRadians` | Advanced | `float >= 0`, default `0.00935` | Clamp `>= 0` | Indeterminate | yes |
| `ExposureCompensation` | Advanced | `float`, EV stops `[-10, 10]` | Clamp | Indeterminate | yes |

The approved **Affects Scene** control maps the existing `affects_world`
participation meaning. Off stops this light's illumination and atmospheric
contribution while retaining its stored intensity, colour, visibility and sun
assignment. On does not override effective Hidden; show a derived visibility
explanation instead of promising illumination. Light Cast Shadows is independent
of the geometry Cast/Receive Shadows controls and must work for every light
offered by V0.1. These behavior changes remain implementation work.

The `IsSunLight`/`EnvironmentContribution` rows and following exclusivity rule
describe the existing authoring baseline. The replacement sun-selector/role
contract is pending a separate decision; neither a single-source selector nor
secondary/moon or sky-only exclusions are approved here.

Baseline sun exclusivity rule: setting `IsSunLight = true` clears the flag on every
other `DirectionalLightComponent`, enables `EnvironmentContribution` on the
selected light and binds it as the scene sun. Disabling `EnvironmentContribution`
also disables `IsSunLight` and clears the scene binding when it points to that
light. Each action is one undo entry, including all related values.
See [environment-authoring.md](./environment-authoring.md).

#### Scene Settings (scene-level, `SelectionPolicy.SceneOnly`)

Field set is owned by [environment-authoring.md](./environment-authoring.md).
The inspector shows scene-level sections only when the scene itself is selected
/ no node is selected. They are not shown while any scene node is selected.
The scene panel is top-aligned and uses the same DroidNet property-section
controls as Transform and Geometry: collapsible property sections, `NumberBox`
for scalar fields, `VectorBox` for fixed RGB/vector fields, and compact
selectors/toggles for enum/bool fields.

The sections are split by domain:

- `Sky Atmosphere`: atmosphere enable, sun disk, planet/atmosphere lengths,
  scattering, luminance, and aerial perspective values.
- `Sun Binding`: directional-light node binding and stale-reference warning.
- `Exposure`: exposure enabled, exposure mode, exposure key, manual EV,
  compensation EV, and auto-exposure range/speeds/metering/histogram/window/
  target/spot radius. Manual EV is visible only for `Manual` /
  `ManualCamera`; auto-exposure rows are visible only for `Auto`.
- `Tone Mapping`: tone mapper and display gamma. Display gamma is hidden when
  the mapper is `None`.
- `Bloom`: bloom intensity and threshold.
- `Color Grading`: saturation, contrast, and vignette. Hidden when the mapper
  is `None`.
- `Background`: fallback scene background color.

Rows edit one primary property each. Dense multi-property rows are avoided
except for fixed vector controls where DroidNet `VectorBox` intentionally owns
the grouped axis layout.

#### Best-effort, non-gating editors

`OrthographicCamera`, `PointLightComponent`, `SpotLightComponent` keep their
existing add/remove affordances. ED-M07A does not block on production-quality
inspectors for them; they are outside the PRD-qualified authoring/import set. If an editor is missing, the inspector shows a
"Editing not implemented in V0.1" raw block listing persisted fields.

### 7.5 Mixed-Value Semantics

- Numeric/scalar/enum/bool: `IsIndeterminate = true` when selected components
  disagree. The field shows `--` (or empty placeholder) and a tri-state for
  bools. Committing writes the typed value to all selected components.
- `Vector3` / `Quaternion`: per-component indeterminate (axis-wise).
- `AssetReference<T>`: indeterminate when URIs differ. The field shows
  "(multiple)"; committing replaces URI on every selected component.

### 7.6 All Existing Material Slots — ED-M08 Contract

On 2026-09-15 the user approved independent overrides for **all existing mesh
material slots**, superseding the slot-0 scope. ED-M07A's recorded evidence stays
historical; it does not establish this broader implementation.

- Enumerate the slots supplied by the resolved mesh and expose each as a material
  identity field. Do not create/remove mesh slots or change topology.
- Store overrides on the scene instance. Assignment to one instance must not
  rewrite its shared mesh, material assets or another instance's overrides.
- Clearing removes that slot's instance override and restores the mesh-assigned
  material. Do not substitute an engine default merely to implement Clear.
- Reuse `IMaterialPickerService` and shared catalog state, including unresolved
  identity and owning material create/open workflows. Geometry does not embed
  scalar-material editing.
- Give every slot the complete command, Undo/Redo, Save/reopen, cook/native-load,
  live binding and missing-material recovery path. Nonzero slots are required
  rendered cases, not read-only metadata.
- Specify stable slot identity and reimport matching before implementation;
  changing source slots must not silently send overrides to different surfaces.

Migrate useful prior single-slot assignments to the canonical per-slot model.
Do not retain a second slot-0 serialization or runtime compatibility path.

### 7.6.1 Schema Decision

Overrides reference material identities rather than embedding material data.
ED-M08 must reconcile canonical scene serialization and the native scene
descriptor with the approved all-slot contract before coding; the old one-ref
descriptor is insufficient. Reuse the engine schema/loader ownership and extend
it where needed. Material asset serialization remains with its material owner.
Exact slot identity/reimport rules are still to be decided; the all-slot scope
does not by itself select index-based or name-based remapping.

### 7.7 Metadata Visibility Rule

For every persisted field present in `*Data` records:

1. If ED-M07A wires a primary editor, the field appears in Primary or Advanced.
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
- Empty-selection state shows "Scene" with the scene-level Sky Atmosphere, Sun
  Binding, Exposure, Tone Mapping, Bloom, Color Grading, and Background
  sections visible.

### 9.1 ED-M07B Compact Layout And Component Filtering

ED-M07B.5g owns the user-reported component-list sizing and filtering defects.
The previous inspector reserved a 2*:3* split between the header/component area
and properties, including empty space in multi-node mode. Component selection
controlled deletion but did not filter property sections. The compact selector
replaces that behavior under the following contract.

The node header and component selector must size to their content. The property
editors receive the remaining height. Show up to four compact component rows
without a separate scroll area; longer lists get a bounded scroll/overflow that
keeps properties usable. Small/short docks may reduce the visible selector rows.
Remove the default proportional split and its need for manual correction. An
existing splitter position must not recreate empty reserved space. Multi-node
selection gets a compact count/type summary and applicable component filters,
without a blank single-node-list placeholder.

Use these selection semantics for both single-node and multi-node inspection:

| Interaction | Visible editors |
| --- | --- |
| No component selected / All components | All editors applicable to the current scene-node selection, under section 7.3. |
| Select a component/type | Only that component's applicable editor section. Selecting the same entry again clears the filter. |
| Choose All components | Clear component selection and restore all applicable sections; the action is keyboard reachable. |
| Change selected nodes, scene, or document | Reset to All components for the new selection; no stale component instance crosses the selection boundary. |
| Add/remove a component in the current node selection | Recompute availability. Preserve a still-applicable filter; if its component was removed, return to All components. |
| Select the scene / clear all scene-node selection | Show scene-level environment settings under the existing contract; no leftover node component filter or reserved header space. |

Multi-node filtering operates on component types, never an arbitrary component
instance from the first node. It must respect existing editor applicability and
mixed-value rules. A type unavailable for the selected set has an explanation;
filtering cannot implicitly add components, change the edit target set, or enable
bulk remove/add actions. Preserve the existing single-node cardinality and locked
Transform rules. All components is a view choice, never a deletion target.

The host owns the active filter and projects applicable editor instances into
visible sections. Switching filters must use the normal edit-session boundary
for a focused field, preserving M07A gesture ownership, Escape cancellation,
validation, and accepted history. Do not recreate document models, clear values,
or lose errors because an editor becomes hidden. Keep errors discoverable from
the affected component entry. A filter change itself does not dirty the document,
create undo history, synchronize values, or request a cook.

Complete valid pending numeric text before changing sections; cancel unfinished
drags. Invalid text keeps its field feedback after the section becomes hidden.
An old color picker's completion cannot end a newer editing lifetime. Source
corrections clear affected field errors, including in mixed selections, while
retaining unrelated errors.

Use friendly type labels such as Transform, Geometry, Camera, and Directional
Light. Component rows use fixed type names, replacing component-instance renaming
in this selector (accepted 2026-09-13). All components uses an icon-only compact
toolbar toggle with the tooltip "Show all component properties". It precedes a
thin separator and the single-node Add/Remove actions; multi-selection shows the
All toggle without a separator or unavailable editing actions.
Keep the node summary, component actions, and filter compact. Property editors
retain their existing side-by-side label/value layout, spacing and inline
descriptions. Clipping at constrained widths is allowed; tooltips expose the
full text. Do not reflow property names onto separate rows or add description
flyouts. Preserve keyboard target sizes, focus visibility and existing theme tokens.

Validation covers single-node and multi-node selections with two/four components,
common and non-common types, All -> Geometry -> Transform -> All, deselection,
component removal, Undo/Redo, scene switching and no-node selection. Exercise
filter changes during numeric/text/color edits and pending native work. Measure
header/list desired height versus rendered height: extra inspector height must
go to properties, not empty component-list space. Qualify narrow/short docks and
100%/150%/200% scaling with the real packaged controls and visible walkthroughs.

## 10. Persistence And Round Trip

All ED-M07A edits round-trip through existing `*Data` DTOs in
`Oxygen.Editor.World/src/Serialization/` via `SceneJsonContext`:

| Edit | DTO Field |
| --- | --- |
| Transform | `TransformData.Position/Rotation/Scale` |
| Geometry asset | `GeometryComponentData.GeometryUri` |
| Material slot | `MaterialsSlotData.MaterialUri` (under `GeometryComponentData.OverrideSlots`) |
| Perspective camera | `PerspectiveCameraData.{FieldOfView, AspectRatio, NearPlane, FarPlane}` |
| Directional light | `DirectionalLightData.{Color, IntensityLux, IsSunLight, EnvironmentContribution, CastsShadows, AffectsWorld, AngularSizeRadians, ExposureCompensation}` |
| Environment | scene-level fields owned by [environment-authoring.md](./environment-authoring.md) |

The table above records existing ED-M07A DTO coverage. ED-M08 adds canonical
Local/Inherit source modes for Scene Visibility and geometry Cast/Receive
Shadows; the current boolean DTOs/cooked records do not yet preserve those
modes. Apply the one-time useful-content migration in
[scene-authoring-model.md](./scene-authoring-model.md#ed-m08-visibility-and-shadow-source-state).
Editor-only hide choices and runtime-loaded state are excluded from authored
scene serialization and cooking. Do not retain a legacy runtime reader branch.

Round-trip rules:

1. After save+reopen, every edited field equals its in-memory value
   bit-for-bit (within float ULP for `float`/`Vector3`/`Quaternion`).
2. Unresolved `MaterialUri` / `GeometryUri` strings are preserved verbatim.
3. Add/remove operations remove the component DTO entry. Cardinality is
   enforced by the command service for ED-M07A-authored changes. Current hydrate
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
| `EditMaterialSlot` | `UpdateMaterialSlotAsync` | `SkippedNotRunning` | n/a |
| `EditPerspectiveCamera` | `AttachCameraAsync` (re-apply) | `SkippedNotRunning` | n/a |
| `EditDirectionalLight` | `AttachLightAsync` (re-apply) | `SkippedNotRunning` | n/a |
| `AddComponent` | matching attach | `SkippedNotRunning` | `Unsupported` for unmapped types |
| `RemoveComponent` | matching detach | `SkippedNotRunning` | `Unsupported` for unmapped types |
| `EditEnvironment` | scene settings adapter (see env LLD) | `SkippedNotRunning` | `Unsupported` per field without native API |

Cook behavior is out of ED-M07A. Inspector edits must produce persisted
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
  `Oxygen.Editor.UI`, `Oxygen.Managed.Assets` (catalog/identity only),
  `Oxygen.Managed.Core.Diagnostics`.
- `Documents.Commands.*` → `ISceneEngineSync`.

Forbidden:

- `Inspector.*` must not reference `ISceneEngineSync`, `Oxygen.Editor.Runtime`,
  `Oxygen.Editor.Interop`, `OxygenWorld`, or `EngineService`.
- `Inspector.*` must not call `IAssetCatalog` mutation methods (read-only).
- Inspector must not write JSON or call `Scene.Dehydrate` directly.
- The Geometry editor must not embed any material editing UI beyond identity.

## 14. Validation Gates

Approved ED-M08 visibility/shadow gates, all **pending**:

- Local/Inherit source modes and resolved values survive edit, reparent,
  Undo/Redo, Save/reopen, cook and native/editor loading. New-root and child
  defaults match the approved table; a locally Shown child under Hidden remains
  eligible for geometry/light contribution.
- Affects Scene, geometry casting, light shadowing and receiver opt-out have
  independently verified rendered effects. Receiver Off preserves direct light,
  ambient occlusion and the surface's own casting.
- Editor-only Hide changes only main-view representations, retains illumination
  and caster eligibility, preserves child choices and never changes authored
  hashes, dirty/history state or cooking demand.
- Hidden camera nodes remain usable. Authored Hidden, off-screen casters and
  editor-only Hide have their distinct shadow behavior. Blended shadow casting
  and authored hidden-shadow/shadows-only modes are not exposed.
- Effective flag/hierarchy changes invalidate directional-light selection and
  affected captured lighting products; stale cached membership cannot pass.

ED-M07A inspector closure requires:

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
   URI; UI shows missing badge; no exception is thrown; live sync attempts a
   best-effort override when the engine is running.
8. With engine not `Running`, every primary edit succeeds in authoring and
   surfaces a `LiveSync` `SkippedNotRunning` warning at the section.
9. A material-slot edit produces no exception, persists the authoring value,
   and maps descriptor URIs to cooked `.omat` paths for live runtime override
   sync.
10. Inspector VMs issue authoring/property commands and do not call runtime
    behavior or interop directly. Check the VM classes/call sites; the containing
    WorldEditor project legitimately references Runtime for viewport services.

## 15. Closed Design Decisions

Component editor factories remain composed by SceneNodeEditorViewModel; shared
property descriptors/mechanics belong to Schemas. Scene settings appear for
empty/no-node selection; no separate Scene tab is required. Wheel idle commit
is 250 ms. V0.1 uses the existing reusable controls with property-pipeline
sessions and field diagnostics; additional public registries are not required.

The canonical [property-pipeline.md](./property-pipeline.md) governs typed
property entry points, shared sessions/history, current field diagnostics and
revision-aware runtime convergence. Existing record adapters implement the same
contract; they are not an alternative architecture.
