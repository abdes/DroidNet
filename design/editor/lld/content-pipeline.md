# Content Pipeline LLD

Status: `Canonical V0.1 contract; implementation and qualification tracked by ED-M08`

## 1. Purpose

Define how editor workflows generate engine descriptors, run import/cook jobs,
inspect cooked output, validate loose cooked roots, refresh asset catalogs, and
request runtime mount refresh.

The editor content pipeline is an orchestration layer. It must use the engine
content pipeline schemas and APIs where they exist, and it may add narrow
`Oxygen.Editor.Interop` wrappers when a native cooker capability is available
but not yet reachable from managed editor code. It must not invent parallel
editor-only JSON schemas for runtime content.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-004` | Authored material, geometry, and scene content can produce cooked output. |
| `GOAL-005` | Descriptor, cook, inspect, validation, catalog, and mount state are visible. |
| `GOAL-006` | Pipeline failures are structured, actionable operation results. |
| `REQ-014` | Scalar material values save, reopen, cook, and remain assignable by identity. |
| `REQ-015` | Editor generates or updates engine descriptor inputs for supported authored content. |
| `REQ-016` | Scoped import/reimport uses retained sources; accepted Save/import/demand triggers and explicit Cook actions share the incremental coordinator and visible results. |
| `REQ-017` | Cooked output and catalog state refresh after cook. |
| `REQ-018` | Cooked output is validated before runtime mount refresh. |
| `REQ-019` | Project cook scope and authored mount policy are honored. |
| `REQ-021` | Authored asset identity stays distinct from cooked filesystem paths. |
| `REQ-022` | Save/cook/refresh/mount failures surface operation results. |
| `REQ-023` | Pipeline and engine/tool failures produce correlated logs. |
| `REQ-024` | Diagnostics identify descriptor/import/cook/index/mount cause. |
| `REQ-036` | Content Browser reflects refreshed descriptor/cooked states. |
| `REQ-037` | Authored source remains repairable and round-trippable. |

## 3. Architecture Links

- `ARCHITECTURE.md`: game project files, content pipeline subsystem, project
  policy, runtime/content boundaries.
- `project-layout-and-templates.md`: canonical `Content/...` layout and
  `.cooked/<Mount>/container.index.bin` output rules.
- `project-services.md`: active project context, authoring mounts, local
  mounts, and `ProjectCookScope`.
- `asset-primitives.md`: reusable asset identity/catalog/import/cook
  primitives in `Oxygen.Managed.Assets`.
- `runtime-integration.md`: runtime cooked-root mount refresh.
- `diagnostics-operation-results.md`: pipeline operation kinds, failure
  domains, and diagnostic code prefixes.

## 4. Integration Boundaries

`ContentPipelineService` coordinates scene/asset/folder/project work through
saved snapshots, native ImportTool execution, Inspector validation, provenance
and journaled fixed-root publication. `MaterialCookService` delegates to that
same service. Managed source readers/writers and catalog projections remain
reusable; they are not alternative native-format cook/publication paths.

The engine owns material, geometry, scene and manifest schemas and native
binary formats. The canonical V0.1 changes require new descriptor/record versions
for stable material slots, float32 emission, Auto/Fixed cameras and retained
Local/Inherit light/visibility semantics. Update engine-owned version constants,
producers, readers and installed schema compatibility together; recook affected
content. Normal readers reject retired formats. Useful pre-V0.1 migration is a
separate one-time development operation, not a shipping legacy execution path.

Existing integration/evidence is recorded in the milestone ledger and
[ED-M07B closeout audit](../validation/ED-M07B-closeout-audit.md). That historical
scope does not establish the newly required material-slot or rendering behavior.

## 5. Target Design

```text
Explicit Cook / successful Save or import / saved-dependency demand
  -> project coordinator and saved scope
  -> coherent input snapshot and native descriptor/manifest generation
  -> native cook into staging
  -> complete-root, reference, slot-layout and descriptor validation
  -> journaled publication and runtime refresh
  -> shared catalog status and correlated operation result
```

Scene authoring keeps its canonical editor document model; generated native
scene descriptors remain derived. Material authoring uses the canonical
engine-owned descriptor schema through a typed managed model:

```text
Content/Scenes/Main.oscene.json        # authored scene
Content/Materials/Red.omat.json        # canonical engine-schema material source
Content/Geometry/Cube.ogeo.json        # canonical geometry source when authored
.cooked/Content/Scenes/Main.oscene     # derived runtime output
.cooked/Content/Materials/Red.omat
.cooked/Content/container.index.bin
```

Derived descriptors/manifests never replace authored URI identities. Save owns
source persistence and acknowledges its captured revision independently of cook.
Only after successful Save does automatic scheduling consider the saved scope.
Session pause affects automatic scheduling, not explicit source saving. Browse
and unsaved edits do not cook. A cook never saves dirty participating documents;
it reports Needs save through the existing recovery flow.

The normal Cooking, browser, document and inspector surfaces present phase,
progress/cancel, current/stale status and actionable failures. Catalog refresh
and mount refresh occur only through the validated publication authority.
Failed staging remains inspectable without becoming published current content.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.ContentPipeline` | Editor orchestration service, descriptor adapter coordination, import/cook requests, inspect/validate adapters, catalog refresh, operation results. |
| `Oxygen.Managed.Assets` | Managed reusable import/cook/index primitives and editor-side readers/writers. |
| `Oxygen.Cooker` / engine content API | Native schemas, manifest batch import, scene descriptor import, loose cooked inspection/validation, runtime-compatible descriptors. |
| `Oxygen.Editor.Interop` | Narrow managed wrappers for native cooker inspection/validation/import APIs when ED-M07 needs native behavior that is not exposed in managed code. |
| `Oxygen.Editor.Projects` | Project root, authoring mount facts, local mount facts, default cooked output root, and validation policy. |
| `Oxygen.Editor.WorldEditor` | Scene document ownership and user commands that invoke pipeline workflows. |
| `Oxygen.Editor.ContentBrowser` | Presents refreshed source/descriptor/cooked/stale/broken/mounted state; does not cook. |
| `Oxygen.Editor.Runtime` | Runtime mount/unmount calls through `IEngineService`; does not decide cook scope. |

`Oxygen.Editor.Projects` must not regain cook execution ownership.

## 7. Data Contracts

### 7.1 Cook Scope

```csharp
public sealed record ContentCookScope(
    ProjectContext Project,
    ProjectCookScope CookScope,
    IReadOnlyList<ContentCookInput> Inputs,
    CookTargetKind TargetKind);

public enum CookTargetKind
{
    CurrentScene,
    Asset,
    Folder,
    Project,
}
```

Rules:

- `ProjectContext` supplies authoring mount projection.
- `ProjectCookScope.CookedOutputRoot` supplies the default `.cooked` root.
- `Inputs` are authored asset identities and source paths, never cooked paths.
- folder/project cooks expand to inputs under authoring mounts only; derived
  roots, config, packages, and browser presentation roots are not cook inputs.
- Refresh operation/status presentation on terminal results. New product catalog
  state becomes current only after committed publication; runtime refresh occurs
  inside its validated transaction. These are not caller-controlled bypass flags.

### 7.2 Cook Input

```csharp
public sealed record ContentCookInput(
    Uri AssetUri,
    ContentCookAssetKind Kind,
    string MountName,
    string SourceRelativePath,
    string SourceAbsolutePath,
    string? OutputVirtualPath,
    ContentCookInputRole Role);

public enum ContentCookInputRole
{
    Primary,
    Dependency,
    GeneratedDescriptor,
}
```

