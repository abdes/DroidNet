# Material Editor LLD

Status: `ED-M05 implementation-ready`

## 1. Purpose

Define the V0.1 scalar material editor baseline: material asset identity,
material documents, scalar PBR property editing, descriptor persistence, content
browser selection, assignment to geometry, minimum cook, and embedded preview
where engine APIs support it.

This LLD is not an ED-M04 implementation gate. ED-M04 only creates the Geometry
material assignment slot and leaves a clean handoff into this ED-M05 workflow.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-004` | Establish a real scalar material editor baseline. |
| `GOAL-005` | Make descriptor/cook/mount state understandable for material assets. |
| `GOAL-006` | Material create/save/cook/assign failures are visible. |
| `REQ-010` | Users can create/open scalar material assets through real material editor UI. |
| `REQ-011` | Users can inspect and edit scalar material properties. |
| `REQ-012` | Users can assign material assets to geometry. |
| `REQ-013` | Users can select material assets from the content browser with clear identity. |
| `REQ-014` | Material values save, reopen, cook, and preview where supported. |
| `REQ-021` | Content browser exposes relevant asset state for material workflows. |
| `REQ-022` | Material failures produce visible operation results. |
| `REQ-037` | Supported material data survives save/reopen without manual repair. |
| `SUCCESS-002` | Material edits survive save/reopen. |
| `SUCCESS-004` | Authored material data is available for runtime parity validation. |
| `SUCCESS-007` | Material assets can be created, edited, assigned, cooked, and previewed through real editor UI. |

## 3. Architecture Links

- `ARCHITECTURE.md`: material editor module, content-pipeline boundary, runtime
  preview, diagnostics.
- `DESIGN.md`: material LLD owner and material assignment workflow.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.MaterialEditor`, `Oxygen.Assets`,
  `Oxygen.Editor.ContentPipeline`, `Oxygen.Editor.ContentBrowser`.
- `property-inspector.md`: Geometry material assignment slot.
- `content-browser-asset-identity.md`: material picker and asset identity.
- `content-pipeline.md`: material import/cook orchestration.

## 4. Current Baseline

The repo already has material primitives:

- `projects/Oxygen.Assets/docs/material-json.md` defines authoring
  `*.omat.json` with `Schema = oxygen.material.v1`.
- `MaterialSource`, `MaterialPbrMetallicRoughness`, texture refs, alpha mode,
  and material source reader/writer exist in `Oxygen.Assets`.
- `MaterialSourceImporter` imports `*.omat.json`.
- `CookedMaterialWriter` writes cooked `.omat` descriptors.
- `MaterialAsset` carries optional `MaterialSource`.
- GLTF import can generate material sources.
- generated engine default material exists in the generated asset catalog.
- `GeometryComponent` already supports `MaterialsSlot` references.

Brownfield gaps:

- there is no material document/editor UI.
- the content browser does not yet provide a dedicated material create/open
  workflow.
- geometry material assignment is not wired to a material picker.
- material preview/live override support is incomplete.

## 5. Target Design

The ED-M05 target workflow is end-to-end:

```text
Content Browser
  -> create/open material asset
  -> Material Editor document
  -> edit scalar PBR properties
  -> save material JSON descriptor (*.omat.json)
  -> import/cook to cooked material (*.omat)
  -> assign material asset to Geometry material slot
  -> embedded preview where engine API supports it
```

Before the ED-M05 editor exists, manually authored Oxygen material descriptors
(`*.omat.json`, `oxygen.material.v1`) and manual or minimum cook steps are
acceptable bootstrapping tools. They are not the V0.1 user experience and must
not become hidden permanent workflow.

V0.1 explicitly excludes:

- texture authoring and texture parameter editing.
- material graph editing.
- custom shader authoring.
- procedural shader node workflows.

V0.1 includes a real scalar PBR material baseline:

- name/display identity.
- base color factor RGBA.
- metallic factor.
- roughness factor.
- alpha mode and alpha cutoff if the existing descriptor supports it.
- double-sided flag.
- normal scale and occlusion strength may be shown in Advanced when texture
  refs exist, but texture editing itself is deferred.

