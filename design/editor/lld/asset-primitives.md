# Asset Primitives LLD

Status: `ED-M06 review-ready`

## 1. Purpose

Define the reusable `Oxygen.Assets` primitives used by the editor for asset
identity, references, catalogs, material descriptors, import, cooked outputs,
and loose cooked indexes.

This LLD is deliberately not a content-browser or content-pipeline design. It
defines the reusable data/model/tooling layer those editor features consume.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-004` | Material authoring uses real material assets and descriptors. |
| `GOAL-005` | Asset identity, descriptor, cooked, stale, and missing states have reusable primitives. |
| `GOAL-006` | Primitive failures can be classified by consuming workflows. |
| `REQ-013` | Material picker can query material assets by stable identity. |
| `REQ-014` | Material values save/reopen/cook through existing material descriptor primitives. |
| `REQ-015` | Descriptor generation uses engine/tooling schemas where they already exist. |
| `REQ-016` | Import/cook operations have reusable primitives. |
| `REQ-017` | Cooked outputs can be indexed and queried. |
| `REQ-018` | Cook output can be validated before mount by later pipeline work. |
| `REQ-020` | Content browser can build state views from catalog primitives. |
| `REQ-021` | Authoring stores asset identity, not raw cooked path text. |
| `REQ-024` | Asset failures can be routed to narrow domains. |
| `REQ-037` | Persisted authoring data survives save/reopen without manual repair. |

## 3. Architecture Links

- `ARCHITECTURE.md`: asset identity, content pipeline, diagnostics, and data
  contract rules.
- `PROJECT-LAYOUT.md`: `Oxygen.Assets` owns reusable asset/cook primitives;
  editor projects own workflows and UI.
- `material-editor.md`: consumes material source, material cook, and material
  identity primitives.
- `content-browser-asset-identity.md`: consumes catalog/query/reference
  primitives for picker UX.
- `content-pipeline.md`: orchestrates import/cook/index primitives.

## 4. Current Baseline

The repo already contains the ED-M05 material slice primitives:

- `Asset`, `GeometryAsset`, `MaterialAsset`, and
  `AssetReference<TAsset>` in `Oxygen.Assets.Model`.
- canonical asset URI helpers in `Oxygen.Core.AssetUris` and
  `Oxygen.Assets.Catalog.AssetUriHelper`.
- `IAssetCatalog`, `AssetRecord`, `AssetQuery`, `AssetQueryScope`,
  `AssetQueryTraversal`, `AssetChange`, and `AssetChangeKind`.
- `GeneratedAssetCatalog` and `BuiltInAssets`, including the generated default
  material `asset:///Engine/Generated/Materials/Default`.
- `FileSystemAssetCatalog`, `LooseCookedIndexAssetCatalog`, and
  `CompositeAssetCatalog` style composition through the content-browser
  project catalog.
- `MaterialSource`, `MaterialPbrMetallicRoughness`,
  `MaterialAlphaMode`, texture-ref records, `MaterialSourceReader`, and
  `MaterialSourceWriter`.
- `MaterialSourceImporter` for `*.omat.json`.
- `CookedMaterialWriter` and `LooseCookedBuildService` for cooked `.omat`
  descriptors and loose cooked indexes.
- tests covering `AssetReference<TAsset>`, generated catalog queries, material
  source read/write, cooked material writer, loose cooked index behavior, and
  import/cook helpers.

Brownfield gaps:

- `AssetRecord` is intentionally minimal and does not carry asset type or
  state. Editor adapters must enrich records without mutating the primitive.
- source, generated, descriptor, cooked, stale, mounted, missing, and broken
  states are not primitive enums in `Oxygen.Assets`; the content-browser LLD
  owns UI state and runtime-availability overlays.
- full descriptor/manifest orchestration is not a primitive; it belongs to
  `Oxygen.Editor.ContentPipeline`.

## 5. Target Design

`Oxygen.Assets` is the reusable layer:

```text
-------------------------------------------------------------+
| Editor workflows                                            |
| MaterialEditor | ContentBrowser | ContentPipeline | World   |
+-------------------------- consume --------------------------+
| Oxygen.Assets primitives                                    |
| Asset identity | references | catalogs | material source     |
| import outputs | cooked writers | loose cooked index         |
+-------------------------------------------------------------+
```