`OutputVirtualPath` is the runtime virtual path without `.json`, for example
`/Content/Materials/Red.omat` or `/Content/Scenes/Main.oscene`.

### 7.3 Scene Descriptor Adapter

`ISceneDescriptorGenerator.GenerateAsync` emits the current engine-owned scene
schema from a captured canonical scene document and resolved dependency scope.
It returns the generated descriptor path/virtual identity, dependencies and
complete diagnostics. Development identity-map construction consumes its stable
traversal through a development adapter; no qualification protocol is embedded
in this production service.

| Authoring fact | Required native descriptor/record content |
| --- | --- |
| Node identity/order/hierarchy | Deterministic depth-first records and parent indices; duplicate names are not identities. |
| Local TRS | Captured position, quaternion and scale without silent repairs. |
| Scene Visibility / geometry Cast and Receive Shadows | Each stored Local/Inherit mode and local value; never flatten inherited intent into the current effective boolean. |
| Geometry URI | Canonical geometry key/path and its native slot inventory. |
| Instance material assignments | Explicit SlotId-keyed overrides and resolved material keys; every supplied slot/LOD binding is supported. No single material_ref or positional-only override path. |
| Perspective camera | Auto/Fixed mode, stored Fixed ratio, vertical FOV, near/far and parented pose. Auto ratio/content rectangle is derived per view, not saved on resize. |
| Directional light | Complete selected scalar/common/shadow values and canonical AtmosphereLightSlot None/Primary/Secondary. |
| Atmosphere roles | Per-light assignment only; uniqueness checked across stored lights including hidden/off ones. No SunNodeId, IsSunLight/Contributes duplicate authority or automatic promotion. |
| Scene environment | Complete canonical atmosphere, captured-sky diffuse/specular, exposure and appearance data from the owning field tables. |
| Background | Canonical display-background colour semantics, independent of scene exposure/tone mapping while preserving foreground transparency. |
| Editor Hide, loaded IsActive, selection/gizmos | Editor/runtime-derived state, excluded from authored runtime content. |
| Unsupported required values | Actionable failure before publication; no best-effort omission or substituted defaults. |

The engine owns the changed schema and binary versions. Do not continue emitting
old scene record layouts that cannot represent the selected contracts. Qualified
imports reject excluded components instead of reporting success after dropping
them. Reject an empty scene and invalid clipping/enum/role/slot data with the
scene and field in the diagnostic.

Native descriptor names derive from the scene file stem: remove `.oscene.json`,
replace characters outside `[A-Za-z0-9_.-]` with `_`, prefix `_` if the first
character is not `[A-Za-z0-9_]`, then limit to 63 characters. An empty result fails.
Display names and original paths remain intact in authoring and diagnostics.

| Authoring URI | Native reference |
| --- | --- |
| `asset:///<Mount>/<Path>.omat.json` or `.omat` | `/<Mount>/<Path>.omat` |
| `asset:///<Mount>/<Path>.ogeo.json` or `.ogeo` | `/<Mount>/<Path>.ogeo` |
| `asset:///<Mount>/<Path>.oscene.json` | `/<Mount>/<Path>.oscene` |

Reject an unknown mount, wrong scheme/type, traversal or invalid normalization.
Native references contain neither `asset:///` nor an authoring `.json` suffix.
Opaque native keys come from engine inspection/identity APIs, not a second
managed hash implementation.

### 7.4 Procedural Geometry Descriptors

Generated/editor procedural geometry references, such as basic shapes, are
resolved before scene cook:

```csharp
public interface IProceduralGeometryDescriptorService
{
    Task<IReadOnlyList<ContentCookInput>> EnsureDescriptorsAsync(
        ContentCookScope scope,
        IReadOnlyList<Uri> geometryUris,
        CancellationToken cancellationToken);
}
```

Rules:

- authored scene files keep the geometry asset URI.
- Use derived procedural geometry descriptors under
  `<ProjectRoot>/.pipeline/Geometry/<StableName>.ogeo.json` and adds
  `geometry-descriptor` jobs to the manifest for those descriptors.
- generated descriptors are derived files and are not shown as authored user
  files unless the Content Browser already presents them as generated assets.
- a scene renderable must not silently cook with an all-zero geometry key.
- missing or unsupported geometry produces a failed scene cook diagnostic.

### 7.5 Import Manifest

Use an explicit manifest model that can be serialized to the native
`oxygen.import-manifest.schema.json` shape:

```csharp
public sealed record ContentImportManifest(
    [property: JsonPropertyName("version")]
    int Version,
    [property: JsonPropertyName("output")]
    string Output,
    [property: JsonPropertyName("layout")]
    ContentImportLayout Layout,
    [property: JsonPropertyName("jobs")]
    IReadOnlyList<ContentImportJob> Jobs);

public sealed record ContentImportLayout(
    [property: JsonPropertyName("virtual_mount_root")]
    string VirtualMountRoot);

public sealed record ContentImportJob(
    [property: JsonPropertyName("id")]
    string Id,
    [property: JsonPropertyName("type")]
    string Type,
    [property: JsonPropertyName("source")]
    string Source,
    [property: JsonPropertyName("depends_on")]
    IReadOnlyList<string> DependsOn,
    [property: JsonPropertyName("output")]
    string? Output,
    [property: JsonPropertyName("name")]
    string? Name);
```

The manifest writer must serialize the native schema property names shown
above. If the implementation does not use `JsonPropertyName` attributes on the
records, it must use a dedicated manifest writer that produces the same
snake_case schema shape.

Mount layout rules:

- Each authored mount cooks to its own physical loose root:
  `<ProjectRoot>/.cooked/<MountName>`.
- The manifest `output` is that physical loose root.
- The manifest `layout.virtual_mount_root` is `/<MountName>`.
- Native descriptor references and cooked-index virtual paths use the same
  `/<MountName>/...` prefix.
- Example for the built-in content mount:
  - `output = <ProjectRoot>/.cooked/Content`
  - `layout.virtual_mount_root = /Content`
  - material descriptor URI `asset:///Content/Materials/Red.omat.json`
    becomes native path `/Content/Materials/Red.omat`
  - cooked index entries must round-trip back to
    `asset:///Content/Materials/Red.omat`
- Do not depend on a native default `/.cooked` virtual root or on
  cooked filesystem paths as asset identity.

Required job type mapping:

| Input | Manifest job type |
| --- | --- |
| scalar material descriptor `*.omat.json` | `material-descriptor` |
| procedural/imported geometry descriptor `*.ogeo.json` | `geometry-descriptor` |
| generated native scene descriptor | `scene-descriptor` |
| glTF/GLB source media import | `gltf` |
| FBX source media import | `fbx` |
| texture source or descriptor | `texture` or `texture-descriptor` |

Scene jobs depend on the material and geometry jobs needed by their
renderables. Folder/project cook generates a manifest and validates it against
the native `oxygen.import-manifest.schema.json` contract before execution.
Use the shared schema-aligned validator for the accepted V0.1 manifest
subset; native builder/import construction is a secondary check for the chosen
adapter, not a substitute for schema validation.

### 7.6 Cook Result

```csharp
public sealed record ContentCookResult(
    Guid OperationId,
    CookTargetKind TargetKind,
    OperationStatus Status,
    IReadOnlyList<DiagnosticRecord> Diagnostics,
    IReadOnlyList<ContentCookedAsset> CookedAssets,
    CookInspectionResult? Inspection,
    CookValidationResult? Validation);

public sealed record ContentCookedAsset(
    Uri SourceAssetUri,
    Uri CookedAssetUri,
    ContentCookAssetKind Kind,
    string MountName,
    string VirtualPath);
```