### UI Design Philosophy

The material editor should feel like a focused asset editor, not a generic JSON
form. It borrows from mature editor patterns:

- grouped material parameters.
- immediate preview where supported.
- reset/default affordances for scalar fields.
- primary/advanced/raw disclosure.
- clear asset identity and descriptor/cook status.

Illustrative ED-M05 editor:

```text
+--------------------------------------------------------------+
| Material: Preview Gold                              [save]   |
| [swatch preview]          asset:///Content/Materials/Gold... |
| Descriptor: saved   Cooked: stale                    [cook]  |
+--------------------------------------------------------------+
| Identity                                                     |
|   Asset URI      asset:///Content/Materials/Gold...    [copy] |
|   Asset GUID     0f2d...                               [copy] |
| PBR Metallic Roughness                                      |
|   Base Color     R [1.00] G [0.76] B [0.22] A [1.00]        |
|   Surface        Metallic [1.00] Roughness [0.35]           |
| > Advanced                                                  |
|   Alpha          Mode [Opaque v] Cutoff [0.50]              |
|   Rendering      Double Sided [off]                         |
+--------------------------------------------------------------+
```

The UI uses shared editor property controls (`PropertiesExpander` and
`PropertyCard`) and DroidNet `NumberBox` scalar editors. ED-M05 must not ship
one-off number boxes or one-off section chrome for this editor.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.MaterialEditor` | material document UI, scalar property editor, preview surface composition. |
| `Oxygen.Assets` | `MaterialSource`, material asset identity, import primitives, cooked material writer. |
| `Oxygen.Editor.ContentBrowser` | material create/open entry point and picker UX. |
| `Oxygen.Editor.ContentPipeline` | import/cook orchestration and descriptor/cooked state. |
| `Oxygen.Editor.WorldEditor` | geometry assignment command consumes material asset identity. |
| `Oxygen.Editor.Runtime` | embedded material preview/runtime application where supported. |

## 7. Data Contracts

### 7.1 Schema decision

ED-M05 reuses `oxygen.material.v1` (`MaterialSource` /
`MaterialPbrMetallicRoughness` / `MaterialAlphaMode`) **as-is**. No new
editor-only schema and no schema augmentation are required for V0.1. The
editor reads/writes `*.omat.json` through the existing
`MaterialSourceImporter` and a writer counterpart paired with
`CookedMaterialWriter`.

### 7.2 Authoring model

```csharp
public sealed class MaterialDocument
{
    public Guid DocumentId { get; }
    public Uri MaterialUri { get; }            // asset:///{Mount}/{Path}.omat.json
    public MaterialSource Source { get; }      // immutable source snapshot
    public MaterialAsset Asset { get; }        // identity only; Source is authoritative while editing
    public bool IsDirty { get; }
    public DescriptorState DescriptorState { get; }
    public MaterialCookState CookState { get; }
}