ED-M05 uses the existing Oxygen material schema directly:

```text
Content/Materials/Gold.omat.json
  -> MaterialSourceReader / MaterialSourceWriter
  -> MaterialSourceImporter
  -> CookedMaterialWriter
  -> .cooked/Content/Materials/Gold.omat
  -> LooseCookedBuildService updates container.index.bin
  -> ProjectAssetCatalog exposes asset:///Content/Materials/Gold.omat
```

No ED-M05 editor-side material JSON schema is introduced. If later material
authoring needs fields missing from `oxygen.material.v1`, the content-pipeline
and material-editor LLDs must decide whether to augment the engine/tooling
schema or introduce a separate editor schema before implementation.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Assets` | reusable asset identity, references, catalog primitives, material source model, import/cook writers, loose index utilities. |
| `Oxygen.Core` | shared URI and diagnostics constants used by asset workflows. |
| `Oxygen.Editor.ContentBrowser` | asset state enrichment, browsing, picker UX, thumbnails/swatches. |
| `Oxygen.Editor.MaterialEditor` | material document UI and user commands over material descriptors. |
| `Oxygen.Editor.ContentPipeline` | editor orchestration over import/cook/index primitives. |
| `Oxygen.Editor.WorldEditor` | scene commands that persist asset references in components. |

## 7. Data Contracts

### 7.1 Asset URI

Canonical editor asset identity is:

```text
asset:///{MountPoint}/{Path}
```

Examples:

- `asset:///Engine/Generated/Materials/Default`
- `asset:///Content/Materials/Gold.omat.json`
- `asset:///Content/Materials/Gold.omat`

Rules:

- `asset` is the only V0.1 asset URI scheme.
- mount point is the first path segment, not a filesystem drive or URI host.
- `/` separators are used in persisted identities.
- URI comparisons for editor asset identity are ordinal on the normalized URI
  string produced by the asset helpers.
- user-facing UI may show mount-relative display paths, but persisted scene and
  material references store only the URI.

### 7.2 `AssetReference<TAsset>`

`AssetReference<TAsset>` is the persisted reference wrapper for scene/domain
objects:

- `Uri` is serialized and is the source of truth.
- `Asset` is runtime-only and rehydrated from catalogs/resolvers.
- changing `Uri` invalidates the cached `Asset` unless it still matches.
- clearing `Asset` must preserve `Uri` so missing references survive
  save/reopen.

ED-M05 geometry material assignment persists
`AssetReference<MaterialAsset>` URI only. Picker-only fields such as thumbnail,
descriptor path, and cooked path must not be serialized into scene data.

### 7.3 Catalog Contracts

`IAssetCatalog.QueryAsync(AssetQuery)` returns lightweight `AssetRecord`
instances. `AssetRecord.Uri` is the stable identity. Name is derived from the
URI path.

`AssetQueryScope` is a record, not an enum:

```csharp
public sealed record AssetQueryScope(
    IReadOnlyList<Uri> Roots,
    AssetQueryTraversal Traversal)
{
    public static AssetQueryScope All { get; }
}
```

`AssetQueryTraversal` controls how the catalog walks from the roots:

| Query intent | Contract shape |
| --- | --- |
| picker/global search | `new AssetQuery(AssetQueryScope.All)` |
| resolve exact assignment | `Roots = [assetUri]`, `Traversal = AssetQueryTraversal.Self` |
| current folder view | `Roots = [folderUri]`, `Traversal = AssetQueryTraversal.Children` |
| recursive folder search | `Roots = [folderUri]`, `Traversal = AssetQueryTraversal.Descendants` |

`AssetChange` is a record carrying an `AssetChangeKind`:

```csharp
public sealed record AssetChange(
    AssetChangeKind Kind,
    Uri Uri,
    Uri? PreviousUri = null);
```

| `AssetChangeKind` | Meaning |
| --- | --- |
| `Added` | asset became visible. |
| `Removed` | asset disappeared. |
| `Updated` | same identity, changed metadata/content. |
| `Relocated` | identity changed; `PreviousUri` carries the old value. |