The existing result also carries InputSnapshot, InputsAreCurrent, ReusedAssets,
IsUpToDate, IsPublished and IsMounted. Preserve these facts on every typed caller;
successful staging alone cannot set IsPublished/IsMounted. A no-op result reuses
verified products without claiming a new native production operation.

`Diagnostics` is the complete workflow diagnostic set for the command result:
descriptor-generation warnings/errors, missing source diagnostics, native import
failures, and cooked-output validation diagnostics. UI producers must publish
this list directly; they must not reduce the result to validation diagnostics
only.

### 7.7 Inspect And Validate

Use the installed native Inspector and native schemas through the existing
process adapter. Reports contain container identity, asset keys/types/paths,
protected files/digests, dependency information and canonical slot inventory.
Cache only verified reports against their producer/schema/container fingerprints.
Managed feature code does not decode binary geometry/material/scene structures.

Failures return complete scoped diagnostics with native codes/messages; invalid
inspection cannot authorize reuse or publication. Validate every preserved and
new descriptor/reference in the candidate generation, including SlotId bindings
and physical-path uniqueness. Successful file hashes alone do not prove that a
source material or slot was preserved.

## 8. Commands, Services, Or Adapters

### 8.1 Main Service

Use the existing `IContentPipelineService` operations for current scene, asset,
folder and project cooking, retained import/reimport, inspection and validation.
Every target uses the same scope, snapshot, native execution, publication and
status machinery. MaterialCookService is a typed caller of CookAssetAsync, not
an independent writer. Invalid/missing slot identity is scoped to its affected
consumer closure, as specified in section 20.

### 8.2 Engine API Adapter

`ImportToolContentPipelineApi` is the normal native-process adapter, backed by
the owned `ContentPipelineProcessRunner`, matched tool/schema discovery and
immutable operation inputs. Native ImportTool performs cooking; native Inspector
performs supported inspection/validation. This is the established production
path, not a temporary fallback awaiting an in-process cooker.

Keep native exceptions, exit failures, parser/schema failures and termination
failures inside classified operation results with retained diagnostics. Preserve
worker/descendant and input/output ownership until I/O drains, including failed
termination (section 18). Do not reintroduce managed binary writers or an
alternative in-process publication path. Runtime Interop does not own cooking.

### 8.3 Catalog And Runtime Refresh

Section 16 owns staging validation, root installation, runtime refresh, receipt
commit and rollback. Publish current catalog/provenance state only after that
transaction commits. A validated staging result does not mark new content current.
The workspace resynchronizes its current authoring scene through the normal
Runtime/SceneEngineSync boundary; document saves and catalog-only refreshes cannot
publish or remount roots independently. Inspect-only operations never mount.

## 9. UI Surfaces

Use the existing UI surfaces:

| Surface | Actions |
| --- | --- |
| Content Browser toolbar/context menu | `Cook`, `Cook Folder`, `Inspect Cooked Output`, `Validate Cooked Output`, `Refresh` |
| Scene document/header/menu | `Cook Current Scene` |
| Material editor | continues to expose `Cook` for one material |
| Output/log panel | correlated cook phase summaries and diagnostics |

Illustrative result summary:

```text
Cook Current Scene: Main
Generated descriptor: Content/Scenes/Main.oscene.json -> .pipeline/...
Cooked: 1 scene, 1 material, 1 geometry
Validated: .cooked/Content/container.index.bin
Mounted: Content
```

Use the existing Cooking panel, correlated logs and compact document/browser
status. Preserve per-phase progress and cancellation; do not create another
progress subsystem or a separate qualification dashboard.

## 10. Persistence And Round Trip

Persisted authored truth:

- editor scene files under `Content/Scenes/*.oscene.json`.
- material descriptors under `Content/Materials/*.omat.json`.
- authored/imported geometry/material/source media under authoring mounts.
- project manifest content-root and cook-scope facts.

Derived artifacts:

- native scene descriptors generated from editor scenes.
- generated procedural geometry descriptors.
- import manifests.
- `.cooked/<Mount>/...` runtime output.
- `.cooked/<Mount>/container.index.bin`.
- optional inspect reports.

Derived artifacts must not be used as authored identity in scene files,
material slots, project manifests, or recent documents.

## 11. Live Sync / Cook / Runtime Behavior

- Automatic and explicit cooks use the same coordinator and stage output while
  runtime/authoring may continue. Preview
  briefly pauses for the publication transaction in section 16.
- Runtime cooked-root refresh happens only after cooked output validation
  succeeds.
- Scene save, material save/cook, Content Browser import/cook, and catalog-only
  refresh paths must not publish unvalidated cooked-root refresh messages.
  Save-time scheduling must never bypass this publication authority.
- Runtime publication uses the section 16 transaction; mount failure reports
  `AssetMount`, restores prior output, and never reports the new output current.
- Embedded runtime refreshes mounted cooked roots only through validated publication.
- Opt-in development tools own standalone parity and visual-equivalence evidence.
- Stable multi-viewport operation remains outside this release scope.

## 12. Operation Results And Diagnostics

Operation kinds:

| Operation | Producer | Domain |
| --- | --- | --- |
| `Content.Descriptor.Generate` | scene/procedural descriptor generators | `ContentPipeline` |
| `Content.Manifest.Generate` | manifest builder | `ContentPipeline` |
| `Content.Import` | content pipeline / engine API adapter | `AssetImport` |
| `Content.Cook.Asset` | content pipeline | `AssetCook` |
| `Content.Cook.Scene` | content pipeline | `AssetCook` |
| `Content.Cook.Folder` | content pipeline | `AssetCook` |
| `Content.Cook.Project` | content pipeline | `AssetCook` |
| `Content.CookedOutput.Inspect` | inspect adapter | `ContentPipeline` |
| `Content.CookedOutput.Validate` | validation adapter | `ContentPipeline` |
| `Content.Catalog.Refresh` | catalog refresh adapter | `AssetIdentity` |
| `Runtime.CookedRoot.Refresh` | workspace/runtime integration | `AssetMount` |

Failure mapping:

| Failure | Domain | Code |
| --- | --- | --- |
| scene descriptor generation failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.SCENE.DescriptorGenerationFailed` |
| required authored scene value cannot be represented; publication blocked | `ContentPipeline` | `OXE.CONTENTPIPELINE.SCENE.UnsupportedField` |
| procedural geometry descriptor failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.GEOMETRY.DescriptorGenerationFailed` |
| manifest generation failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.MANIFEST.GenerationFailed` |
| source path missing | `AssetImport` | `OXE.ASSETIMPORT.SourceMissing` |
| importer returned diagnostics | `AssetImport` | `OXE.ASSETIMPORT.ImportFailed` |
| cook failed | `AssetCook` | `OXE.ASSETCOOK.CookFailed` |
| cooked index missing or invalid | `AssetCook` | `OXE.ASSETCOOK.IndexInvalid` |
| inspect failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.INSPECT.Failed` |
| validation failed | `ContentPipeline` | `OXE.CONTENTPIPELINE.VALIDATE.Failed` |
| catalog refresh failed | `AssetIdentity` | `OXE.ASSETID.RefreshFailed` |
| mount refresh failed | `AssetMount` | `OXE.ASSETMOUNT.RefreshFailed` |

