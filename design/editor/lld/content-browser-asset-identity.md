# Content Browser And Asset Identity LLD

Status: `ED-M06 review-ready`

## 1. Purpose

Define the content browser UX, asset identity model, asset-state reducer,
typed picker behavior, and missing/broken reference presentation for ED-M06.
ED-M05 delivered the scalar material picker slice; ED-M06 turns that slice into
the reusable content browser identity workflow.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-004` | Material and future asset references are discoverable and assignable by identity. |
| `GOAL-005` | Source, generated, descriptor, cooked, missing, broken, and runtime-availability overlay states are visible in the browser; authoritative mounted state is deferred to `ED-M07`. |
| `GOAL-006` | Asset browsing, picking, and resolve failures produce visible diagnostics. |
| `REQ-013` | Users select material assets from a browser/picker using stable identity. |
| `REQ-020` | Content browser shows scoped asset state. |
| `REQ-021` | Authoring stores asset identity, not raw cooked paths. |
| `REQ-022` | Catalog/picker failures are visible. |
| `REQ-024` | Diagnostics identify asset identity, content-root, cook, mount, and missing-reference causes. |
| `REQ-036` | Project content browsing supports the V0.1 authoring workflow without manual filesystem repair. |
| `REQ-037` | Save/reopen preserves asset identities, including unresolved identities. |
| `SUCCESS-006` | Import/cook/mount state is understandable and actionable. |
| `SUCCESS-007` | Material authoring and assignment uses content browser identity. |

## 3. Architecture Links

- `ARCHITECTURE.md`: asset identity, content browser, content pipeline,
  diagnostics, and project content roots.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.ContentBrowser`, `Oxygen.Assets`,
  `Oxygen.Editor.ContentPipeline`, `Oxygen.Editor.Projects`.
- `asset-primitives.md`: catalog, URI, reference, and change primitives.
- `project-services.md`: project root, authoring roots, local roots, and
  cooked-root policy.
- `material-editor.md`: material create/open/edit/cook workflow.
- `property-inspector.md`: Geometry material slot consumes picker results.
- `diagnostics-operation-results.md`: operation kinds, domains, and code
  prefixes.

## 4. Current Baseline

The repository already has these useful pieces:

- `IAssetCatalog`, `AssetRecord`, `AssetQuery`, `AssetQueryScope`,
  `AssetQueryTraversal`, `AssetChange`, and `AssetChangeKind` in
  `Oxygen.Assets`.
- `ProjectAssetCatalog` in `Oxygen.Editor.ContentBrowser`, composing generated,
  filesystem, and loose-cooked catalog providers from the active
  `IProjectContextService`.
- `FileSystemAssetCatalog` for source files and `LooseCookedIndexAssetCatalog`
  for `.cooked/<Mount>/container.index.bin`.
- `GameAsset` and `AssetsLayoutViewModel`, which already show list/tile rows
  and merge `*.omat.json` / `*.omat` by a local logical key.
- `ProjectLayoutViewModel`, which owns the left project tree and mounted folder
  selection through `ContentBrowserState`.
- ED-M05 `IMaterialPickerService`, `MaterialPickerResult`,
  `MaterialPickerFilter`, `MaterialPreviewColor`, and generated/default
  material rows.
- Material create/open/cook messages from the Content Browser to the Material
  Editor.

Brownfield gaps ED-M06 must close:

- `GameAsset` is a file-oriented row; it does not carry stable identity facts,
  source/cooked relationships, mount availability, or diagnostics.
- logical source/cooked merge behavior exists only in `AssetsLayoutViewModel`
  and material picker code, not as a shared tested reducer.
- `AssetRecord` is intentionally minimal, so all type/state enrichment is ad
  hoc today.
- Content Browser defaults can still confuse project-root files, content
  authoring roots, and `.cooked` output.
- picker state exists for materials only; ED-M06 must define the reusable
  typed-picking contract without forcing every picker consumer to persist
  descriptor/cooked paths.

## 5. Target Design

The Content Browser presents browser rows built from catalog records and
project-root policy. It never presents raw cooked filesystem paths as authored
identity.