Catalogs do not compute user-facing `AssetState`, `AssetKind`, or runtime
mount availability; they provide records and changes.
`content-browser-asset-identity.md` defines state enrichment.

### 7.4 Catalog Identity Limits

`AssetRecord` remains intentionally minimal in ED-M06:

```csharp
public sealed record AssetRecord(Uri Uri)
{
    public string Name { get; }
}
```

ED-M06 must not add browser state fields directly to `AssetRecord`. The same
record may represent:

- source descriptor: `asset:///Content/Materials/Red.omat.json`.
- cooked index entry: `asset:///Content/Materials/Red.omat`.
- generated asset: `asset:///Engine/Generated/Materials/Default`.
- local/foreign source file under a mounted folder.

Consumers that need file paths, timestamps, asset type, diagnostics, or mount
availability resolve those facts in editor-owned adapters using
`ProjectContext`, catalog provider type, and storage services. This keeps
`Oxygen.Assets` usable by tools and tests without pulling in editor UI policy.

### 7.5 Material Source Contract

ED-M05 material authoring uses `MaterialSource` with schema
`oxygen.material.v1` and type `PBR`.

Supported scalar fields:

- `Name`
- `PbrMetallicRoughness.BaseColorR`
- `PbrMetallicRoughness.BaseColorG`
- `PbrMetallicRoughness.BaseColorB`
- `PbrMetallicRoughness.BaseColorA`
- `PbrMetallicRoughness.MetallicFactor`
- `PbrMetallicRoughness.RoughnessFactor`
- `AlphaMode`
- `AlphaCutoff`
- `DoubleSided`
- `NormalTexture.Scale` when a normal texture ref exists
- `OcclusionTexture.Strength` when an occlusion texture ref exists

Texture references may be preserved and displayed read-only in ED-M05. Texture
authoring/editing is not part of this primitive LLD or ED-M05.

Material round-trip preserves every `MaterialSource` field not edited by
ED-M05, including texture-reference payloads. Scalar editing must replace only
the edited immutable record branch and leave unedited descriptor data intact.

### 7.6 Import/Cook Contracts

`MaterialSourceImporter`:

- accepts `*.omat.json`.
- parses through `MaterialSourceReader`.
- emits an imported `Material` asset with virtual path derived from the source
  path, for example `/Content/Materials/Gold.omat`.
- reports invalid material JSON through import diagnostics.

`CookedMaterialWriter`:

- writes the runtime `.omat` descriptor from `MaterialSource`.
- emits scalar material values and V0.1 texture indices according to the
  current `Oxygen.Assets` writer behavior.

`LooseCookedBuildService`:

- writes or updates `container.index.bin` for mount points represented by
  imported assets.
- runs material cooking as one step inside the loose cooked build service.
- is a primitive invoked by the content-pipeline orchestrator, not directly by
  MaterialEditor UI.

## 8. Commands, Services, Or Adapters

`Oxygen.Assets` primitives are not user commands. They are called by editor
services:

| Consumer | Primitive used |
| --- | --- |
| Material document service | `MaterialSourceReader`, `MaterialSourceWriter`, `MaterialSource`. |
| Material picker | `IAssetCatalog`, `AssetQuery`, `AssetRecord`, `AssetChange`, `AssetChangeKind`. |
| Content browser identity reducer | `IAssetCatalog`, `AssetRecord`, `AssetUriHelper`, `AssetChange`, `AssetQueryScope`. |
| Content pipeline material slice | `MaterialSourceImporter`, `CookedMaterialWriter`, `LooseCookedBuildService`. |
| Geometry material slot command | `AssetReference<MaterialAsset>` and `IAssetCatalog` resolution. |

Adapters may be introduced in editor projects, but the underlying primitive
contracts stay in `Oxygen.Assets`.

## 9. UI Surfaces

None.

UI belongs to:

- `material-editor.md` for material documents.
- `content-browser-asset-identity.md` for picker/browser views.
- `property-inspector.md` for Geometry material slots.

This LLD only requires UI consumers to preserve asset URI identity and not
display raw cooked filesystem paths as the authored identity.