Import/cooker native diagnostic codes must be preserved in technical details
and adapted to the nearest editor diagnostic code.

## 13. Dependency Rules

Allowed:

- ContentPipeline depends on `Oxygen.Managed.Assets`.
- ContentPipeline depends on project services for project/cook scope facts.
- ContentPipeline may depend on `Oxygen.Editor.Interop` through
  `IEngineContentPipelineApi` for native cooker APIs.
- WorldEditor, MaterialEditor, and ContentBrowser invoke ContentPipeline
  service contracts.
- Runtime integration consumes validated mount refresh requests.

Forbidden:

- Save must not execute a cook inline or publish output; successful persistence may schedule saved-scope work through the shared coordinator.
- ContentPipeline must not mutate scene authoring state.
- ContentPipeline must not own Content Browser item layout or picker UX.
- ContentPipeline must not persist cooked paths into authoring files.
- Project services must not execute cook/import.
- Runtime services must not generate descriptors or decide cook scope.
- Stable multi-viewport behavior is outside this production scope.

## 14. Validation Gates

The following production paths require regression coverage:

1. `Cook Current Scene` generates a native `oxygen.scene` descriptor from the
   editor scene with transform, geometry, material slot, perspective camera,
   directional light, and supported environment data.
2. procedural geometry references needed by the scene produce descriptor/key
   inputs or fail with visible diagnostics.
3. cooking the current scene cooks its referenced V0.1 material and geometry
   dependencies or reports which dependency blocked the cook.
4. cooked output contains `.cooked/Content/container.index.bin` and cooked
   `.oscene`, `.omat`, and required `.ogeo` entries for the test scene.
5. inspect output shows asset entries and file entries for the cooked root.
6. validation runs before runtime mount refresh; failed validation blocks mount
   and publishes a visible operation result.
7. successful validation refreshes Content Browser asset states without editor
   restart.
8. successful validation requests runtime cooked-root refresh and reports mount
   failure as `AssetMount`, not as a cook failure.
9. dirty save-time cook coupling does not reappear in project services.
10. scene/material save, material cook, Content Browser import/cook, and
    catalog-only refresh paths no longer publish unvalidated cooked-root
    refresh messages.
11. URI normalization tests prove `.omat.json`/`.ogeo.json` authored asset URIs
    become native `.omat`/`.ogeo` descriptor paths and reject invalid inputs.
12. manifest layout tests prove `.cooked/Content` uses virtual root `/Content`
    and cooked-index virtual paths round-trip to `asset:///Content/...`.
13. scene descriptor name tests prove display/file names with spaces normalize
    to native schema identifiers without changing authored display names.
14. environment mapping covers every canonical authored field; a missing required mapping fails publication without partial native payloads.
15. Opt-in development qualification verifies native/editor parity without adding a production dependency.

## 15. Scheduling And Product Scope

The [content workflow contract](content-cooking-workflows.md) specifies shared
status, incremental reuse, browser/picker behavior and recovery. Schedule cooking
after successful Save/import and on asset/active-scene demand, with session pause. Browse and
transient-edit events do not cook; external-source reimport stays explicit.

V0.1 retains existing Cook Asset/Folder/Scene/Project actions to rebuild stale
content explicitly through the same coordinator. No separate stale-only scheduler,
generic project-settings panel, renderer-preset selector or descriptor/manifest
editor/launcher is required. Source/generated paths are visible and copyable in content/result
information; Inspect and Validate operate on cooked products. Project mounts
supply cook policy; scene settings supply render intent. PRD section 8 and the
current field tables define the supported surface.

## 16. Saved Inputs And Publication Transaction

Fixed published paths remain `.cooked/<Mount>/container.index.bin` and
companions. Every producer uses this transaction. Development qualification
consumes its publication/provenance/read leases through an opt-in adapter; it
does not add a second writer or shipping qualification workflow.

### Input Capture And Serialization

- ContentPipeline is the sole writer/coordinator for scene, material, asset,
  folder, and project cooks. Existing material helpers execute through it and
  cannot publish independently. One cook/publish operation per project runs at
  a time; a later request waits or can be cancelled, and captures inputs only
  when it obtains the project operation gate.
- Complete/cancel an active edit gesture first. Reject a cook if any open scene
  or material document in the dependency closure is dirty. Name the documents
  and offer the ordinary Save workflow, including explicit Save listed and Cook;
  never save implicitly. Automatic requests surface Needs save without a modal.
- Resolve the saved dependency closure, including importer settings and source
  media, then capture a coherent byte snapshot under document/read coordination.
  Read participating files through handles that exclude concurrent writes while
  their bytes are copied. If the set changes while dependencies are resolved,
  retry capture at most three times, then fail visibly. Do not guess a mixture
  of revisions. External inaccessible/conflicting files fail before cooking.
  Optional import settings record absence as an input fact; settings created
  during discovery/capture cause rediscovery. Discovery includes geometry buffer
  files and separates engine recipes and cooked-only references from authored
  inputs. Cooked-only references require publication validation and output leases
  before reuse; their presence in the graph does not establish freshness.
- Normal cooking copies inputs to `.build/cook/<OperationId>/inputs`, preserving their logical
  mount-relative relationships. Record document saved revisions where known,
  source hashes, import settings, schema/build fingerprint, project lifetime,
  target scope, and operation identity. Native jobs read this private snapshot.
  `ContentImportExecution` supplies the input root, operation directory and
  coordinator operation ID separately from the native manifest's output root.
  The adapter must not infer input paths from the published directory layout.
  Temporary manifests remain in the operation directory until its worker drains.
- Authoring may continue after capture. A later edit or source change marks the
  result stale relative to current authoring, while a successful cook of the
  captured input remains a successful, identifiable historical result.

The saved-snapshot core accepts a caller-owned canonical private input directory
and the existing input registry/read gates. `CookInputSnapshotCapture` keeps the
normal cooking destination above; development preparation supplies
`<EvidenceRoot>/<OperationId>/inputs`. Both use the same closure discovery,
coherent copy and hash implementation. Validate directory ownership, reparse
boundaries and non-overlap with authored/published roots before writing. This is
an ordinary destination-parameterized snapshot capability: no qualification
protocol types, field expectations or validation-specific path policy enter the
production API.

### Staging And Validation

- Write native output to `.build/cook/<OperationId>/output/<Mount>` on the same
  volume as the published root. The manifest's physical output is staging;
  virtual paths remain `/<Mount>/...` and authoring URIs remain unchanged.
- For a partial asset/folder cook, seed staging from the previous published
  root and replace the selected assets and dependencies. Preserve unrelated
  entries. Track generated subassets so reimport removes superseded outputs
  from that source without deleting unrelated assets or changing stable IDs.
- Run native inspect, complete-root validation, reference/dependency validation,
  and all required descriptor-field checks on staging. Unsupported required
  V0.1 content is an error, not a successful result with omitted values.
- Before publication, ensure the operation still owns the project lifetime and
  publication gate. A closed/replaced project invalidates the operation. Failure
  or cancellation here leaves published output and preview untouched.

### Publication And Rollback

1. Record the complete affected-root set and prior publication metadata in a
   durable transaction journal at `.build/cook/<OperationId>/publication.json`.
   Use explicit states Prepared, OldRetained, RootsInstalled, RuntimeReady,
   Committed, and RolledBack. Each filesystem step is recoverable from the journal.
   The journal retains prior and proposed bytes for both `.cooked/publication.json`
   and `.build/cook/provenance.json`, so a failed generation cannot leave its
   product cache ahead of the restored roots. Recovery resolves mount names within
   the owning project rather than trusting paths stored in the journal.