public enum DescriptorState { Saved, Dirty, Missing, Invalid }
```

`MaterialSource` and nested PBR records are immutable in `Oxygen.Assets`.
Material edits replace the affected record branch and publish a new document
snapshot; they do not rely on hidden mutable editor-only state.

Material asset name policy is option B: the user-facing asset name is the
material source file stem. `Content/Materials/Gold.omat.json` opens as `Gold`,
and saves with `MaterialSource.Name == "Gold"`. The Material Editor header
shows this name; the Identity section shows the material asset URI and an
editor-side material GUID with copy buttons. The GUID is derived from the
canonical asset URI for ED-M05 and is not written into `oxygen.material.v1`.
The editor must not create a second editable descriptor name that can drift from
Content Browser and filesystem names. A future Content Browser asset rename
command owns file/URI rename, stable registry identity, and reference repair.

`MaterialDocument.Source` is the editable snapshot. `MaterialDocument.Asset`
exists only to expose identity for assignment/open workflows; its optional
`MaterialAsset.Source` may be stale and is not updated during scalar edits.

`MaterialCookState` and `MaterialCookResult` are owned by
[content-pipeline.md](./content-pipeline.md). MaterialEditor consumes that
contract instead of defining a parallel cook-result type.

### 7.3 Field table (V0.1 PBR)

| UI Field | `MaterialSource` path | Type | Range / Validation | Tier |
| --- | --- | --- | --- | --- |
| Asset URI | `MaterialUri` | `Uri` | read-only with copy affordance | Primary |
| Asset GUID | derived editor identity | `Guid` | read-only with copy affordance; not serialized to material JSON | Primary |
| Name | `Name` | `string` | derived from `*.omat.json` file stem; shown in header, not edited as a scalar field | Primary |
| Base Color R | `PbrMetallicRoughness.BaseColorR` | `float` | `[0,1]`, clamped on commit | Primary |
| Base Color G | `PbrMetallicRoughness.BaseColorG` | `float` | `[0,1]` | Primary |
| Base Color B | `PbrMetallicRoughness.BaseColorB` | `float` | `[0,1]` | Primary |
| Base Color A | `PbrMetallicRoughness.BaseColorA` | `float` | `[0,1]` | Primary |
| Metallic | `PbrMetallicRoughness.MetallicFactor` | `float` | `[0,1]` | Primary |
| Roughness | `PbrMetallicRoughness.RoughnessFactor` | `float` | `[0,1]` | Primary |
| Alpha Mode | `AlphaMode` | enum `Opaque` / `Mask` / `Blend` | enum | Advanced |
| Alpha Cutoff | `AlphaCutoff` | `float` | `[0,1]`, enabled iff `AlphaMode == Mask` | Advanced |
| Double Sided | `DoubleSided` | `bool` | — | Advanced |
| Normal Scale | `NormalTexture.Scale` | `float` | `>= 0`, enabled iff `NormalTexture` exists | Advanced |
| Occlusion Strength | `OcclusionTexture.Strength` | `float` | `[0,1]`, enabled iff `OcclusionTexture` exists | Advanced |
| Texture refs (if any) | `PbrMetallicRoughness.BaseColorTexture`, `PbrMetallicRoughness.MetallicRoughnessTexture`, `NormalTexture`, `OcclusionTexture` | refs | **read-only** in V0.1 | Raw |
| Schema / Type | `Schema`, `Type` | string | read-only | Raw |

Clamp policy: out-of-range numeric input is **clamped on commit** and the
committed value is shown in the field; clamping does not produce a warning
unless the original input was non-numeric (then `Rejected`).

### 7.4 Asset identity in the scene

Geometry material assignment persists as
`AssetReference<MaterialAsset>` (URI + asset). The scene round-trip stores
only the URI; the catalog rehydrates the asset on load.

## 8. Commands, Services, Or Adapters

### 8.1 `IMaterialDocumentService`

```csharp
public interface IMaterialDocumentService
{
    Task<MaterialDocument> CreateAsync(Uri targetUri, CancellationToken ct);
    Task<MaterialDocument> OpenAsync(Uri materialUri, CancellationToken ct);
    Task<MaterialEditResult> EditScalarAsync(Guid documentId, MaterialFieldEdit edit, CancellationToken ct);
    Task<MaterialSaveResult> SaveAsync(Guid documentId, CancellationToken ct);
    Task<MaterialCookResult> CookAsync(Guid documentId, CancellationToken ct);
    Task CloseAsync(Guid documentId, bool discard, CancellationToken ct);
}

public readonly record struct MaterialFieldEdit(
    string FieldKey,            // e.g. "PbrMetallicRoughness.MetallicFactor"
    object NewValue);

public sealed record MaterialEditResult(bool Succeeded, OperationResultId? ResultId);
public sealed record MaterialSaveResult(bool Succeeded, OperationResultId? ResultId);
```

All mutating operations follow the ED-M03 command pattern: validate → mutate
→ mark dirty/record undo → diagnostics. `SaveAsync` is the operation that
persists the descriptor. Edits feed an `EditSessionToken` for sliders to
coalesce one undo entry per drag.

### 8.2 Assignment to geometry

Assignment uses the existing scene command from
[property-inspector.md](./property-inspector.md):

```csharp
Task<SceneCommandResult> EditMaterialSlotAsync(
    SceneDocumentCommandContext ctx,
    IReadOnlyList<Guid> nodeIds,
    int slotIndex,
    Uri? materialUri,
    EditSessionToken session);