ED-M06 Content Browser UI consumes primitive records through an editor-owned
`ContentBrowserAssetItem` projection. That projection must not move into
`Oxygen.Assets`.

## 10. Persistence And Round Trip

Required ED-M05 persistence behavior:

- material descriptors persist as `*.omat.json` using `oxygen.material.v1`.
- `MaterialSourceWriter` output must be readable by `MaterialSourceReader`.
- scene material slots persist only the `AssetReference<MaterialAsset>.Uri`.
- missing material URIs survive scene save/reopen unchanged.
- cooked output is derived state and must not be the only copy of authored
  material values.

## 11. Live Sync / Cook / Runtime Behavior

`Oxygen.Assets` does not start runtime, mount roots, or call native interop.

Runtime-relevant behavior:

- cooked material descriptors are derived from material sources.
- loose cooked indexes expose cooked asset identities to catalogs and runtime
  mount workflows.
- full runtime preview/parity is owned by `runtime-integration.md` and
  `standalone-runtime-validation.md`.

ED-M05 may validate the minimum material cook slice by producing `.omat` and
refreshing catalog state. It must not claim full runtime parity.

## 12. Operation Results And Diagnostics

Primitive APIs may throw or return primitive diagnostics. User-facing operation
results are emitted by consuming editor workflows:

| Primitive failure | Consuming domain |
| --- | --- |
| invalid material JSON | `MaterialAuthoring` or `ContentPipeline`, depending on whether user is editing or cooking. |
| importer failure | `ContentPipeline` / `AssetImport`. |
| cooked writer failure | `ContentPipeline`. |
| missing catalog record | `AssetIdentity`. |
| loose cooked index invalid | `AssetMount` in ED-M02/ED-M07 mount flows, `ContentPipeline` in cook validation flows. |

Concrete ED-M05 diagnostic codes are allocated in
`diagnostics-operation-results.md` and implemented in `Oxygen.Core`.

## 13. Dependency Rules

Allowed:

- `Oxygen.Assets` may depend on `Oxygen.Core` and storage/import/cook support.
- editor projects may depend on `Oxygen.Assets` primitives.

Forbidden:

- `Oxygen.Assets` must not depend on WinUI.
- `Oxygen.Assets` must not depend on `Oxygen.Editor.*`.
- `Oxygen.Assets` must not call `Oxygen.Editor.Interop` or engine runtime
  services.
- primitives must not persist editor UI state, picker state, or operation
  result history.

## 14. Validation Gates

ED-M05 gates:

1. `MaterialSourceWriter` writes a descriptor that `MaterialSourceReader` reads
   with all V0.1 scalar fields preserved.
2. `MaterialSourceImporter.CanImport` accepts `*.omat.json` and rejects other
   source files.
3. `CookedMaterialWriter` produces deterministic `.omat` output for a fixed
   scalar material.
4. `GeneratedAssetCatalog` exposes
   `asset:///Engine/Generated/Materials/Default` to material picker queries.
5. `AssetReference<MaterialAsset>` persists URI while treating `Asset` as
   runtime-only.
6. `LooseCookedIndexAssetCatalog` can expose a cooked material entry from a
   valid loose cooked index.
7. `AssetReference<MaterialAsset>.Asset = null` preserves `Uri`; missing
   material identities survive save/reopen unchanged.

ED-M06/ED-M07 gates:

- ED-M06: Content Browser state reducer merges source descriptor and cooked
  index records by logical identity without changing `AssetRecord`.
- ED-M06: `AssetChangeKind.Relocated` is consumed using `PreviousUri` so cached
  browser rows do not keep stale identities.
- ED-M06: generated, source, descriptor, cooked, stale, missing, and broken
  browser states are derived outside `Oxygen.Assets`.
- ED-M06: runtime mounted availability is represented as an editor overlay, not
  as a primitive catalog fact.
- broader pak state coverage.
- scoped cook/index validation for complete scenes and dependencies.
- inspect and mount refresh workflows.

## 15. Open Issues

- Whether `AssetRecord` should later carry asset type/state metadata directly
  or remain intentionally minimal with editor-side enrichment.