```text
------------------------- Oxygen.Assets -------------------------+
| AssetRecord(Uri) + AssetChange                                  |
+---------------------------- query ------------------------------+
                             |
                             v
+------------------- Oxygen.Editor.ContentBrowser ----------------+
| AssetIdentityReducer                                            |
|   records + project roots + optional referenced URIs             |
|      -> ContentBrowserAssetItem                                  |
|      -> AssetPickerResult<TAsset>                                |
+---------------------------- consume ----------------------------+
                             |
          +------------------+------------------+
          v                                     v
  Content Browser list/tile               Material picker /
  state badges and details                inspector slots
```

Source/cooked rows are merged into one visible asset item when they share the
same logical identity. The item still exposes the source and cooked facts
separately for diagnostics and actions.

```text
Content/Materials/Red.omat.json       asset:///Content/Materials/Red.omat.json
.cooked/Content/Materials/Red.omat via index
                                      asset:///Content/Materials/Red.omat
                 \                    /
                  v                  v
            ContentBrowserAssetItem
            IdentityUri = asset:///Content/Materials/Red.omat.json
            Kind = Material
            PrimaryState = Descriptor
            DerivedState = Cooked | Stale
            RuntimeAvailability = NotMounted | Mounted | Unknown
```

`AssetState` describes authoring/cooked identity state. Runtime mount
availability is a separate overlay because an asset can be both `Stale` and
`Mounted`, or `Cooked` and `NotMounted`. ED-M06 normally publishes
`Unknown`/`NotMounted`; `Mounted` is reserved for later authoritative runtime
availability input.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.ContentBrowser` | browser row model, state reducer, list/tile UX, typed picker UX, content-root display policy. |
| `Oxygen.Assets` | asset URI, references, catalog query/change primitives, generated/filesystem/cooked index providers. |
| `Oxygen.Editor.Projects` | active project context, authoring roots, local roots, cooked-root policy. |
| `Oxygen.Editor.ContentPipeline` | import/cook/index operations and later full state production. |
| `Oxygen.Editor.MaterialEditor` | material create/open/edit/cook UI. |
| `Oxygen.Editor.WorldEditor` | consumes typed picker result and persists asset reference URI in scene components. |

## 7. Data Contracts

### 7.1 Asset Kind

`AssetKind` is an editor-facing classification computed from URI/path/schema:

```csharp
public enum AssetKind
{
    Folder,
    Material,
    Geometry,
    Scene,
    Texture,
    Image,
    ImportSettings,
    ForeignSource,
    CookedData,
    CookedTable,
    Unknown,
}
```

This replaces `GameAsset.AssetType` for ED-M06 browser rows. ED-M06 updates
`AssetsLayoutViewModel`, `AssetsView`, and item-invocation dispatch from
`GameAsset` to `ContentBrowserAssetItem` in one pass. Any temporary adapter is
local to Content Browser and must not leak into picker contracts.

`AssetKind`, `AssetState`, `AssetRuntimeAvailability`, and
`ContentBrowserAssetItem` live in a shared Content Browser asset-identity
namespace, not in `Oxygen.Editor.ContentBrowser.Materials`. The ED-M05
material-only `AssetState` location is replaced before `Descriptor` is added.

### 7.2 Asset State

```csharp
public enum AssetState
{
    Generated,   // built-in or generated identity, no user source file
    Source,      // non-descriptor authoring/source file exists
    Descriptor,  // descriptor source exists and is the primary authoring record
    Cooked,      // cooked artifact/index entry exists and is up-to-date
    Stale,       // source/descriptor exists and cooked artifact is older
    Missing,     // referenced URI is not present in any catalog
    Broken,      // record exists but descriptor/index/metadata failed validation
}
```

`Descriptor` is used for engine/tooling descriptors such as `*.omat.json`,
`*.ogeo.json`, `*.oscene.json`, and `*.otex.json`. A descriptor can also have
`Cooked` or `Stale` derived state in the same row; the row exposes both the
primary source state and the best derived state.

ED-M06 state requirements:

- `AssetState.Descriptor` is added to the existing ED-M05 enum.
- `AssetState.Source` remains for non-descriptor authoring/source files such as
  imported images, glTF/FBX sources, or other raw files under browsed roots.
- `*.omat.json` material picker rows switch from `Source` to `Descriptor`.
- `MaterialPickerService`, Geometry material slot rendering, and Content
  Browser row rendering are updated in lockstep. No consumer should infer
  descriptor state by checking `Source`.
- missing/broken/stale/cooked behavior keeps the same user meaning as ED-M05,
  but stale/cooked are represented as derived state for merged rows.

### 7.3 Runtime Availability

```csharp
public enum AssetRuntimeAvailability
{
    NotApplicable, // source-only or generated item with no runtime mount state
    Unknown,       // mount state is not observable in ED-M06
    NotMounted,    // cooked entry exists but current runtime mount is not known
    Mounted,       // runtime/mount catalog confirms availability
}
```

ED-M06 may show `Unknown` or `NotMounted` when runtime mount state is not yet
fully wired. ED-M06 must not fake `Mounted` from the presence of a cooked file.
Full mount refresh and authoritative runtime availability close in ED-M07.

### 7.4 Content Browser Asset Item

```csharp
public sealed record ContentBrowserAssetItem(
    Uri IdentityUri,
    AssetKind Kind,
    string DisplayName,
    string DisplayPath,
    string MountName,
    AssetState PrimaryState,
    AssetState? DerivedState,
    AssetRuntimeAvailability RuntimeAvailability,
    Uri? SourceUri,
    Uri? CookedUri,
    string? SourcePath,
    string? CookedPath,
    string? ThumbnailKey,
    IReadOnlyList<DiagnosticRecord> Diagnostics);
