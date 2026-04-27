# Content Browser And Asset Identity LLD

Status: `ED-M05 material-picker implementation-ready, ED-M06 full review later`

## 1. Purpose

Define the content browser UX, asset identity model, asset picker behavior, and
source/generated/cooked/missing asset states. ED-M05 consumes the material
picker slice; ED-M06 completes the broader content browser workflow.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-004` | Material assets are discoverable and assignable. |
| `GOAL-005` | Asset source/generated/cooked/missing states are understandable. |
| `GOAL-006` | Picker/catalog/pipeline failures are visible. |
| `REQ-013` | Users select material assets from content browser/picker with clear identity. |
| `REQ-020` | Content browser shows scoped asset state. |
| `REQ-021` | Content browser supports source/generated/cooked/missing asset understanding. |
| `REQ-022` | Catalog/picker/pipeline failures are visible. |
| `REQ-024` | Diagnostics identify asset identity/content/cook/mount causes. |
| `SUCCESS-006` | Pipeline state is understandable and actionable. |

## 3. Architecture Links

- `ARCHITECTURE.md`: asset identity, content browser, content pipeline, and
  diagnostics boundaries.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.ContentBrowser`, `Oxygen.Assets`,
  `Oxygen.Editor.ContentPipeline`, `Oxygen.Editor.Projects`.
- `material-editor.md`: material create/open/edit/assign workflow.
- `property-inspector.md`: Geometry material slot consumes picker result.
- `asset-primitives.md`: reusable asset identity contracts.
- `content-pipeline.md`: import/cook state production.

## 4. Current Baseline

The codebase already has:

- `IAssetCatalog`, `AssetRecord`, `AssetQuery`, and catalog change events.
- generated asset catalog for built-in geometry/material assets.
- project `ProjectAssetCatalog` composition with generated, filesystem, and
  loose-cooked catalog sources.
- `GeometryViewModel` dynamically populates a Geometry asset menu from
  `IAssetCatalog`.
- `MaterialAsset` and material source/cook primitives exist in `Oxygen.Assets`.

Brownfield gaps:

- no reusable typed asset picker contract.
- Geometry picker logic is local to `GeometryViewModel`.
- material assets do not yet have a first-class create/open/pick workflow.
- source/generated/cooked/missing state is not consistently exposed in UI.

## 5. Target Design

The content browser returns asset identity, not cooked file paths:

```text
asset source / generated asset / cooked index entry
  -> AssetRecord
  -> content browser item
  -> typed picker result
  -> AssetReference<TAsset>
```

For materials:

```text
*.omat.json / generated default material / cooked .omat
  -> MaterialAsset catalog record
  -> material picker row
  -> Geometry material slot AssetReference<MaterialAsset>
```

Until the ED-M05 material editor exists, Oxygen material descriptors
(`*.omat.json`, `oxygen.material.v1`) may be created manually and cooked
manually/minimally. The picker must still treat them as material assets with
stable identity.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.ContentBrowser` | navigation, display, selection, material picker UX. |
| `Oxygen.Assets` | asset identity, asset records, catalog data, material/geometry asset primitives. |
| `Oxygen.Editor.ContentPipeline` | source/generated/cooked state production and pipeline operations. |
| `Oxygen.Editor.Projects` | content roots and project policy. |
| `Oxygen.Editor.WorldEditor` | consumes picker result for scene assignment commands. |

## 7. Data Contracts

### 7.1 `AssetState`

```csharp
public enum AssetState
{
    Generated,    // engine/built-in generated asset (e.g. default material)
    Source,       // descriptor present (*.omat.json), not yet cooked
    Cooked,       // cooked artifact present and up-to-date
    Stale,        // cooked artifact present but older than descriptor
    Missing,      // referenced URI not found in any catalog
    Broken,       // descriptor or cooked artifact failed to load/validate
}
```

State is computed by combining `IAssetCatalog` records (source / generated /
cooked) with content-pipeline timestamps. Computation lives in
`Oxygen.Editor.ContentBrowser` adapters; catalogs themselves stay neutral.

### 7.2 `AssetPickerResult<TAsset>`

```csharp
public abstract record AssetPickerResult<TAsset>(
    Uri Uri,
    string Name,
    string DisplayPath,
    AssetState State,
    string? ThumbnailKey)
    where TAsset : Asset;
