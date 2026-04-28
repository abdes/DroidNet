# Content Pipeline LLD

Status: `ED-M05 material slice implementation-ready, ED-M07 full review later`

## 1. Purpose

Define how editor workflows orchestrate import, descriptor generation, cooking,
cooked-index refresh, catalog refresh, and mount-refresh requests.

ED-M05 uses only the scalar material slice. ED-M07 owns the full content
pipeline for scenes, manifests, scoped cooking, inspect, validation, and mount
refresh.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-004` | Material descriptors can be saved and cooked as real assets. |
| `GOAL-005` | Descriptor/cook state is explicit to users. |
| `GOAL-006` | Pipeline failures are visible and actionable. |
| `REQ-014` | Scalar material values save, reopen, cook, and preview where supported. |
| `REQ-015` | Editor can generate/update descriptor inputs for supported authored content. |
| `REQ-016` | Import/cook workflows are explicit, not hidden side effects. |
| `REQ-017` | Cooked output and catalog state can be refreshed. |
| `REQ-018` | Cooked output validation is explicit before mount in full pipeline scope. |
| `REQ-019` | Project cook scope is honored by later full pipeline work. |
| `REQ-021` | Authored asset identity is distinct from cooked filesystem paths. |
| `REQ-022` | Save/cook/refresh failures surface operation results. |
| `REQ-024` | Failures identify descriptor/import/cook/index/mount cause. |
| `REQ-037` | Authored data remains repairable and round-trippable. |

## 3. Architecture Links

- `ARCHITECTURE.md`: content pipeline subsystem and Projects vs
  ContentPipeline ownership.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.ContentPipeline` owns editor tooling
  orchestration; `Oxygen.Assets` owns reusable primitives.
- `asset-primitives.md`: reusable asset identity/import/cook/index primitives.
- `material-editor.md`: material document save/cook workflow.
- `content-browser-asset-identity.md`: visible material asset state.
- `runtime-integration.md`: cooked-root mount refresh after ED-M07.

## 4. Current Baseline

Existing code already provides important primitives:

- `Oxygen.Assets` material source read/write/import/cook primitives.
- `LooseCookedBuildService` for loose cooked index updates.
- `ProjectAssetCatalog` composition over generated, filesystem, and loose
  cooked catalogs.
- runtime cooked-root refresh in workspace/runtime integration.

Brownfield gaps:

- pipeline orchestration is not yet a first-class editor service.
- material editor workflow does not yet call material cook orchestration.
- scene save still has legacy cook coupling in project services; full cleanup is
  ED-M07, not ED-M05.
- content browser state is not yet consistently linked to cook/index refresh.

Non-regression rule: ED-M05 must not add new save-time cook coupling. Existing
legacy scene-save cook paths are tolerated only as brownfield debt and removed
in ED-M07.

## 5. Target Design

The content pipeline owns orchestration over primitives:

```text
Editor command
  -> validate authored source/descriptor
  -> call Oxygen.Assets import/cook primitives
  -> update loose cooked index where in scope
  -> refresh editor catalog state
  -> request runtime mount refresh only when the milestone owns it
  -> publish operation result
```

It does not own material fields, scene mutation, project settings, or runtime
frame lifecycle.

### 5.1 ED-M05 material slice

ED-M05 implements the minimum material path:

```text
MaterialDocument.Save
  -> writes Content/Materials/*.omat.json

MaterialDocument.Cook
  -> reject if descriptor dirty
  -> import one *.omat.json as Material
  -> write .cooked/Content/Materials/*.omat
  -> update .cooked/Content/container.index.bin
  -> notify/refresh asset catalog state
  -> return material cook result
```

This slice is enough for:

- material editor status strip (`Saved`, `Dirty`, `Cooked`, `Stale`,
  `Failed`, `Unsupported`).
- material picker state changes (`Source`, `Cooked`, `Stale`, `Missing`,
  `Broken`).
- manual or editor-triggered material cook validation.

ED-M05 does **not** claim scene descriptor generation, full dependency graph
cooking, cooked-root remount, or standalone runtime parity.

### 5.2 ED-M07 full pipeline