2. Announce `Publishing cooked content` and suspend preview at a runtime boundary.
   The runtime drains requests/reads using affected roots and releases conflicting
   file handles. UI and authoring remain responsive. An external content
   reader holds a project-output lease; publication waits for its release or reports
   busy, without terminating another process or writing through its lease.
   A registration gate and OS-held reader markers under `.build/cook/readers`
   coordinate processes. The publisher holds the gate through replacement and
   registers the resumed runtime's reader before releasing it. A marker is
   reclaimable only after its file can be opened exclusively; a recorded PID or
   an exit signal alone does not establish released file ownership.
   Asynchronous readers and publishers wait cancellably for the registration
   gate using non-throwing contention checks. Finite staging and metadata reads
   register inspection markers; persistent native readers remain distinct.
   Verification reuses its caller's lease throughout nested journal, metadata
   and root reads, so a waiting publisher cannot deadlock a finishing inspection.
3. Retain previous roots under the same operation's `previous/<Mount>` directory,
   then install validated staging roots at the fixed `.cooked/<Mount>` locations.
   Same-volume directory renames and a journal protect replacement; all affected
   roots form one logical transaction. A partial swap cannot become published.
4. Mount the complete validated root set through the normal runtime service.
   Resynchronize the current active authoring scene, with its current document
   lifetime and revision. Retain pending/newer edits and show their stale-material
   state where their source is newer than the captured cook.
5. Atomically publish `.cooked/publication.json`: operation ID, project identity,
   input revisions/hashes, schema/build fingerprint, output/index hashes, root
   set, and completion time. Refresh catalog rows from that publication and
   resume preview; only then report full publication success. With no live
   runtime, record validated disk publication and `NotMounted`, never `Mounted`.
6. If replacement/remount fails, restore all prior roots and metadata, remount
   the prior validated set, and resume. Report publication failure independently
   from successful staging/cook. If restoration/remount also fails, retain the
   journal and backups and leave preview visibly unavailable; never report the
   new set current or destroy the last recoverable validated output.
7. Recovery runs before new cook or runtime mount on project reopen. An
   uncommitted journal restores the prior complete root set. A committed journal
   verifies the published set before mounting. Missing/invalid recovery material
   produces a blocking pipeline diagnostic with preserved files. Cleanup removes
   only operation-owned staging/backups after commit/rollback and released leases.

Cancellation before publication drains the worker and ends as Cancelled without
changing published output. Cancellation during publication is deferred until
commit or rollback reaches a safe state; the final result reports what actually
happened. The UI may show Cancelling, but a cancellation token alone is not proof
that native I/O stopped. Publication is also valid when preview is unavailable;
no engine startup is required merely to cook saved source.

### Result And UI Contract

Extend the pipeline result with input provenance, the captured/current freshness
comparison, publication state, and mount state. Distinguish CookFailed,
ValidationFailed, PublicationFailed, MountFailed, Cancelled, and successful
publication in structured diagnostics. A staged success plus failed publication
is partial success for the composite workflow, never a usable new mounted cook.
Copyable generated descriptor/manifest paths are diagnostic affordances; they
are not authored identities or required manual repair steps.

### Publication Validation Gates

- [ ] Dirty dependencies reject capture; later edits do not mutate the captured
  bytes or get cleared by cook completion. Overlapping requests serialize.
- [ ] Asset/folder recook preserves unrelated published entries and stable
  source/subasset identity; deleted generated subassets are reconciled.
- [ ] Inspect and validation run on staging before any published file changes.
- [ ] Failure/cancellation at every staging and publication boundary preserves
  or restores the prior complete output; recovery handles interruption between
  each pair of journaled steps, including first publication with no prior root.
- [ ] Preview pause/drain/resume, active-scene change, runtime fault, failed new
  mount, failed rollback mount, and standalone reader leases are exercised.
- [ ] Input/output hashes and publication state are queryable in results;
  stale/newer authoring never becomes falsely current. User workflows need no
  manual generated-file edits.

## 17. Qualified Import And Reproduction Policy

V0.1 qualifies glTF 2.0 (`.gltf`/`.glb`) and FBX through the existing native
importers, restricted to static triangle geometry, node-local transforms, normals,
UVs when present, and material groups assignable to supported scalar materials.
The qualification fixtures include one small self-contained source of each
format. They use explicit source units/axis metadata and the engine importer's
canonical Oxygen conversion; the import report records effective units, handedness,
axis conversion, winding, and resulting bounds. Unknown/ambiguous FBX units or
axis metadata is rejected rather than guessed. A unit cube, oriented triangle,
and nonuniform transform fixture prove each conversion.

Perspective cameras and directional lights are included under REQ-009. Map
explicit source camera ratios to Fixed and omitted ratios to Auto; preserve
valid FOV/clipping. Per-light atmosphere assignment uses the canonical
None/Primary/Secondary contract; ambiguous old sun combinations require explicit
migration repair, not an automatic selection.
Orthographic cameras, point/spot lights, animation, skinning, morphs, physics
and scripts do not enter the qualified scene. Detect such required content before
publication and return a precise unsupported-content result while preserving the
source. Import must not appear successful by silently dropping those features.
Existing imported texture references may be preserved/read-only, but texture
creation/editing and general textured-import qualification are outside V0.1.
Texture-bearing imports are rejected by the qualified scalar-only entry point
with an explanation; existing read-only references are not stripped on save.

The native scene import request carries `SceneContentPolicy::kStaticScalar`;
manifest scene options select it with `content_policy: "static-scalar"`.
The editor persists and supplies that policy on initial import and reimport.
Both native adapters validate the parsed source before creating pipeline work,
including when loading captured in-memory inputs. Ordinary engine imports keep
the default policy. This is part of the existing ImportTool/native transaction,
not a separate qualification build or startup operation.

`Oxygen.Cooker`'s `InspectSceneSource` and ImportTool `inspect-source` report parsed
source counts, native coordinate conversion facts, supported-content diagnostics
and decoded external buffer paths through the versioned scene-source-inspection
schema. Inspection emits no cooked content and does not load external glTF
buffers; supported FBX has no external scalar/geometry dependencies. The managed
adapter validates the matched schema and retains worker/artifact ownership
through cancellation and termination failure.

Coherent bundle discovery inspects a private copy of the primary file and
uses that copy's hash when capturing the complete original file set. Relative
paths stay relative to the primary source; embedded data needs no extra file.
Missing references are reported by retention, and unsupported source content
cannot enter publication. Each discovery attempt supplies its primary-relative
path with the complete file set, so a retry can change the bundle's common root.
Retained model bundles live under `Content/SourceMedia/DCC` through the declared
Content mount. Operation ownership and the private primary copy survive a failed
worker termination until native drain and cleanup complete. The report destination cannot overwrite the source,
its hard-link alias, or a declared buffer.

Native retained settings use the existing `<source>.import.json` naming
convention with `SchemaVersion: 3` for new imports and
`Importer: "Oxygen.Cooker.Scene/v1"`.
The sidecar records the Content bundle/primary paths, initially discovered files
and primary hash, output mount and exclusive destination, and explicit native
content/unit/normal/tangent/transform policies. Retired sidecars require the one-time migration tool and cannot silently replace
existing identities. Canonical retained settings also carry the engine-owned
slot identity provenance defined in section 20. Creation uses the
ordinary atomic file store with a missing-file baseline.