```

Rules:

- `IdentityUri` is the stable authored identity for selection and copy.
- `PrimaryState` describes the identity selected by the user: generated,
  source/descriptor, missing, or broken.
- `DerivedState` describes derived cooked state when a related cooked row
  exists: `Cooked` or `Stale`. It is `null` for source-only/generated/missing
  rows.
- `DisplayPath` is mount-relative (`Content/Materials/Red.omat.json`) and is
  never a raw `.cooked` filesystem path.
- `SourcePath` and `CookedPath` are diagnostic/open-action affordances only.
- `Diagnostics` contains row-local missing/broken/stale diagnostics already
  adapted to `diagnostics-operation-results.md`.
- user copy commands expose both `IdentityUri` and physical paths where known,
  but scene/material persistence consumes only `IdentityUri`.

### 7.5 Typed Picker Result

```csharp
public abstract record AssetPickerResult<TAsset>(
    Uri Uri,
    string DisplayName,
    string DisplayPath,
    AssetState PrimaryState,
    AssetState? DerivedState,
    AssetRuntimeAvailability RuntimeAvailability,
    string? ThumbnailKey)
    where TAsset : Asset;
```

Only `Uri` crosses into authoring persistence. Picker-only fields are display
and diagnostic affordances.

### 7.6 Material Picker Result

The ED-M05 material result stays as the material-specialized projection:

```csharp
public sealed record MaterialPickerResult(
    Uri MaterialUri,
    string DisplayName,
    AssetState PrimaryState,
    AssetState? DerivedState,
    AssetRuntimeAvailability RuntimeAvailability,
    string? DescriptorPath,
    string? CookedPath,
    MaterialPreviewColor? BaseColorPreview);
```

ED-M06 replaces the ED-M05 `MaterialPickerResult.State` field with
`PrimaryState` plus `DerivedState` when the picker moves to the shared provider.
Badge precedence for compact picker/slot display is:
`Broken`, `Missing`, `Stale`, `Cooked`, `Descriptor`, `Source`, `Generated`.
The full details surface still shows both primary and derived states where both
exist. `DisplayPath` and `ThumbnailKey` are excluded from
`MaterialPickerResult` in ED-M06; reconsider them in ED-M07 only if a concrete
consumer needs those projection fields. Partial duplicate result types are not
allowed.

## 8. Services And Adapters

### 8.1 Asset Identity Reducer

New Content Browser service:

```csharp
public interface IAssetIdentityReducer
{
    IReadOnlyList<ContentBrowserAssetItem> Reduce(
        IReadOnlyList<AssetRecord> records,
        AssetBrowserContext context,
        IReadOnlyList<Uri> referencedUris);
}

public sealed record AssetBrowserContext(
    ProjectContext Project,
    IReadOnlyList<string> SelectedFolders,
    string? SearchText,
    AssetBrowserFilter Filter);