ED-M07 expands this LLD to cover:

- scene descriptor generation for current V0.1 scene.
- procedural geometry descriptors.
- source import for supported project assets.
- cook manifests and project cook scopes.
- full dependency cooking.
- cooked output inspect/validation.
- catalog refresh after cook.
- runtime cooked-root mount refresh request.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.ContentPipeline` | editor workflow orchestration for import/cook/index/catalog refresh. |
| `Oxygen.Assets` | reusable importer, writer, catalog, loose-index, and asset identity primitives. |
| `Oxygen.Editor.MaterialEditor` | invokes material save/cook through public pipeline services. |
| `Oxygen.Editor.ContentBrowser` | displays source/cooked/stale/missing state and picker actions. |
| `Oxygen.Editor.Projects` | project root, content roots, cooked root, and cook-scope policy. |
| `Oxygen.Editor.Runtime` | mount refresh/runtime availability after ED-M07. |

## 7. Data Contracts

### 7.1 Descriptor source

For ED-M05, descriptor source is the existing Oxygen material JSON:

```text
Content/Materials/Name.omat.json
Schema = oxygen.material.v1
Type = PBR
```

The editor writes it through `MaterialSourceWriter` and reads it through
`MaterialSourceReader`. No editor-only material schema is introduced.

### 7.2 Material cook request

```csharp
public sealed record MaterialCookRequest(
    Uri MaterialSourceUri,
    string ProjectRoot,
    string MountName,
    string SourceRelativePath,
    bool FailFast);
```

Rules:

- `MaterialSourceUri` is the authored identity.
- `MountName` is the authoring mount token from the material asset URI.
- `SourceRelativePath` points to the `*.omat.json` source under a project
  content root and may differ from the mount token when a mount maps to a
  differently named folder.
- ED-M05 uses the project's default `.cooked` root through
  `LooseCookedBuildService`; material cook requests cannot redirect cooked
  output to an arbitrary root.
- output virtual path is derived from `MaterialSourceUri` and passed explicitly
  into the import input so cooked output lands under `.cooked/<MountName>/...`.
- dirty descriptors are rejected before import/cook starts.

### 7.3 Material cook result

```csharp
public sealed record MaterialCookResult(
    Uri MaterialSourceUri,
    Uri? CookedMaterialUri,
    MaterialCookState State,
    OperationResultId? OperationResultId);

public enum MaterialCookState
{
    NotCooked,
    Cooked,
    Stale,
    Failed,
    Rejected,
    Unsupported,
}
```

`CookedMaterialUri` is derived state. Scene assignment and material documents
must preserve the authored asset URI, not replace it with a filesystem path.

### 7.4 State inputs

Content-browser state enrichment combines:

- source descriptor existence and timestamp.
- cooked descriptor existence and timestamp.
- loose cooked index visibility.
- material source parse result.
- content-pipeline last operation result when available.

The state enum and UI badges are owned by
`content-browser-asset-identity.md`, not by this LLD.

## 8. Commands, Services, Or Adapters

### 8.1 `IMaterialCookService`

```csharp
public interface IMaterialCookService
{
    Task<MaterialCookResult> CookMaterialAsync(
        MaterialCookRequest request,
        CancellationToken cancellationToken);