New imports follow the existing type folders: `/Content/Materials/<model>`,
`/Content/Geometry/<model>` and `/Content/Scenes/<model>`. The default review
starts at the selected authoring mount's root, never at the selected Scenes or
Materials folder. An optional relative group is applied within each type folder;
the dialog shows all resulting paths before acceptance. Native mesh/material
names already include the source-file namespace, which is not repeated when it
matches the model folder. Bulk resource tables remain native-owned shared files.

One-time migration normalizes useful older sidecars/output paths and their
authored references together, then rebuilds affected content. Production reimport
accepts only the canonical settings/layout; it does not retain version-2 grouped
path behavior. Changing a sidecar alone cannot relocate content because native
asset keys derive from virtual paths. Ownership,
collision checks, folder cooking and source inspection cover every disjoint
output namespace, without claiming the whole mount for one imported model.

Retained model jobs use the same captured inputs, incremental planner, staging
and journaled root publication as descriptors. Native rediscovery runs when a
changed primary has no matching published dependency layout; successful product
provenance retains that layout so an unchanged repeat/reopen starts no worker.
Folder/project scopes include configured retained models. Native batch reports
identify the actual emitted files and structured issues; seeded old files cannot
be claimed as new outputs. Existing source ownership and native identities are
checked before publication. Identity-changing replacement remains blocked with
an actionable result; failed imports preserve published roots.

Resolve imported output requests through their retained source ownership, not a
guessed JSON descriptor. Verified provenance supplies exact prior output owners;
saved native import settings supply exclusive output namespaces when derived
metadata is absent. Namespace ownership identifies work to run, not proof that a
particular named asset exists. Asset and scene requests retain the exact output
URI and require that output in the verified emitted/reused set before publication.
Folder cooks include sources whose output namespace intersects the selected
folder. Capture and freshness checks use the same resolved source scope. Read-only
status uses saved settings and prior output evidence without native discovery.

Descriptor jobs use the engine's ordered `cooked_context_roots` lookup context
for other staged project mounts and leased cooked libraries. Cook dependencies
before consumers across mount boundaries, use staged roots for affected mounts,
and retain published roots for reused dependencies. Reference lookup follows the
saved content priority; explicitly listing the destination root places it at that
position, with the last occurrence of a duplicate root winning. The highest
priority matching asset must have the requested type; a lower-priority asset must
not hide a type error in the effective source. Check each root's index and newly
written descriptors before proceeding to the next root; index publication must
not change the selected source.

Cooked-library dependencies retain the selected source, native key/type and a
fingerprint of its protected container files. These facts participate in consumer
freshness and the publication receipt. Library inputs remain read-only and leased
through native work, publication and failed-worker drain. Source priority is
resolved from the saved project order for both cooking and preview; no library is
copied into project authoring or claimed as a newly produced project asset.

Dependency inspection uses the existing native Inspector in metadata-only mode.
Cache its versioned report under `.build/cache/cooked-dependencies-v1`, keyed by
the verified container-file fingerprint. Validate the report schema, cached-report
digest, container identity and indexed asset keys/types/paths before reuse.
Missing or damaged cache entries require inspection; they cannot prove freshness.
Cooking and mount preparation populate cache misses while retaining library and
native-tool readers through worker completion or failed-termination drain.
Status reads consume cached facts without starting native processes. A known
changed library remains Out of date while further inspection is pending.
Catalog initialization and changes queue metadata refresh behind the existing
project writer. Browser rows remain available while it runs, and a completed
cache update refreshes their status. This work creates no Cooking-panel run;
unchanged catalog revisions and verified cache hits launch no extra inspection.
Project changes cancel obsolete work and schedule the latest catalog revision.

Follow asset-key dependencies across declared libraries using the saved source
order. Capture every reached library in consumer provenance, including materials
reached through cooked geometry. Incomplete or missing native dependencies are
scoped diagnostics. Release candidate-library readers that are outside the
resolved dependency set before native cooking. Expand those identities during
saved-input discovery, before coherent capture. A project-owned material reached
through library geometry participates in the same snapshot and cook closure when
project output wins. A higher-priority library excludes the shadowed source from
that consumer closure. Final binding selection uses captured ownership; changes
to a shadowed version do not invalidate the consumer. Status uses the same
resolution with cached metadata and never starts an Inspector process.
For dependency keys absent from every library index, resolve project descriptor
identities through the native Inspector using the engine's public virtual-path
key policy. Batch candidate paths and cache the verified key map as derived data;
status reads consume cached identities without launching tools. An uncooked
project descriptor can then satisfy a cooked-library dependency on its first
scene cook. Managed code must not reimplement the native key algorithm.
The derived map lives under `.build/cache/project-asset-keys-v1`, keyed by the
canonical descriptor and known imported-output path set. Its schema version fixes
the native identity policy; a policy change requires a new map version. Validate
the report digest and exact candidate set before reuse. Content edits reuse the
map, while creation, rename and removal change its key. Missing/corrupt maps
remain pending during status reads and are regenerated by background metadata
refresh or an owned cook. Lookup request/output files and native readers remain
owned through failed-worker drain.

Authored references resolve virtual paths according to source priority. References
already embedded in cooked descriptors retain their native asset keys. A project
source can satisfy such a key only when the engine identity map proves an exact
match; a coincident virtual path is insufficient. Record library dependencies per
consuming asset so an explicit project material and a different library material
at the same path can coexist without sharing freshness or ownership facts.

Explicit replacement of an existing retained source keeps the reviewed source
and settings intact during discovery and native cooking. Capture the incoming
bundle into private inputs under the existing source identity and output
namespace. The publication journal includes the source directory alongside its
cooked roots: verify the reviewed source baseline, retain every old directory,
install the new source and output, and commit only after preview accepts the
generation. Failure or interrupted publication restores both source and output.
Source changes after a committed import are ordinary authoring changes; they do
not invalidate the integrity of that earlier cooked generation. Cleanup removes
only journal-verified private bundles. Dirty owners, ambiguous source ownership,
unowned files in a replacement bundle and changed reviewed baselines block the
replacement before mutation.

Replacement keeps the existing source format, logical primary path, import
policies and output namespace. Map incoming files relative to the primary so its
references survive a filename change; reject layouts that would escape the owned
bundle. Missing prior source files may be repaired from the incoming bundle.
Retry retains the private candidate, including external buffers and candidates
moved aside during rollback, without requiring the original external file again.
Any change to the reviewed source baseline requires a new review.

Inspecting a retained model follows its declared output namespace and verified
published ownership across authoring mounts. Source-folder inspection includes
the output roots of matching retained models. An uncooked model reports its
declared destination without starting native work for unrelated mounts; source
associations require committed provenance and matching output bytes.

Published model provenance records a source/settings/dependency fingerprint
independent of the producer fingerprint. Automatic Save and preview demand may
regenerate unchanged model inputs for a new producer, but changed model inputs
require explicit Reimport or Cook before replacing output. An outdated prior
product without the source fingerprint requires that explicit confirmation too.

Importer version/options, source hashes, and source-relative dependency paths
are retained in authored import descriptors/configuration. Reimport uses those
facts; name/ID changes cannot silently redirect existing scene references. General file rename/move/reference repair UI is excluded; per-slot layout repair
is required by section 20; externally missing references remain
visible and retain their URIs. External source edits trigger stale state and an
explicit reimport/cook; they cannot silently replace a dirty authored material.