```

`Uri` is the only field persisted into scene data. `DisplayPath` is the
mount-relative path shown in UI. `ThumbnailKey` is opaque to the picker
consumer; the renderer resolves it (deterministic swatch acceptable in V0.1).

### 7.3 `MaterialPickerResult`

```csharp
public sealed record MaterialPickerResult(
    Uri Uri,
    string Name,
    string DisplayPath,
    AssetState State,
    string? ThumbnailKey,
    string? DescriptorPath,    // local FS path to *.omat.json when known
    string? CookedPath,        // local FS path to cooked .omat when known
    Color? BaseColorPreview)   // for swatch rendering when descriptor is loaded
    : AssetPickerResult<MaterialAsset>(Uri, Name, DisplayPath, State, ThumbnailKey);
```

`DescriptorPath` and `CookedPath` are diagnostic / open-action affordances
only. They are **never** persisted into scene data; only `Uri` is.

### 7.4 Geometry slot persistence

Geometry's material slot holds `AssetReference<MaterialAsset>`. On scene
save, only the URI travels through JSON. On scene load,
`ProjectAssetCatalog.Resolve(uri)` rehydrates the asset; if it returns no
record, the slot's `State` becomes `Missing` but the URI is preserved
verbatim.

## 8. Commands, Services, Or Adapters

### 8.1 `IMaterialPickerService`

```csharp
public interface IMaterialPickerService
{
    IObservable<IReadOnlyList<MaterialPickerResult>> Results { get; }
    Task RefreshAsync(MaterialPickerFilter filter, CancellationToken ct);
    Task<MaterialPickerResult?> ResolveAsync(Uri materialUri, CancellationToken ct);
}

public sealed record MaterialPickerFilter(
    string? SearchText = null,
    bool IncludeGenerated = true,
    bool IncludeSource = true,
    bool IncludeCooked = true,
    bool IncludeMissing = false);
```

`RefreshAsync` issues `IAssetCatalog.QueryAsync(new AssetQuery(AssetQueryScope.All))`,
filters to `MaterialAsset` records, then enriches with content-pipeline
state. Subscribes to `IAssetCatalog.Changes` and re-publishes `Results` on
`Added` / `Removed` / `Modified`.

`ResolveAsync(uri)` is used by the Geometry slot to render the **current**
assignment (including `Missing` rows) without opening the picker.

### 8.2 Picker invocation

From the Geometry inspector slot:

```text
user clicks slot
 -> open MaterialPickerView (modal flyout, V0.1)
 -> bind to IMaterialPickerService.Results
 -> user selects row -> MaterialPickerResult
 -> EditMaterialSlotAsync(ctx, [nodeId], slotIndex, result.Uri, OneShot)