```

Reducer responsibilities:

- classify `AssetKind` from descriptor/cooked/source extension.
- normalize URI logical keys (`*.omat.json` and `*.omat` merge).
- merge source/descriptor/cooked records into one item.
- add `Missing` items for `referencedUris` not found in records.
- mark `Broken` only when a known descriptor/index/source cannot be read or
  validated.
- compare source/cooked timestamps only through resolved project paths.
- preserve unresolved URIs verbatim.
- use `AssetChange.PreviousUri` on relocate to invalidate old logical keys.

Broken-state detection is Content Browser adapter work, not an
`Oxygen.Assets` catalog responsibility:

- catalog providers enumerate identities only.
- the provider schedules descriptor validation off the UI thread when material,
  geometry, scene, or texture descriptor rows are first materialized.
- V0.1 material descriptors are validated with `MaterialSourceReader`; other
  descriptor kinds may validate only file existence/readability until their
  owning LLD reaches implementation.
- validation failures are cached by identity and last-write timestamp until an
  `AssetChange` for that URI or `PreviousUri` invalidates the cache.
- a broken descriptor produces a `ContentBrowserAssetItem` with
  `PrimaryState = Broken`, row diagnostics, and no crash.
- loose cooked index read failures remain inside `LooseCookedIndexAssetCatalog`
  as catalog query failures unless the index exposes a record whose target path
  cannot be resolved; that record becomes `Broken` or `Missing` according to
  the reducer's path-resolution result.

### 8.2 Asset Browser Filter

```csharp
public sealed record AssetBrowserFilter(
    string? SearchText,
    IReadOnlySet<AssetKind> Kinds,
    bool IncludeGenerated,
    bool IncludeSource,
    bool IncludeDescriptor,
    bool IncludeCooked,
    bool IncludeStale,
    bool IncludeMounted,
    bool IncludeMissing,
    bool IncludeBroken);