Portable project truth is Project.oxy, authored Content, retained `Content/SourceMedia`,
import descriptors/settings, and Config. `.cooked`, `.imported`, `.pipeline`,
`.build`, and `.oxygen` are reproducible/local and excluded from version control.
Absolute local mounts are explicit nonportable dependencies, reported as such;
the portable qualification fixture uses project-relative mounts only.

Reproduction validation deletes derived products from a copied fixture, then
reimports/cooks using only retained sources/settings and the matched toolchain.
Compare stable asset identities, canonical descriptors, resolved dependencies,
and loaded values. Byte-for-byte reproducibility is required only for outputs
whose format declares it; timestamps and diagnostic operation IDs are excluded
from semantic comparison. Opt-in development qualification proves rendered equivalence.

## 18. Owned Native Worker Lifetime

Run each native cook worker in an operation-owned Windows job with
kill-on-close descendant containment. Establish job membership before the worker
can execute or create descendants; no start-then-attach escape window is allowed.
Start with structured arguments and hidden
window behavior, and drain stdout/stderr concurrently. The present native CLI
has no cooperative cancellation protocol: cancellation terminates only that job,
then awaits the child/descendant termination and observes reader tasks. Handle
exit/cancel races by the actual outcome. Cleanup of manifests/snapshot/staging
and release of the project operation gate occur only after termination/I/O drain.

If termination fails, report an explicit termination failure, retain the job and
operation/input ownership, and keep published output protected; never return a
successful Cancelled outcome while writes can continue. Stream cancellation alone
or disposal of Process is not process termination. No embedded-engine or unrelated
process may be killed. Controlled subprocess tests include a periodic writer,
a descendant writer, large output on both streams, startup failure, natural exit
races and failed termination. After successful cancellation, no writer survives
and no additional writes occur.

The managed runner uses CreateProcessW startup job/handle attributes so ownership
exists before child execution, while request arguments remain structured until
native command-line encoding. Both output readers run without the request's
cancellation token. A failed termination throws
`ContentPipelineTerminationException` with `DrainCompletion`; the manifest
adapter retains input until that task finishes. The project coordinator must
likewise retain its operation gate and staging until this drain completes.

## 19. One Procedural Asset Authority

Use one supported engine/content procedural-definition capability shared
by live resolution and cooking. The engine owns immutable recipe/version data,
generator parameters, computed bounds, default material semantics and deterministic
identity mapping. Editor project policy supplies the selected virtual mount/output
scope, not duplicate generator defaults. A definition yields the live asset and
schema-valid cook contribution through the same engine-owned generation helpers.
The cooker remains the native format authority.

`Oxygen.Data` exposes the built-in catalog, canonical URI/descriptor identity,
and cached geometry resolution. Its procedural parameter defaults are shared by
direct mesh factories, binary-parameter generation and the descriptor importer.
Bounds come from the generated mesh, and material parameters come from
`MaterialAsset::CreateDefault`. `Oxygen.Cooker` emits the schema-valid descriptor
contributions and the catalog artifact consumed by the editor. The editor supplies
the project output mount; it does not reconstruct geometry or material defaults.

The canonical ten-shape palette is: Cube, Sphere, Capsule, Cylinder, Cone, Plane, Quad, IcoSphere,
Torus and SubdividedCube. SubdividedCube is advanced creation; ArrowGizmo is an
internal tool resource. Migrate useful GeodesicSphere references to IcoSphere,
then remove the alias and legacy resolution path. Useful former scene uses of
tool-only geometry migrate to ordinary geometry; no compatibility shim remains.

Capsule must join the same native definition/generation/cook authority, not an
editor-only primitive. The engine-owned catalog distinguishes canonical authoring
choices from internal tools, and the editor consumes that metadata rather than
duplicating name lists. Metric, centred, Z-up defaults are specified in the property-inspector Geometry contract. Apply them through
shared native recipes and migrate useful old recipe intent; keep API/schema
prose aligned with generated buffers. This adds no raw generator-parameter editor.
Future additions require explicit catalog and qualification updates.

SetGeometryCommand registers the native request generation and resolves canonical
built-ins through Oxygen.Data. Browser and picker choices use the native catalog's
canonical authoring classification, including advanced SubdividedCube; managed
catalogs and resolvers require supplied metadata rather than constructing recipes.

Discovery retains the complete native catalog under the editor's derived
`cache/builtins/<configuration>` state directory. SDK identity changes refresh the
snapshot; an unavailable SDK uses the last valid snapshot with the
last-known/preview-unavailable notice. Invalid metadata cannot replace the cache.
Query output uses temporary storage, and catalog discovery never publishes cooked
project output. The strict native provider remains the cook recipe source.

Direct factories, parameter-driven generation and descriptor import use the
same engine-owned defaults. Compare effective definitions and generated output;
the editor does not copy constants into a competing recipe.

SetGeometryCommand calls the supported resolver through the runtime adapter
and preserves request generation/completion acceptance. It owns no independent
generator selection/defaults/cache policy or manually maintained format fields.
ProceduralGeometryDescriptorService consumes the same definition/cook contribution;
it no longer owns independent parameters, bounds or default-material constants.
No explicit cook is required for initial procedural preview. Instance overrides
are authored assignments over the shared defaults, with the SlotId contract below.

For every exposed shape compare live/cooked identity mapping, bounds, topology,
vertex attributes, default material and explicit overrides using supported engine
APIs. Save/reopen preserves its URI and values. Include thin/zero-thickness bounds
and sphere segment defaults. ED-M08 records actual loaded/visual parity separately;
source implementation and unit tests alone do not establish rendered equivalence.

Required regression: assign Cylinder to a scene node, explicitly save, then Cook
Current Scene and Cook Project. Both succeed and preserve the authored identity;
repeat for every canonical authoring generator, including Capsule.
Test useful-content migration separately; no legacy alias is a release gate.
Every offered choice must work; do not downgrade a failed required cook to a
successful warning or hide a required choice to evade qualification.
Failure diagnostics carry captured scene identity, node ID/path/name, geometry
identity, and saved revision, and can navigate to the matching current node.
If the node has been renamed/deleted since capture, explain that relationship
instead of selecting a different node with a similar display name.

## 20. Canonical Material-Slot Identity And Reimport

### 20.1 Native inventory and authored assignments

The engine owns slot identity and publishes it through geometry records and
normal Inspector/content metadata. Extend the canonical geometry/scene formats
and their schema versions; a managed decoder or editor-generated slot catalog
is not an alternative implementation.

`MaterialSlotId` is an opaque 128-bit identifier: 16 bytes natively and canonical
lower-case UUID text in JSON. It is scoped by geometry identity. Labels and
indices are not IDs. Separate declared slots remain separate even when their
labels or default materials coincide. A native definition may bind one proven
semantic slot to several LOD/submesh locations; ordinal coincidence across LODs
is insufficient to establish that relation.

The engine-owned inventory schema contains:

| Field | Contract |
| --- | --- |
| `schema_version` | Inventory schema version, checked before use. |
| `geometry_asset_key` | Exact native geometry identity; authored callers retain its URI and winning source. |
| `layout_revision` | SHA-256 of canonical slot IDs, binding locations and default keys. Excludes display labels, material scalar/texture bytes, producer timestamps and temporary paths. |
| `slots[].slot_id` | Unique, nonempty MaterialSlotId. |
| `slots[].display_name` | Presentation only; may be duplicated or renamed. |
| `slots[].bindings[]` | Explicit `lod_index`, `submesh_index` and `default_material_key` for each existing binding. Each material-bearing surface belongs to exactly one declared slot. |