```

Clearing the slot calls `EditMaterialSlotAsync(..., materialUri: null)`.

### 8.3 Operations (for diagnostics)

`Asset.Query`, `Asset.Resolve`, `Material.Pick`, `Material.Open`,
`Material.Create` (delegated to Material Editor).

## 9. UI Surfaces

### 9.1 Picker layout

```text
+--------------------------------------------------+
| Pick Material                       [search...]  |
| [x] Generated  [x] Source  [x] Cooked  [ ] Miss. |
+--------------------------------------------------+
| [swatch] Default          Engine/Generated   GEN |
| [swatch] Gold             Content/Materials  COOK|
| [swatch] Bronze (stale)   Content/Materials  STA |
| [swatch] Copper (src)     Content/Materials  SRC |
| [warn ]  MissingMat       (unresolved)       MIS |
+--------------------------------------------------+
| [Open in Material Editor] [Create New] [Clear]   |
+--------------------------------------------------+
```

### 9.2 State badges

| State | Badge | Behavior |
| --- | --- | --- |
| `Generated` | `GEN` (engine icon) | always selectable. |
| `Source` | `SRC` | selectable; consumer warned cook is required for runtime preview. |
| `Cooked` | `COOK` | selectable; preferred state. |
| `Stale` | `STA` | selectable; warning surfaced; user can trigger cook from picker action. |
| `Missing` | `MIS` | shown only when `IncludeMissing`; selectable to keep current dangling URI; clicking opens diagnostic. |
| `Broken` | `ERR` | not selectable; shows diagnostic on click. |

### 9.3 Geometry slot rendering of current assignment

The slot in [property-inspector.md](./property-inspector.md) renders
`ResolveAsync(currentUri)` output:

- `Generated|Source|Cooked|Stale` → swatch + name + state badge.
- `Missing|Broken` → warning icon + URI text + "Re-pick" affordance.
- `null` → `<None>` + "Pick Material…" affordance.

### 9.4 Broader content browser (ED-M06 only)

Source tree, descriptor/generated view, cooked output view, asset-type views,
missing/broken diagnostic presentation — deferred. ED-M05 ships the picker
alone.

## 10. Persistence And Round Trip

Asset selections persist as authoring identities:

- Geometry material slot stores `AssetReference<MaterialAsset>`.
- save/reopen preserves the URI even when missing/unresolved.
- picker can later resolve the URI to generated/source/cooked/missing state.

## 11. Live Sync / Cook / Runtime Behavior

The content browser does not mount cooked roots or mutate scenes directly. It
invokes content-pipeline/runtime/scene services through public contracts.

For ED-M05:

- material picker can assign an uncooked material identity.
- material preview/geometry runtime display may require cook/mount.
- missing cooked material is a diagnostic state, not a reason to rewrite the
  assignment.

## 12. Operation Results And Diagnostics

Failure mapping (codes under `OXE.ASSETID.*`, `OXE.ASSET_MOUNT.*`,
`OXE.CONTENTPIPELINE.*`):

| Failure | Domain | Code |
| --- | --- | --- |
| Catalog query exception | `AssetIdentity` | `OXE.ASSETID.QueryFailed` |
| Resolve(uri) returns null | `AssetIdentity` | `OXE.ASSETID.MATERIAL.Missing` |
| Resolve(uri) loads but descriptor invalid | `AssetIdentity` | `OXE.ASSETID.MATERIAL.Broken` |
| Cooked artifact older than descriptor | `ContentPipeline` (warning) | `OXE.CONTENTPIPELINE.MATERIAL.Stale` |
| Cook from picker action failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.CookFailed` |
| Assignment command failure | `SceneAuthoring` | (owned by scene command) |

`AffectedScope.AssetVirtualPath` carries the URI for every asset-scoped
diagnostic.

## 13. Dependency Rules

Allowed:

- ContentBrowser depends on `Oxygen.Assets`.
- WorldEditor consumes picker contracts but does not own picker internals.
- MaterialEditor opens assets returned by ContentBrowser.

Forbidden:

- ContentBrowser must not depend on WorldEditor internals.
- picker results must not persist cooked filesystem paths.
- content browser must not own scene mutation policy.
- content browser must not own cook primitive implementations.

## 14. Validation Gates

ED-M05 material-picker slice:

1. `IMaterialPickerService.RefreshAsync` returns at least the generated
   default material in a fresh project.
2. Adding a `*.omat.json` to a content root surfaces a row with
   `AssetState.Source` within one `IAssetCatalog.Changes` event.
3. Cooking that material via Material Editor flips the row state to
   `AssetState.Cooked`; modifying the descriptor afterwards flips it to
   `AssetState.Stale`.
4. Deleting the descriptor flips the assigned slot's resolved state to
   `AssetState.Missing`; the slot keeps the URI verbatim across save/reopen.
5. Picker selection produces a `MaterialPickerResult` whose `Uri` is what
   `EditMaterialSlotAsync` receives — no cooked path, no descriptor path.
6. Static check: persistence layer (`SceneJsonContext` outputs) contains
   only the asset URI for material slots, never `DescriptorPath` or
   `CookedPath`.
7. Search filter narrows visible rows in O(N) without re-querying the
   catalog.

Full ED-M06 validation — source/generated/cooked/pak inspection, broader
pipeline actions — deferred.

## 15. Open Issues

- Exact thumbnail/preview generation source.
- Whether picker is a modal, flyout, or docked reusable panel.
- Whether source and cooked entries are merged into one row or shown as related
  rows with state badges.