    Task<MaterialCookState> GetMaterialCookStateAsync(
        Uri materialSourceUri,
        CancellationToken cancellationToken);
}
```

Implementation for ED-M05:

1. validate request and source path.
2. run `ImportService.ImportAsync` against the material source path. Its
   Import phase dispatches to `MaterialSourceImporter`; its Build phase invokes
   `LooseCookedBuildService` for the imported material set.
3. pass `ImportInput.MountPoint = request.MountName` and
   `ImportInput.VirtualPath = asset URI without the trailing .json`.
4. verify the cooked `.omat` and loose cooked index entry are visible under the
   active project `.cooked/<MountName>/...` root.
5. signal catalog refresh through existing catalog change mechanisms where
   available.
6. publish operation result.

### 8.2 Future full pipeline services

ED-M07 will expand or split the service surface for:

- `CookSceneAsync`
- `CookAssetAsync`
- `CookFolderAsync`
- `CookProjectAsync`
- `InspectCookedOutputAsync`
- `RefreshMountedCookedRootsAsync`

Those methods are not ED-M05 implementation requirements.

## 9. UI Surfaces

ContentPipeline owns no primary UI surface in ED-M05. It feeds:

- material editor descriptor/cook state strip.
- material picker state badges.
- operation-result details for failed/rejected cook.

Illustrative material status strip owned by MaterialEditor:

```text
Descriptor: Saved     Cooked: Stale        [Save] [Cook]
```

Failure details appear through the shared diagnostics/result surfaces, not a
separate pipeline panel in ED-M05.

## 10. Persistence And Round Trip

ED-M05 persistence:

- material source JSON is authoring truth.
- cooked `.omat` and `container.index.bin` are derived artifacts.
- source descriptor changes make cooked state `Stale` until cooked again.
- deleting cooked output never deletes authored material source.
- deleting source descriptor makes picker/material document state `Missing` or
  `Broken`, depending on whether the URI was assigned/opened and parse state.

ED-M07 persistence:

- scene descriptors, manifests, cooked indexes, and inspect reports are added
  to the full pipeline design.

## 11. Live Sync / Cook / Runtime Behavior

ED-M05:

- material cook can produce cooked output.
- geometry assignment persists the material URI through scene commands.
- scene live material override remains `Unsupported` unless the runtime API
  already supports it.
- no automatic cooked-root remount is forced by the material editor.

ED-M07/ED-M08:

- runtime mount refresh and standalone/runtime parity evidence are handled in
  their owning milestones.

## 12. Operation Results And Diagnostics

Operation kinds:

| Operation | Producer | Domain |
| --- | --- | --- |
| `Material.Cook` | Material editor/content pipeline | `ContentPipeline` |
| `Material.Save` | Material editor | `Document` |
| `Asset.Query` | content browser/picker | `AssetIdentity` |
| `Asset.Resolve` | content browser/picker | `AssetIdentity` |

Failure mapping:

| Failure | Domain | Code |
| --- | --- | --- |
| dirty descriptor cooked | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty` |
| material source missing | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.SourceMissing` |
| material JSON invalid | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.InvalidDescriptor` |
| importer/cooked writer failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.CookFailed` |
| loose cooked index update failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.MATERIAL.IndexFailed` |
| catalog refresh failed | `AssetIdentity` | `OXE.ASSETID.QueryFailed` |

ED-M05 cook success may be silent in the editor chrome if the material editor
state strip clearly changes to `Cooked`. Failures and rejections must be
visible operation results.

## 13. Dependency Rules

Allowed:

- ContentPipeline depends on `Oxygen.Assets` primitives.
- ContentPipeline depends on project services for project root/content root
  policy.
- MaterialEditor invokes ContentPipeline through public service contracts.

Forbidden:

- ContentPipeline must not own MaterialEditor UI.
- ContentPipeline must not own ContentBrowser picker UI.
- ContentPipeline must not mutate scene components directly.
- ContentPipeline must not call native interop directly.
- `Oxygen.Editor.Projects` must not regain cook execution ownership.

## 14. Validation Gates

ED-M05 gates:

1. Dirty material descriptor cook is rejected with
   `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty`.
2. Saved scalar material descriptor cooks to `.cooked/Content/.../*.omat`.
3. Cook updates the loose cooked index enough for catalog query to expose the
   cooked material URI.
4. Cook failure leaves the source descriptor intact and reports a visible
   operation result.
5. Content browser material picker can observe `Source -> Cooked -> Stale`
   state transitions for one material.
6. Material editor does not force runtime mount refresh or claim standalone
   parity.

ED-M07 gates:

- scene descriptor/manifest generation.
- scoped source import.
- full project/scene cook dependency coverage.
- cooked output inspect.
- mount refresh.
- standalone/runtime validation handoff.

## 15. Open Issues

No ED-M05 blocking open issues. Source/cooked row merging is a picker/UI
question owned by [content-browser-asset-identity.md](./content-browser-asset-identity.md);
ContentPipeline only reports the source and cooked facts it can verify.