Inventory order is display order only. The native canonical hash writer sorts
slots by ID and each binding list by LOD/submesh before hashing the fixed-schema
UTF-8 record. Managed code transports the native revision and validates the
report/record schema; it does not invent another canonical hash algorithm.

Canonical scene assignments contain GeometryUri, SlotId, MaterialUri and the
last resolved layout revision. MaterialUri absence means no override; clearing
removes that entry and restores the native default for every binding of that
slot. If defaults differ across LOD bindings, show that fact rather than changing
the mesh. Choosing the engine default is an explicit ordinary material assignment.

The last resolved revision detects stale edit targets; it is not a demand to
repair every revision change. A fresh inventory with established SlotId continuity
can resolve the same authored assignment. Library priority changes must resolve
against the winning geometry inventory and cannot reuse another source's slot
merely because the URI, label or ordinal looks similar.

### 20.2 Retained continuity witness

Retained source settings carry versioned native slot provenance as portable
import data. Derived `.build` metadata alone cannot own these IDs: removing
reproducible output/cache directories must not change slot identities. The
source/import-settings publication journal commits updated identity provenance
with the produced geometry, never ahead of it.

For each source geometry, the native provenance schema records:

- Retained source identity and source-geometry anchor.
- Previous inventory revision and its SlotId allocations.
- Stable source slot/surface/LOD IDs when the format supplies explicit durable
  identities; display names are not accepted as durable IDs.
- A versioned `source_layout_witness` for layouts without sufficient durable IDs.
  It contains the ordered source LOD/mesh/surface ownership, declared slot-to-
  surface binding relation, primitive modes, vertex/index counts and hashes of
  source vertex positions and index connectivity for each bound surface.
- Slot labels and prior binding context separately for diagnostics/repair.

The witness excludes material scalar values, texture-reference payloads/image
contents, render colours, normals/tangents, producer fingerprints, timestamps
and temporary paths. Material parameter or texture-content edits therefore do
not change it. Material assignment/slot-to-surface relations do participate;
matching only material names or list positions is never continuity proof.

Resolve a reimport in this order:

1. Validate prior provenance against its retained source identity, schema,
   recorded inventory and any available committed geometry. When derived output
   is absent, validate the retained allocations and recomputed source witness;
   do not allocate replacement IDs merely because the cache was deleted.
2. Reuse a SlotId when unique durable source identities establish the same slot
   and its surface ownership. The inventory revision may change while that
   semantic identity remains valid.
3. Without such identities, reuse the recorded allocation only when the complete
   canonical source-layout witness matches exactly. This is a whole-layout proof,
   not a nearest-name/index matching heuristic. Parameter/texture-only changes
   preserve IDs under this rule.
4. A new or structurally changed slot without proven continuity receives a new
   native ID. Record the old IDs as unresolved for affected authored overrides;
   do not pair them by name, ordinal, material equality or spatial proximity.
   Duplicate/ambiguous persistent IDs are errors, not an invitation to guess.

Native IDs for first-seen slots are allocated deterministically within the
retained source/geometry namespace from the proven durable anchor or complete
witness plus declaration identity. The engine owns this versioned allocation
policy. Retained mappings prevent later producer changes from reallocating
already established IDs. Any policy migration is explicit development tooling;
no shipping legacy resolver remains.

### 20.3 Repair state and affected publication

Keep an unresolved scene override's GeometryUri, SlotId, MaterialUri, previous
layout revision and human-readable prior slot context. These are useful authored
intent, not a hidden compatibility binding. Show Missing slot / Layout changed
on the instance and provide explicit Reassign or Clear commands. Reassign chooses
an existing candidate SlotId; clear removes the obsolete override. Neither
changes mesh topology, another slot, another instance or the shared material.
Both are ordinary undoable scene edits; failed validation changes no targets.

Before publishing changed geometry, inspect its known saved/published consumer
closure and validate all preserved scene overrides against the candidate
inventories. A root with internally valid file hashes can still be semantically
invalid if a scene references a removed slot. Include compatible regenerated
consumers in the same root transaction where needed.

If a preserved consumer has unresolved assignments, retain the candidate as
operation-owned evidence and end the attempt as Needs slot repair. Do not publish
new geometry under an old scene's incompatible slot bindings. Do not mutate or
save affected authoring documents to unblock publication. Release the project
writer and native/read ownership after drain; waiting for a user repair must not
hold the shared coordinator indefinitely. Unrelated scenes and later unrelated
cooks continue against the previous valid generation.

After explicit repair and Save, start a new owned cook of the affected source
and dependent scene closure. Revalidate source/settings/layout baselines and the
latest published root before reusing candidate work; never install a stale
whole-root staging copy after another operation has published. Commit source slot
provenance, changed geometry and compatible scene descriptors atomically under
section 16. Cancel/failure/rollback restores the previous complete generation and
identity provenance. Read-only external consumers that cannot be repaired are
reported with their owning source; do not rewrite a library silently.

Repair-needed scenes cannot pass development parity or report current runtime
assignment. Their last valid published preview may remain visible with explicit
stale/pending status. Transient geometry/material loading is a different condition
and retains generation-checked recovery without demanding structural repair.

### 20.4 Required slot regressions

- Two instances share a multi-slot mesh; editing a nonzero slot changes only the
  selected instance/slot, including all declared LOD bindings.
- Separate slots with equal names/default materials remain independently editable.
- Clear restores each native mesh default; Undo/Redo and pending callbacks cannot
  restore a superseded assignment.
- Unchanged source, producer-only recook with the same structural layout,
  derived-cache deletion and material/texture-only edits retain established SlotIds.
- Proven durable-ID changes retain correct assignments through surface reordering;
  unproven structural changes/removal produce repair state without guessed remaps.
- A failed/abandoned repair attempt does not block unrelated work or publish an
  incompatible preserved scene. Repaired joint publication and rollback retain
  source identity/provenance and all unaffected assets.

## 21. Material Emission Precision

The canonical source, colour conversion, finite HDR range and float32 compiled
factor are defined in [Material Editor section 7.3](material-editor.md#73-field-table-and-numeric-contract).
Native cooking emits float32 emissive RGB after the colour-times-intensity
calculation, and the matching binary version is required by readers. Update
canonical examples, schemas, producer fingerprints and all dependent products;
retired binary16 material-factor content requires recook, not a runtime fallback.

Development expected-state generation uses independent source arithmetic and
unchanged 1e-4 semantic tolerances. Rounding its expected factor to match old
binary16 output would conceal a format deficiency and is forbidden. Source
colour/intensity round-trip tests remain distinct from native compiled-factor
checks and final image comparison.

## 22. Development Qualification And Historical Evidence

Normal production services provide content identity, immutable snapshots,
coordinator admission, leases and native execution for real authoring workflows.
Only opt-in development modules compose those capabilities into saved-revision
parity preparation, capture/process orchestration and comparison. Production
ContentPipeline does not embed qualification schemas, request/result DTOs, command
registration or capture-reservation state; normal Debug/Release stay clean.

Earlier evidence remains in [ED-M07B closeout](../validation/ED-M07B-closeout-audit.md),
[typed assets](../validation/ED-M07B-typed-assets.md),
[import values](../validation/ED-M07B-import-values.md) and
[workspace publication](../validation/ED-M07B-workspace-publication.md).
The canonical changes above require their own implementation and evidence in
ED-M08; this LLD is a contract, not a completion report.
