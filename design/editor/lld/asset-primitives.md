# Asset Primitives LLD

Status: current contract.

Read [ownership](#6-ownership), [identities and catalogs](#7-data-contracts),
[native production](#76-importcook-contracts) and [validation](#14-validation-gates).

## 1. Purpose

Define the reusable `Oxygen.Managed.Assets` primitives used by the editor for asset
identity, references, catalogs, material descriptors, import, cooked outputs,
and loose cooked indexes.

This LLD is deliberately not a content-browser or content-pipeline design. It
defines the reusable data/model/tooling layer those editor features consume.

## 2. PRD Traceability

| ID         | Coverage                                                                                |
| ---------- | --------------------------------------------------------------------------------------- |
| `GOAL-004` | Material authoring uses real material assets and descriptors.                           |
| `GOAL-005` | Asset identity, descriptor, cooked, stale, and missing states have reusable primitives. |
| `GOAL-006` | Primitive failures can be classified by consuming workflows.                            |
| `REQ-013`  | Material picker can query material assets by stable identity.                           |
| `REQ-014`  | Material values save/reopen/cook through existing material descriptor primitives.       |
| `REQ-015`  | Descriptor generation uses engine/tooling schemas where they already exist.             |
| `REQ-016`  | Import/cook operations have reusable primitives.                                        |
| `REQ-017`  | Cooked outputs can be indexed and queried.                                              |
| `REQ-018`  | Cook output can be validated before mount by later pipeline work.                       |
| `REQ-020`  | Content browser can build state views from catalog primitives.                          |
| `REQ-021`  | Authoring stores asset identity, not raw cooked path text.                              |
| `REQ-024`  | Asset failures can be routed to narrow domains.                                         |
| `REQ-037`  | Persisted authoring data survives save/reopen without manual repair.                    |

## 3. Architecture Links

- `ARCHITECTURE.md`: asset identity, content pipeline, diagnostics, and data
  contract rules.
- `PROJECT-LAYOUT.md`: `Oxygen.Managed.Assets` owns reusable asset/cook primitives;
  editor projects own workflows and UI.
- `material-editor.md`: consumes material source, material cook, and material
  identity primitives.
- `content-browser-asset-identity.md`: consumes catalog/query/reference
  primitives for picker UX.
- `content-pipeline.md`: orchestrates import/cook/index primitives.

## 4. Current Baseline

`Oxygen.Managed.Assets` supplies typed references, source and cooked catalogs,
material authoring records/readers/writers, and a current-format loose-index
metadata reader. Material authoring lives in `Authoring/Materials`; native
`Oxygen.Cooker` owns import, resource conversion, binary descriptors, indexes and
PAKs. The editor invokes native producers through ContentPipeline.

The managed layer does not decode textures or models, write cooked binaries, or
provide a second runtime loader. Native Content owns loading and residency.

## 5. Target Design

```text
Editor documents and commands
  -> Managed.Assets authoring records, references and catalogs
  -> Editor.ContentPipeline snapshot, native invocation and publication
  -> Oxygen.Cooker import, validation and packing
  -> Oxygen.Content runtime loading and residency
```

A material document saves `Content/Materials/Gold.omat.json` through
`MaterialSourceWriter`. ContentPipeline captures that saved descriptor and calls
the native material importer; native cooking writes `.omat` and its index.
Catalogs expose the resulting identity without interpreting material binaries.

## 6. Ownership

| Owner                           | Responsibility                                                                                                                 |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| `Oxygen.Managed.Assets`         | reusable asset identity, references, catalog primitives, material authoring model, and read-only current loose-index metadata. |
| `Oxygen.Managed.Core`           | shared URI and diagnostics constants used by asset workflows.                                                                  |
| `Oxygen.Editor.ContentBrowser`  | asset state enrichment, browsing, picker UX, thumbnails/swatches.                                                              |
| `Oxygen.Editor.MaterialEditor`  | material document UI and user commands over material descriptors.                                                              |
| `Oxygen.Editor.ContentPipeline` | saved input capture, native tools, diagnostics/progress, provenance and publication.                                           |
| `Oxygen.Editor.WorldEditor`     | scene commands that persist asset references in components.                                                                    |

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
- `Asset` is runtime-only and rehydrated by the consuming service.
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

| Query intent             | Contract shape                                                       |
| ------------------------ | -------------------------------------------------------------------- |
| picker/global search     | `new AssetQuery(AssetQueryScope.All)`                                |
| resolve exact assignment | `Roots = [assetUri]`, `Traversal = AssetQueryTraversal.Self`         |
| current folder view      | `Roots = [folderUri]`, `Traversal = AssetQueryTraversal.Children`    |
| recursive folder search  | `Roots = [folderUri]`, `Traversal = AssetQueryTraversal.Descendants` |

`AssetChange` is a record carrying an `AssetChangeKind`:

```csharp
public sealed record AssetChange(
    AssetChangeKind Kind,
    Uri Uri,
    Uri? PreviousUri = null);
```

| `AssetChangeKind` | Meaning                                                |
| ----------------- | ------------------------------------------------------ |
| `Added`           | asset became visible.                                  |
| `Removed`         | asset disappeared.                                     |
| `Updated`         | same identity, changed metadata/content.               |
| `Relocated`       | identity changed; `PreviousUri` carries the old value. |

Catalogs do not compute user-facing `AssetState`, `AssetKind`, or runtime
mount availability; they provide records and changes.
`content-browser-asset-identity.md` defines state enrichment.

### 7.4 Catalog Identity Limits

`AssetRecord.Uri` selects a source descriptor, cooked asset, engine recipe or
mounted source file. Optional `Generated`, `Cooked` and `OverriddenCookedSources`
metadata describe the producer or indexed representation. Cooked metadata comes
from native index records, not filename guesses.

Catalog records do not carry browser badges, dirty state, operation progress or
runtime mount state. Editor adapters combine those facts with `ProjectContext`
and immutable cooking-status snapshots. Catalog refresh does not launch native
verification or hash cooked payloads.

### 7.5 Material Source Contract

`MaterialSourceReader` and `MaterialSourceWriter` preserve the canonical native
material descriptor. The [material editor](material-editor.md) owns its editable
fields, colour conversions and HDR emission contract.

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

Texture references remain authored virtual identities. Native cooking resolves
them to resource-table entries; managed authoring does not allocate those entries.

Material round-trip preserves every `MaterialSource` field not edited by
ED-M05, including texture-reference payloads. Scalar editing must replace only
the edited immutable record branch and leave unedited descriptor data intact.

### 7.6 Import/Cook Contracts

ContentPipeline sends canonical descriptors/manifests and captured inputs to
native ImportTool, Inspector and PakTool. Native schemas, codecs and validators
are authoritative. Tools return structured diagnostics, progress and inventory;
the editor presents those facts and owns publication and source provenance.

The managed V2 index reader exposes catalog metadata and rejects obsolete formats.
Mandatory native file/asset digests support full validation at cook boundaries;
ordinary catalog reads and mounts use metadata admission. The [content pipeline
contract](content-pipeline.md) owns freshness, verification and repair policy.

Texture descriptors are resource sidecars, not keyed Script assets. A named
texture's native `virtual_path` fixes its descriptor location under the mount.
ContentPipeline records the native physical descriptor path alongside the source
association; resource-table indices remain generation-local. Native inventory
keeps resource descriptors separate from keyed asset entries. Textures use the
same capture, cooking, error/progress and publication workflow as other inputs.

## 8. Commands, Services, Or Adapters

`Oxygen.Managed.Assets` primitives are not user commands. They are called by editor
services:

| Consumer                         | Primitive used                                                                                   |
| -------------------------------- | ------------------------------------------------------------------------------------------------ |
| Material document service        | `MaterialSourceReader`, `MaterialSourceWriter`, `MaterialSource`.                                |
| Material picker                  | `IAssetCatalog`, `AssetQuery`, `AssetRecord`, `AssetChange`, `AssetChangeKind`.                  |
| Content browser identity reducer | `IAssetCatalog`, `AssetRecord`, `AssetUriHelper`, `AssetChange`, `AssetQueryScope`.              |
| Content pipeline                 | Material source records, asset URIs and current index metadata; native APIs produce cooked data. |
| Geometry material slot command   | `AssetReference<MaterialAsset>` and `IAssetCatalog` resolution.                                  |

Adapters may be introduced in editor projects, but the underlying primitive
contracts stay in `Oxygen.Managed.Assets`.

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
`Oxygen.Managed.Assets`.

## 10. Persistence And Round Trip

V0.1 persistence behavior:

- material descriptors persist as canonical engine-schema `*.omat.json`; the
  [material contract](material-editor.md) defines colour/HDR emission and the
  float32 compiled factor.
- `MaterialSourceWriter` output must be readable by `MaterialSourceReader`.
- scene overrides persist geometry identity, stable material SlotId and material
  URI; the [content contract](content-pipeline.md) defines importer continuity,
  unresolved repair and atomic publication.
- missing material URIs survive scene save/reopen unchanged.
- cooked output is derived state and must not be the only copy of authored
  material values.

## 11. Live Sync / Cook / Runtime Behavior

`Oxygen.Managed.Assets` does not start runtime, mount roots, or call native interop.

Runtime-relevant behavior:

- cooked material descriptors are derived from material sources.
- loose cooked indexes expose cooked asset identities to catalogs and runtime
  mount workflows.
- full runtime preview/parity is owned by `runtime-integration.md` and
  `standalone-runtime-validation.md`.

Native production and catalog behavior have their own tests. Full saved-source,
embedded and standalone image/semantic parity is qualified through ED-M08's
development-only targets.

## 12. Operation Results And Diagnostics

Primitive APIs may throw or return primitive diagnostics. User-facing operation
results are emitted by consuming editor workflows:

| Primitive failure          | Consuming domain                                                                           |
| -------------------------- | ------------------------------------------------------------------------------------------ |
| invalid material JSON      | `MaterialAuthoring` or `ContentPipeline`, depending on whether user is editing or cooking. |
| importer failure           | `ContentPipeline` / `AssetImport`.                                                         |
| cooked writer failure      | `ContentPipeline`.                                                                         |
| missing catalog record     | `AssetIdentity`.                                                                           |
| loose cooked index invalid | `AssetMount` in ED-M02/ED-M07 mount flows, `ContentPipeline` in cook validation flows.     |

Concrete ED-M05 diagnostic codes are allocated in
`diagnostics-operation-results.md` and implemented in `Oxygen.Managed.Core`.

## 13. Dependency Rules

Allowed:

- `Oxygen.Managed.Assets` may depend on `Oxygen.Managed.Core` and shared storage/schema support.
- editor projects may depend on `Oxygen.Managed.Assets` primitives.

Forbidden:

- `Oxygen.Managed.Assets` must not depend on WinUI.
- `Oxygen.Managed.Assets` must not depend on `Oxygen.Editor.*`.
- `Oxygen.Managed.Assets` must not call `Oxygen.Editor.Interop` or engine runtime
  services.
- primitives must not persist editor UI state, picker state, or operation
  result history.

## 14. Validation Gates

- Material source round-trip preserves edited scalars and untouched texture,
  extension and HDR emission data.
- Typed references persist URI only; clearing a cached asset preserves missing
  reference identity across save/reopen.
- Source, generated and cooked catalog queries obey exact/children/descendant
  scope and preserve precedence and relocation information.
- The current index reader rejects wrong versions, malformed ranges, ambiguous
  identities and missing required digests.
- Native cooking produces material, geometry, scene and texture outputs consumed
  through their real native readers. Named textures resolve from both materials
  and exposure-mask references and participate in incremental reuse.
- Browser state remains an editor projection; refresh/filter operations neither
  invoke native producers nor verify cooked payloads.

## 15. Closed V0.1 Decisions

AssetRecord carries identity and producer/index metadata; ContentBrowser owns
user-facing state enrichment and runtime availability overlays. URI identity
and missing-reference preservation remain the contract; file rename/move repair
UI is outside V0.1. ContentPipeline owns native import/source regeneration and publication;
[ED-M08](../plan/ED-M08-runtime-parity-and-standalone-validation.md) owns current
workflow qualification.