```

The material picker (§9) returns a `MaterialPickerResult` whose `Uri` is
passed to `EditMaterialSlotAsync`. The scene command resolves it to
`AssetReference<MaterialAsset>` via `IAssetCatalog`. Single-node assignment
passes a one-item `nodeIds` list; multi-selection assignment writes the same
URI to every selected `GeometryComponent` that owns the slot. Live sync of the
slot remains `Unsupported` per [live-engine-sync.md](./live-engine-sync.md)
§8.2 in V0.1.

### 8.3 Operation kinds (for diagnostics)

`Material.Create`, `Material.Open`, `Material.EditScalar`, `Material.Save`,
`Material.Cook`, `Material.AssignToGeometry`, `Material.Preview`.

## 9. UI Surfaces

ED-M05 surfaces:

- Content Browser material create/open command.
- material asset tile/list row with clear visual identity and state.
- material picker used by Geometry material slot.
- Material Editor document tab/pane.
- scalar property inspector.
- preview surface or preview swatch where runtime preview is not yet stable.
- descriptor/cook state strip.
- output/result details for failed create/save/cook/preview.

Material picker:

```text
+-----------------------------------------------+
| Pick Material                           search |
+-----------------------------------------------+
| [swatch] Default       Engine/Generated        |
| [swatch] Preview Gold  Content/Materials       |
| [warn ] Missing Mat    Missing reference       |
+-----------------------------------------------+
```

`Create New` flow:

1. Opens a compact prompt for material name and target folder.
2. Defaults the target folder to `/Content/Materials` under the active project.
   A content-browser folder becomes the initial target only when it is already
   under `/Content`; arbitrary project-root folders such as `Scenes` are not
   inferred as material targets.
3. Creates `{Name}.omat.json` via `IMaterialDocumentService.CreateAsync`.
4. Opens the new material document. Assignment to a geometry slot is a separate
   explicit picker action unless the picker was launched from a slot and the
   user confirms assigning the newly created material.

## 10. Persistence And Round Trip

Requirements:

- material create writes a valid Oxygen `*.omat.json` descriptor unless an
  explicit schema decision above changes that.
- scalar edits update descriptor data, not hidden editor state.
- save/reopen preserves all supported scalar fields.
- raw/imported metadata that ED-M05 does not edit is preserved where practical.
- assignment to geometry persists as `AssetReference<MaterialAsset>`.

## 11. Live Sync / Cook / Runtime Behavior

### 11.1 Save

`SaveAsync` writes `*.omat.json` atomically (temp file + rename). On
failure: `MaterialSaveResult { Succeeded = false }` and diagnostic in
`Document` domain (`OXE.DOCUMENT.MATERIAL.SaveFailed`). Document remains
dirty.

### 11.2 Cook

`CookAsync` delegates to the public content-pipeline orchestration service.
That service uses `Oxygen.Assets` primitives (`MaterialSourceImporter` and
`CookedMaterialWriter`) to produce the cooked `.omat`; `Oxygen.Editor.MaterialEditor`
does not own cook primitive execution. If the descriptor is dirty, cook is
**rejected** with `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty` (user must
save first). On success the result transitions `MaterialCookState` to `Cooked`. The
cooked output path follows existing project cooked-root layout — this LLD does
not invent a new path scheme. For the default Content mount:

```text
source: <ProjectRoot>/Content/Materials/Gold.omat.json
cooked: <ProjectRoot>/.cooked/Content/Materials/Gold.omat
index:  <ProjectRoot>/.cooked/Content/container.index.bin
```

### 11.3 Preview

ED-M05 does not introduce a managed material preview API. Preview is:

1. **In the material document**: a deterministic CPU-rendered swatch driven
   by the scalar PBR values (no engine roundtrip). Shows base color, alpha,
   and a fixed-lighting ball thumbnail.
2. **In the scene**: the slot assignment is recorded in scene data. The
   geometry material override calls on `ISceneEngineSync`
   (`UpdateMaterialOverrideAsync`, `UpdateTargetedMaterialOverrideAsync`,
   `RemoveMaterialOverrideAsync`, `RemoveTargetedMaterialOverrideAsync`) return
   `Unsupported` by V0.1 editor policy (`OXE.LIVESYNC.MATERIAL.Unsupported`).
   The engine override API exists; ED-M05 does not wire that API into the
   editor sync surface.

No engine cooked-root remount is forced from the material editor. The user
triggers cook explicitly; project-level mount/refresh is owned by
`content-pipeline.md`.

## 12. Operation Results And Diagnostics

### 12.1 Failure domain

ED-M05 **adds** `MaterialAuthoring` to `FailureDomain`. Without it, scalar
field validation collides with `SceneAuthoring` (which is owned by scene
commands) and dilutes diagnostic filtering.

### 12.2 Failure mapping

| Failure | Domain | Code |
| --- | --- | --- |
| Numeric out of range | (clamped, no result) | — |
| Non-numeric input | `MaterialAuthoring` | `OXE.MATERIAL.Field.Rejected` |
| Empty / over-long name in Create New prompt | `MaterialAuthoring` | `OXE.MATERIAL.Name.Invalid` |
| Descriptor-only name edit attempt | `MaterialAuthoring` | `OXE.MATERIAL.Field.Rejected` |
| Save IO failure | `Document` | `OXE.DOCUMENT.MATERIAL.SaveFailed` |
| Cook with dirty descriptor | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty` |
| Cook IO / importer failure | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.CookFailed` |
| Picker selected URI not in catalog | `AssetIdentity` | `OXE.ASSETID.MATERIAL.Missing` |
| Picker selected URI not yet cooked | `AssetIdentity` | `OXE.ASSETID.MATERIAL.NotCooked` (warning only) |
| Slot live preview unsupported | `LiveSync` | `OXE.LIVESYNC.MATERIAL.Unsupported` |

`AffectedScope` always carries `AssetVirtualPath` (the material URI) for any
material-scoped diagnostic.

## 13. Dependency Rules

Allowed:

- MaterialEditor depends on `Oxygen.Assets` material contracts.
- MaterialEditor depends on ContentBrowser picker contracts.
- MaterialEditor invokes ContentPipeline through public orchestration services.

Forbidden:

- MaterialEditor must not own reusable cook primitives.
- MaterialEditor must not call native interop directly.
- Geometry inspector must not implement material asset editing.
- Material asset references must not be stored as cooked filesystem paths.

## 14. Validation Gates

1. Create → Save → Reopen of a material descriptor preserves all V0.1 PBR
   fields and texture refs (read-only) bit-for-bit.
2. Editing Metallic from 0 → 1 with 30 slider samples produces 1 undo entry
   and one dirty material-document update; the descriptor is written once when
   the user saves.
3. Cook with dirty descriptor returns `Rejected` with
   `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty`. After save, cook succeeds
   and `MaterialCookState` becomes `Cooked`.
4. Geometry assignment via picker writes the URI through
   `EditMaterialSlotAsync`, persists round-trip, and reports the slot's
   `MATERIAL.Unsupported` warning from live sync without blocking the edit.
5. A material URI removed from the catalog after assignment surfaces as
   `OXE.ASSETID.MATERIAL.Missing` on next scene open; the slot keeps the
   URI text and shows a missing badge.
6. Texture-ref fields are visibly read-only and do not produce edit commands.
7. No code path in `Oxygen.Editor.MaterialEditor` calls
   `OxygenWorld`, `IEngineService`, or `ISceneEngineSync` directly.

## 15. Open Issues

- Whether the deterministic swatch is later replaced by an embedded material
  preview view once runtime APIs mature. ED-M05 uses the deterministic swatch.
- Whether imported texture refs get first-class pickers after V0.1. ED-M05
  preserves them and shows them read-only.
- Whether any ED-M05 material-authoring need genuinely requires augmenting
  `oxygen.material.v1`; default assumption is no.