```

Default browser filter:

- include generated/source/descriptor/cooked/stale/broken. For merged rows,
  filters match either `PrimaryState` or `DerivedState`.
- exclude missing unless the current workspace has unresolved references or
  the user enables the Missing filter.

Default picker filter:

- include generated/source/descriptor/cooked/stale.
- exclude broken and missing from normal browse results.
- always show the current unresolved assignment row when opening a picker from
  a missing/broken slot.

The ED-M05 `MaterialPickerFilter` becomes a material-specific facade over
`AssetBrowserFilter`: `IncludeSource` maps to `IncludeSource` and
`IncludeDescriptor`, `IncludeCooked` maps to `IncludeCooked` and
`IncludeStale`, and existing `IncludeMissing` maps to both `IncludeMissing`
and `IncludeBroken`. Material picker results force `Kinds = { Material }`.
ED-M06 does not split `MaterialPickerFilter.IncludeMissing` into separate
missing/broken flags unless this LLD is reopened.

### 8.3 Browser Row Provider

```csharp
public interface IContentBrowserAssetProvider
{
    IObservable<IReadOnlyList<ContentBrowserAssetItem>> Items { get; }
    Task RefreshAsync(AssetBrowserFilter filter, CancellationToken ct);
    Task<ContentBrowserAssetItem?> ResolveAsync(Uri uri, CancellationToken ct);
}
```

`AssetsLayoutViewModel` consumes this provider instead of directly querying
`IAssetCatalog` and constructing `GameAsset` rows. The provider owns catalog
subscription and coalesces change bursts before publishing UI rows.

The provider exposes `Items`, `RefreshAsync`, and `ResolveAsync` only.
`Asset.Query` remains an operation kind for picker/browser filter passes over
the snapshot; ED-M06 does not add a separate provider `QueryAsync` method.

ED-M06 keeps `IProjectAssetCatalog` as the catalog-composition layer.
`IContentBrowserAssetProvider` is the higher-level adapter that subscribes to
`IProjectAssetCatalog`, runs `IAssetIdentityReducer`, and publishes
`ContentBrowserAssetItem` snapshots.

### 8.4 Picker Service Pattern

The existing `IMaterialPickerService` remains the ED-M06 material picker
contract. It should be implemented as a material projection over
`IContentBrowserAssetProvider`, not as a second independent state reducer.

Future typed pickers follow the same pattern:

```csharp
public interface IAssetPickerService<TAsset, TResult>
    where TAsset : Asset
    where TResult : AssetPickerResult<TAsset>
{
    IObservable<IReadOnlyList<TResult>> Results { get; }
    Task RefreshAsync(AssetBrowserFilter filter, CancellationToken ct);
    Task<TResult?> ResolveAsync(Uri uri, CancellationToken ct);
}
```

ED-M06 can implement this typed picker contract through the material picker
only, but it must not add a second material-only reducer.

## 9. UI Design

ED-M06 Content Browser is an operational tool: dense, clear, and optimized for
repeat asset browsing, not a gallery page.

### 9.1 Workspace Layout

```text
+-------------------------------------------------------------------+
| <  >  ^   Project / Content / Materials             [search....]  |
| [Tiles] [List] [Refresh]     Kind: [All v]  State: [All v]        |
+---------------------------+---------------------------------------+
| Project                   |  [swatch] Red.omat.json     MAT COOK  |
|  Content                  |           Content/Materials/Red        |
|   Materials               |           asset:///Content/...         |
|  Scenes                   |                                       |
|  Local Mounts             |  [warn ] MissingGlass       MAT MIS   |
|  Cooked (derived)         |           asset:///Content/...         |
+---------------------------+---------------------------------------+
| Details: URI [copy]  Source [open]  Cooked [open]  Diagnostics    |
+-------------------------------------------------------------------+
```

Rules:

- left tree selects folders/mounts; right pane shows asset identity rows.
- `.cooked` is shown as derived output, not as a valid material-create target.
- rows show icon/swatch, display name, display path, state badge, and any
  warning/error badge.
- selecting a row exposes a compact details strip or side details area with
  copy actions for URI, source path, and cooked path where known.
- copy URI is always available for asset rows.
- raw cooked paths appear only in details or diagnostics, never as the primary
  row identity.

### 9.2 State Badges

| State / Overlay | Badge | User behavior |
| --- | --- | --- |
| `Generated` | `GEN` | selectable; details show generated source. |
| `Source` | `SRC` | selectable when the consumer supports source identity. |
| `Descriptor` | `DESC` | selectable; primary authoring record. |
| `Cooked` | `COOK` | selectable; cooked artifact is current. |
| `Stale` | `STALE` | selectable; warning says cook is needed. |
| `Missing` | `MISS` | not a normal browse row; visible for unresolved references. |
| `Broken` | `ERR` | not selectable for new assignment; details show diagnostics. |
| `Mounted` | `MNT` | overlay badge when runtime availability is known. |

### 9.3 Picker UX

Material picker reuses the same state language:

```text
+-----------------------------------------------------------+
| Pick Material                                 [search...] |
| [Generated] [Source] [Descriptor] [Cooked] [Stale] [Miss] |
+-----------------------------------------------------------+
| [swatch] Default        Engine/Generated       GEN        |
| [swatch] Red            Content/Materials      COOK       |
| [swatch] Blue           Content/Materials      STALE      |
| [warn ] MissingGlass    Current assignment     MISS       |
+-----------------------------------------------------------+
| [Open] [Create New] [Clear] [Cancel]                      |
+-----------------------------------------------------------+
```

Opening from a missing/broken geometry slot pins the current unresolved row at
the top even when Missing/Broken filters are off.

### 9.4 Create/Open Policy

- Material creation target resolution is owned by
  `project-layout-and-templates.md`.
- Project root, `Config`, `Packages`, and derived roots resolve to
  `/Content/Materials`.
- Authored mount roots resolve to `/<Mount>/Materials`; selected material
  folders inside an authored mount are reused.
- Opening a material uses `IdentityUri`; the Material Editor resolves source
  descriptor path from project content roots.

## 10. Persistence And Round Trip

- Scene material slots persist only `AssetReference<MaterialAsset>.Uri`.
- Content browser state persists selected folders and view mode, not
  `ContentBrowserAssetItem` rows.
- Missing/broken identities survive save/reopen unchanged.
- `DescriptorPath`, `CookedPath`, state badges, thumbnails, diagnostics, and
  mount overlays are derived current-session facts.

## 11. Cook / Mount / Runtime Behavior

ED-M06 reads cook and mount state; it does not execute cook/mount workflows.

Allowed:

- query loose cooked indexes.
- display stale/cooked/missing/broken state.
- show actions that navigate to Material Editor or diagnostics.
- refresh browser rows after `AssetsChangedMessage` / `AssetsCookedMessage`.

Forbidden in ED-M06:

- invoking full scene cook.
- mounting cooked roots.
- validating standalone runtime load.
- treating a cooked filesystem path as authored identity.

Full cook/inspect/mount refresh closes in ED-M07.

## 12. Operation Results And Diagnostics

Operation kinds:

| Operation Kind | Producer | Domain |
| --- | --- | --- |
| `Asset.Browse` | Content Browser asset provider | `AssetIdentity` |
| `Asset.Query` | catalog-backed provider/picker | `AssetIdentity` |
| `Asset.Resolve` | picker/slot/browser details | `AssetIdentity` |
| `Asset.CopyIdentity` | Content Browser row/details | `AssetIdentity` |
| `ContentBrowser.Navigate` | Content Browser shell | `AssetIdentity` / `ProjectContentRoots` |
| `ContentBrowser.Refresh` | Content Browser shell/provider | `AssetIdentity` |
| `Material.Pick` | material picker | `AssetIdentity` |

Reducer failures are child diagnostics on the triggering `Asset.Browse`,
`Asset.Query`, or `Asset.Resolve` operation. ED-M06 does not publish a separate
top-level `Asset.ReduceState` result.

Diagnostic codes:

| Failure | Domain | Code |
| --- | --- | --- |
| Catalog query exception | `AssetIdentity` | `OXE.ASSETID.QueryFailed` |
| Browser row reduction failed | `AssetIdentity` | `OXE.ASSETID.ReduceFailed` |
| Resolve URI missing | `AssetIdentity` | `OXE.ASSETID.Resolve.Missing` |
| Descriptor cannot be parsed | `AssetIdentity` | `OXE.ASSETID.Descriptor.Broken` |
| Cooked index entry missing target | `AssetIdentity` | `OXE.ASSETID.Cooked.Missing` |
| Selected content root is invalid | `ProjectContentRoots` | `OXE.PROJECT.CONTENT_ROOT.InvalidSelection` |
| Cooked output older than source | `ContentPipeline` warning | `OXE.CONTENTPIPELINE.Asset.Stale` |

`AffectedScope.AssetVirtualPath` carries the asset URI string. Diagnostics may
also carry `AffectedPath` for source/cooked filesystem paths.

## 13. Dependency Rules

Allowed:

- Content Browser depends on `Oxygen.Assets`, `Oxygen.Editor.Projects`,
  `Oxygen.Core.Diagnostics`, and editor messaging.
- Material Editor opens material identities supplied by Content Browser.
- WorldEditor consumes picker contracts and persists asset URIs.

Forbidden:

- Content Browser must not depend on WorldEditor internals.
- Content Browser must not mutate scene components.
- Content Browser must not own import/cook primitive implementations.
- picker/browser rows must not persist descriptor/cooked paths into authoring
  data.
- `Oxygen.Assets` primitives must not depend on Content Browser row types.

## 14. Validation Gates

ED-M06 is complete when:

1. content browser list/tile rows show `Generated`, `Descriptor`, `Source`,
   `Cooked`, `Stale`, `Missing`, and `Broken` states where test data provides
   those states.
2. source descriptor and cooked index entries for the same logical material
   merge into one row with separate source/cooked details.
3. changing a descriptor after cook makes the row `Stale`.
4. deleting a descriptor for a referenced material shows a `Missing` row for
   the unresolved assignment and preserves the URI.
5. broken descriptor JSON produces a visible row diagnostic and does not crash
   the browser.
6. material picker uses the same reducer/provider state as the browser and
   still returns only typed material identity to Geometry assignment.
7. content browser details can copy asset URI; source/cooked path copy appears
   only when those paths are known.
8. selecting `.cooked` or project infrastructure folders never changes the
   default material-create target away from a valid authoring content root.
9. `AssetChangeKind.Relocated` removes the previous URI row and adds/updates
   the new URI row.
10. serializer round-trip test confirms scene material slots contain only asset
    URIs, not browser state or filesystem paths.

Expected tests:

- `AssetIdentityReducer` merge/state tests for descriptor/cooked/stale/missing
  and broken descriptors.
- provider tests for `Added`, `Removed`, `Updated`, and `Relocated` changes.
- material picker projection test proving it consumes the shared provider.
- content-root target normalization tests for material create/open paths.

Manual validation belongs to the user: launching the editor, browsing a project
with source/cooked materials, filtering states, assigning material from picker,
deleting/breaking a descriptor, and confirming visible diagnostics.

## 15. Open Issues

- Exact thumbnail generation source. ED-M06 may keep deterministic swatches and
  file/type icons.
- ED-M06 does not claim authoritative runtime `Mounted`. It shows
  `Unknown`/`NotMounted` unless a later ED-M07 mount-refresh workflow supplies
  authoritative availability.
