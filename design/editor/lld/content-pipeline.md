# Content Pipeline LLD

Status:
`Canonical V0.1 contract; implementation and qualification tracked by ED-M08`

Last Updated: 2026-10-20 01:32

**Navigation:** [purpose](#1-purpose),
[surface identity and reimport](#20-canonical-surface-identity-and-reimport),
[emission](#21-material-emission-precision).

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

| ID         | Coverage                                                                                                                                                            |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GOAL-004` | Authored material, geometry, and scene content can produce cooked output.                                                                                           |
| `GOAL-005` | Descriptor, cook, inspect, validation, catalog, and mount state are visible.                                                                                        |
| `GOAL-006` | Pipeline failures are structured, actionable operation results.                                                                                                     |
| `REQ-014`  | Scalar material values save, reopen, cook, and remain assignable by identity.                                                                                       |
| `REQ-015`  | Editor generates or updates engine descriptor inputs for supported authored content.                                                                                |
| `REQ-016`  | Scoped import/reimport uses retained sources; accepted Save/import/demand triggers and explicit Cook actions share the incremental coordinator and visible results. |
| `REQ-017`  | Cooked output and catalog state refresh after cook.                                                                                                                 |
| `REQ-018`  | Cooked output is validated before runtime mount refresh.                                                                                                            |
| `REQ-019`  | Project cook scope and authored mount policy are honored.                                                                                                           |
| `REQ-021`  | Authored asset identity stays distinct from cooked filesystem paths.                                                                                                |
| `REQ-022`  | Save/cook/refresh/mount failures surface operation results.                                                                                                         |
| `REQ-023`  | Pipeline and engine/tool failures produce correlated logs.                                                                                                          |
| `REQ-024`  | Diagnostics identify descriptor/import/cook/index/mount cause.                                                                                                      |
| `REQ-036`  | Content Explorer reflects refreshed descriptor/cooked states.                                                                                                       |
| `REQ-037`  | Authored source remains repairable and round-trippable.                                                                                                             |

## 3. Architecture Links

- `ARCHITECTURE.md`: game project files, content pipeline subsystem, project
  policy, runtime/content boundaries.
- `project-layout-and-templates.md`: canonical `Content/...` layout and
  generation-root output and atomic publication rules in section 16.
- `project-services.md`: active project context, authoring mounts, local mounts,
  and `ProjectCookScope`.
- `asset-primitives.md`: reusable asset identity/catalog/import/cook primitives
  in `Oxygen.Managed.Assets`.
- `runtime-integration.md`: runtime cooked-root mount refresh.
- `diagnostics-operation-results.md`: pipeline operation kinds, failure domains,
  and diagnostic code prefixes.

## 4. Integration Boundaries

`ContentPipelineService` coordinates scene/asset/folder/project work through
saved snapshots, native ImportTool execution, Inspector validation, provenance
and journaled immutable-generation publication. `MaterialCookService` delegates
to that same service. Managed source readers/writers and catalog projections
remain reusable; they are not alternative native-format cook/publication paths.

The engine owns material, geometry, scene and manifest schemas and native binary
formats. The canonical V0.1 changes require new descriptor/record versions for
stable surfaces, float32 emission, Auto/Fixed cameras and retained
Local/Inherit light/visibility semantics. Update engine-owned version constants,
producers, readers and installed schema compatibility together; recook affected
content. Normal readers reject retired formats. Useful pre-V0.1 migration is a
separate one-time development operation, not a shipping legacy execution path.

Existing integration/evidence is recorded in the milestone ledger. That
historical scope does not establish the newly required surface or
rendering behavior.

## 5. Target Design

```text
Explicit Cook / cook on change / import / saved-dependency demand
  -> project coordinator and saved scope
  -> coherent input snapshot and native descriptor/manifest generation
  -> native cook into staging
  -> complete-root, reference, surface-layout and descriptor validation
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
.cooked/generations/<SourceKey>/Scenes/Main.oscene     # derived runtime output
.cooked/generations/<SourceKey>/Materials/Red.omat
.cooked/generations/<SourceKey>/container.index.bin
```

Derived descriptors/manifests never replace authored URI identities. Save owns
source persistence; nothing about cooking slows or fails it. A cook reads the
saved files on disk and never saves: an explicit cook or Play first asks to save
the unsaved documents it reads, and cook on change cooks what is saved without
asking or waiting
([editor-documents.md](editor-documents.md#533-asking-before-play-and-cook)).
Browse and unsaved edits do not cook.

The normal Cooking, browser, document and inspector surfaces present phase,
progress/cancel, current/stale status and actionable failures. Catalog refresh
and mount refresh occur only through the validated publication authority. Failed
staging remains inspectable without becoming published current content.

## 6. Ownership

| Owner                                | Responsibility                                                                                                                                      |
| ------------------------------------ | --------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Oxygen.Editor.ContentPipeline`      | Editor orchestration service, descriptor adapter coordination, import/cook requests, inspect/validate adapters, catalog refresh, operation results. |
| `Oxygen.Managed.Assets`              | Managed reusable import/cook/index primitives and editor-side readers/writers.                                                                      |
| `Oxygen.Cooker` / engine content API | Native schemas, manifest batch import, scene descriptor import, loose cooked inspection/validation, runtime-compatible descriptors.                 |
| `Oxygen.Editor.Interop`              | Narrow managed wrappers for native cooker inspection/validation/import APIs when ED-M07 needs native behavior that is not exposed in managed code.  |
| `Oxygen.Editor.Projects`             | Project root, authoring mount facts, local mount facts, default cooked output root, and validation policy.                                          |
| `Oxygen.Editor.WorldEditor`          | Scene document ownership and user commands that invoke pipeline workflows.                                                                          |
| `Oxygen.Editor.ContentExplorer`      | Presents refreshed source/descriptor/cooked/stale/broken/mounted state; does not cook.                                                              |
| `Oxygen.Editor.Runtime`              | Runtime mount/unmount calls through `IEngineService`; does not decide cook scope.                                                                   |

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
  inside its validated transaction. These are not caller-controlled bypass
  flags.

### 7.2 Cook Input

```csharp
public sealed record ContentCookInput(
    AssetPath AssetUri,
    ContentCookAssetKind Kind,
    string MountName,
    string SourceRelativePath,
    FileSystemPath SourceAbsolutePath,
    ContentCookInputRole Role);

public enum ContentCookInputRole
{
    Primary,
    Dependency,
    GeneratedDescriptor,
}
```

`AssetUri` is the identity the source stands for, its path without the authoring
`.json`: `/Content/Materials/Red.omat` for `Red.omat.json`,
`/Content/Input/Player.input` for `Player.input.json`,
`/Content/Models/Ship.glb` for a model. The source file is `SourceRelativePath`.
A source that cooks one asset cooks it at that identity: its import job names it
as `virtual_path`, and the cooker writes the descriptor there. Cook results,
provenance and run status key a source by the same identity.

### 7.3 Scene Descriptor Adapter

`ISceneDescriptorGenerator.GenerateAsync` emits the current engine-owned scene
schema from a captured canonical scene document and resolved dependency scope.
It returns the generated descriptor path/virtual identity, dependencies and
complete diagnostics. Development identity-map construction consumes its stable
traversal through a development adapter; no qualification protocol is embedded
in this production service.

| Authoring fact                                       | Required native descriptor/record content                                                                                                                                      |
| ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Node identity/order/hierarchy                        | Deterministic depth-first records and parent indices; duplicate names are not identities.                                                                                      |
| Local TRS                                            | Captured position, quaternion and scale without silent repairs.                                                                                                                |
| Scene Visibility / geometry Cast and Receive Shadows | Each stored Local/Inherit mode and local value; never flatten inherited intent into the current effective boolean.                                                             |
| Geometry URI                                         | Canonical geometry key/path and its native surface inventory.                                                                                                                  |
| Instance material assignments                        | Explicit SurfaceId-keyed overrides and resolved material keys; every supplied surface/LOD binding is supported. No single material_ref or positional-only override path.       |
| Perspective camera                                   | Auto/Fixed mode, stored Fixed ratio, vertical FOV, near/far and parented pose. Auto ratio/content rectangle is derived per view, not saved on resize.                          |
| Directional light                                    | Complete selected scalar/common/shadow values and canonical AtmosphereLightSlot None/Primary/Secondary.                                                                        |
| Atmosphere roles                                     | Per-light assignment only; uniqueness checked across stored lights including hidden/off ones. No SunNodeId, IsSunLight/Contributes duplicate authority or automatic promotion. |
| Scene environment                                    | Complete canonical atmosphere, captured-sky diffuse/specular, exposure and appearance data from the owning field tables.                                                       |
| Background                                           | Canonical display-background colour semantics, independent of scene exposure/tone mapping while preserving foreground transparency.                                            |
| Editor Hide, loaded IsActive, selection/gizmos       | Editor/runtime-derived state, excluded from authored runtime content.                                                                                                          |
| Unsupported required values                          | Actionable failure before publication; no best-effort omission or substituted defaults.                                                                                        |

The engine owns the changed schema and binary versions. Do not continue emitting
old scene record layouts that cannot represent the selected contracts. Qualified
imports reject excluded components instead of reporting success after dropping
them. Reject invalid clipping/enum/role/surface data with the scene and field in
the diagnostic. Empty scenes are valid: emit an empty node array and preserve
their environment and identity. Project cooking publishes them normally, without
placeholder nodes. Components still require valid node references; runtime hosts
own their preview cameras.

The native descriptor name is the scene file stem as spelled: the file name
without `.oscene.json`, so `Level 1.oscene.json` cooks to `Level 1.oscene`. The
name follows the shared segment rule (`oxygen.name.schema.json`), which the
editor checks where the file is named, so the source and the cooked scene keep
one identity.

| Authoring URI                                  | Native reference         |
| ---------------------------------------------- | ------------------------ |
| `asset:///<Mount>/<Path>.omat.json` or `.omat` | `/<Mount>/<Path>.omat`   |
| `asset:///<Mount>/<Path>.ogeo.json` or `.ogeo` | `/<Mount>/<Path>.ogeo`   |
| `asset:///<Mount>/<Path>.oscene.json`          | `/<Mount>/<Path>.oscene` |

Reject an unknown mount, wrong scheme/type, traversal or invalid normalization.
Native references contain neither `asset:///` nor an authoring `.json` suffix.
Opaque native keys come from engine inspection/identity APIs, not a second
managed hash implementation.

### 7.4 Engine Content References

Engine content (the basic shapes, the default material and engine-internal
assets) lives in the SDK's engine content pak, `share/oxygen/engine.pak`, under
`/Engine`. A scene reference to it is a library reference like any other:

- authored scene files keep the asset path, file type included, such as
  `/Engine/Generated/BasicShapes/Cube.ogeo`.
- the native scene descriptor names that path; the engine pak is the first
  native context root, so the cook resolves the reference without copying the
  asset into the project, and the publication never binds the engine pak because
  every runtime mounts it beneath the project's content.
- no editor-side descriptor is generated for engine content.
- a scene renderable must not silently cook with an all-zero geometry key; an
  unresolved reference fails the scene cook with a diagnostic.

Textures follow the same rule. A material's texture or a scene's environment
texture (sky light, sky sphere and fog cubemaps, the metering mask) names the
texture's virtual path. The native cook binds a texture the job's own output
holds to that output's texture table, and a texture a cooked context root holds
(the engine pak, a declared pak or a mounted library) by its texture key, the
engine's key of its virtual path, without copying it; the runtime resolves that
key in the newest mount holding it. Material jobs therefore receive the cooked
context roots like scene, geometry and sidecar jobs. The asset catalog lists the
cooked textures of loose libraries and paks with the facts their texture record
states (2D or cube, format), which the texture pickers use, and the library
closure records a library texture as a cooked dependency of the material or
scene that uses it.

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

The manifest writer must serialize the native schema property names shown above.
If the implementation does not use `JsonPropertyName` attributes on the records,
it must use a dedicated manifest writer that produces the same snake_case schema
shape.

Mount layout rules:

- Each authored mount cooks to its own physical loose root:
  `<ProjectRoot>/.cooked/generations/<SourceKey>`, selected by the project head.
- The manifest `output` is that physical loose root.
- The manifest `layout.virtual_mount_root` is `/<MountName>`.
- Native descriptor references and cooked-index virtual paths use the same
  `/<MountName>/...` prefix.
- Example for the built-in content mount:
  - `output = <ProjectRoot>/.cooked/generations/<SourceKey>`
  - `layout.virtual_mount_root = /Content`
  - material descriptor URI `asset:///Content/Materials/Red.omat.json` becomes
    native path `/Content/Materials/Red.omat`
  - cooked index entries must round-trip back to
    `asset:///Content/Materials/Red.omat`
- Do not depend on a native default `/.cooked` virtual root or on cooked
  filesystem paths as asset identity.

Required job type mapping:

| Input                                               | Manifest job type                 |
| --------------------------------------------------- | --------------------------------- |
| scalar material descriptor `*.omat.json`            | `material-descriptor`             |
| authored/imported geometry descriptor `*.ogeo.json` | `geometry-descriptor`             |
| generated native scene descriptor                   | `scene-descriptor`                |
| glTF/GLB source media import                        | `gltf`                            |
| FBX source media import                             | `fbx`                             |
| texture source or descriptor                        | `texture` or `texture-descriptor` |
| input source `*.input.json`                         | `input` (section 24)              |

Scene jobs depend on the material and geometry jobs needed by their renderables.
Folder/project cook generates a manifest and validates it against the native
`oxygen.import-manifest.schema.json` contract before execution. Use the shared
schema-aligned validator for the accepted V0.1 manifest subset; native
builder/import construction is a secondary check for the chosen adapter, not a
substitute for schema validation.

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
IsUpToDate, IsPublished and IsMounted. Preserve these facts on every typed
caller; successful staging alone cannot set IsPublished/IsMounted. A no-op
result reuses verified products without claiming a new native production
operation.

`Diagnostics` is the complete workflow diagnostic set for the command result:
descriptor-generation warnings/errors, missing source diagnostics, native import
failures, and cooked-output validation diagnostics. UI producers must publish
this list directly; they must not reduce the result to validation diagnostics
only.

### 7.7 Inspect And Validate

Use the installed native Inspector and native schemas through the existing
process adapter. Reports contain container identity, asset keys/types/paths,
protected files/digests, dependency information and canonical surface inventory.
Cache only verified reports against their producer/schema/container
fingerprints. Managed feature code does not decode binary
geometry/material/scene structures.

Failures return complete scoped diagnostics with native codes/messages; invalid
inspection cannot authorize reuse or publication. Validate every preserved and
new descriptor/reference in the candidate generation, including SurfaceId bindings
and physical-path uniqueness. Successful file hashes alone do not prove that a
source material or surface was preserved.

## 8. Commands, Services, Or Adapters

### 8.1 Main Service

Use the existing `IContentPipelineService` operations for current scene, asset,
folder and project cooking, retained import/reimport, inspection and validation.
Every target uses the same scope, snapshot, native execution, publication and
status machinery. MaterialCookService is a typed caller of CookAssetAsync, not
an independent writer. Invalid/missing surface identity is scoped to its affected
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
transaction commits. A validated staging result does not mark new content
current. The workspace resynchronizes its current authoring scene through the
normal Runtime/SceneEngineSync boundary; document saves and catalog-only
refreshes cannot publish or remount roots independently. Inspect-only operations
never mount.

### 8.4 Asset Relocation

`IAssetRelocationService` renames, moves, copies and deletes authored assets
while keeping every authored reference to them valid, as the project writer, so
it never overlaps a cook. It cooks nothing: the changed files reach cook on
change through the asset catalog like any other change.

- It keeps its reference index current itself: any asset catalog change,
  including one made outside the editor, makes the next search read the files
  again, so no caller invalidates it.
- `FindReferrersOutsideAsync` reviews a delete: the files outside the deleted
  items that reference any of them, read again first, so the review is never
  stale.
- `FilesChanged` reports the exact files the editor moved, rewrote, created or
  deleted, including the files and folders a copy creates, on the caller's
  synchronization context after open documents followed the change.
- `RelocateAsync` records the relocation into the history its caller gives it,
  as one change whose undo runs the reverse relocation and records the reverse
  of that for redo. An undo that later changes made impossible is rejected like
  any request, changes nothing and stays in the history.
- A relocation refuses to run over open documents with unsaved changes
  (`IUnsavedDocuments`), because a document follows a relocation only from its
  saved file: its caller asks to save them first, and an undo, a redo or an edit
  made since is refused.

### 8.5 Asset Authoring

`IAssetAuthoring` (`Authoring/`) creates new files from their asset family's
template (`AuthoredAssetTemplate`), such as a script or an input file. It
proposes the folder and the first free name, and checks a chosen name against
the asset catalog: a name collides with any file of the same name in the folder
that has one of the template's suffixes, because both would cook to the same
asset. `CreateAsync` checks the name again and writes the template through
`IAtomicFileStore` only where no file exists, so a file the catalog has not seen
yet is never overwritten. It reports each created file as `FilesChanged` before
`CreateAsync` returns.

## 9. UI Surfaces

Use the existing UI surfaces:

| Surface                               | Actions                                                                             |
| ------------------------------------- | ----------------------------------------------------------------------------------- |
| Content Explorer toolbar/context menu | `Cook`, `Cook Folder`, `Inspect Cooked Output`, `Validate Cooked Output`, `Refresh` |
| Scene document/header/menu            | `Cook Current Scene`                                                                |
| Material editor                       | continues to expose `Cook` for one material                                         |
| Output/log panel                      | correlated cook phase summaries and diagnostics                                     |

Illustrative result summary:

```text
Cook Current Scene: Main
Generated descriptor: Content/Scenes/Main.oscene.json -> .pipeline/...
Cooked: 1 scene, 1 material, 1 geometry
Validated: .cooked/generations/<SourceKey>/container.index.bin
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
- import manifests.
- `.cooked/generations/<SourceKey>/` runtime containers.
- `.cooked/publications/<OperationId>.json` and the atomic `.cooked/head.json`.
- optional inspect reports.

Derived artifacts must not be used as authored identity in scene files, material
surfaces, project manifests, or recent documents.

## 11. Live Sync / Cook / Runtime Behavior

- Automatic and explicit cooks use the same coordinator and stage output while
  runtime/authoring may continue. Preview briefly pauses for the publication
  transaction in section 16.
- Runtime cooked-root refresh happens only after cooked output validation
  succeeds.
- Scene save, material save/cook, Content Explorer import/cook, and catalog-only
  refresh paths must not publish unvalidated cooked-root refresh messages.
  Save-time scheduling must never bypass this publication authority.
- Runtime publication uses the section 16 transaction; mount failure reports
  `AssetMount`, restores prior output, and never reports the new output current.
- Embedded runtime refreshes mounted cooked roots only through validated
  publication.
- Opt-in development tools own standalone parity and visual-equivalence
  evidence.

## 12. Operation Results And Diagnostics

Operation kinds:

| Operation                       | Producer                              | Domain            |
| ------------------------------- | ------------------------------------- | ----------------- |
| `Content.Descriptor.Generate`   | scene descriptor generator            | `ContentPipeline` |
| `Content.Manifest.Generate`     | manifest builder                      | `ContentPipeline` |
| `Content.Import`                | content pipeline / engine API adapter | `AssetImport`     |
| `Content.Cook.Asset`            | content pipeline                      | `AssetCook`       |
| `Content.Cook.Scene`            | content pipeline                      | `AssetCook`       |
| `Content.Cook.Folder`           | content pipeline                      | `AssetCook`       |
| `Content.Cook.Project`          | content pipeline                      | `AssetCook`       |
| `Content.CookedOutput.Inspect`  | inspect adapter                       | `ContentPipeline` |
| `Content.CookedOutput.Validate` | validation adapter                    | `ContentPipeline` |
| `Content.Catalog.Refresh`       | catalog refresh adapter               | `AssetIdentity`   |
| `Runtime.CookedRoot.Refresh`    | workspace/runtime integration         | `AssetMount`      |

Failure mapping:

| Failure                                                                  | Domain            | Code                                                      |
| ------------------------------------------------------------------------ | ----------------- | --------------------------------------------------------- |
| scene descriptor generation failed                                       | `ContentPipeline` | `OXE.CONTENTPIPELINE.SCENE.DescriptorGenerationFailed`    |
| required authored scene value cannot be represented; publication blocked | `ContentPipeline` | `OXE.CONTENTPIPELINE.SCENE.UnsupportedField`              |
| scene geometry reference cannot be normalized                            | `ContentPipeline` | `OXE.CONTENTPIPELINE.GEOMETRY.DescriptorGenerationFailed` |
| manifest generation failed                                               | `ContentPipeline` | `OXE.CONTENTPIPELINE.MANIFEST.GenerationFailed`           |
| source path missing                                                      | `AssetImport`     | `OXE.ASSETIMPORT.SourceMissing`                           |
| importer returned diagnostics                                            | `AssetImport`     | `OXE.ASSETIMPORT.ImportFailed`                            |
| cook failed                                                              | `AssetCook`       | `OXE.ASSETCOOK.CookFailed`                                |
| cooked index missing or invalid                                          | `AssetCook`       | `OXE.ASSETCOOK.IndexInvalid`                              |
| inspect failed                                                           | `ContentPipeline` | `OXE.CONTENTPIPELINE.INSPECT.Failed`                      |
| validation failed                                                        | `ContentPipeline` | `OXE.CONTENTPIPELINE.VALIDATE.Failed`                     |
| catalog refresh failed                                                   | `AssetIdentity`   | `OXE.ASSETID.RefreshFailed`                               |
| mount refresh failed                                                     | `AssetMount`      | `OXE.ASSETMOUNT.RefreshFailed`                            |

Import/cooker native diagnostic codes must be preserved in technical details and
adapted to the nearest editor diagnostic code.

## 13. Dependency Rules

Allowed:

- ContentPipeline depends on `Oxygen.Managed.Assets`.
- ContentPipeline depends on project services for project/cook scope facts.
- ContentPipeline may depend on `Oxygen.Editor.Interop` through
  `IEngineContentPipelineApi` for native cooker APIs.
- WorldEditor, MaterialEditor, and ContentExplorer invoke ContentPipeline service
  contracts.
- Runtime integration consumes validated mount refresh requests.

Forbidden:

- Save must not execute a cook inline or publish output; successful persistence
  may schedule saved-scope work through the shared coordinator.
- ContentPipeline must not mutate scene authoring state.
- ContentPipeline must not own Content Explorer item layout or picker UX.
- ContentPipeline must not persist cooked paths into authoring files.
- Project services must not execute cook/import.
- Runtime services must not generate descriptors or decide cook scope.

## 14. Validation Gates

The following production paths require regression coverage:

1. `Cook Current Scene` generates a native `oxygen.scene` descriptor from the
   editor scene with transform, geometry, surface material, perspective camera,
   directional light, and supported environment data.
2. engine content references needed by the scene resolve through the engine
   content pak or fail with visible diagnostics.
3. cooking the current scene cooks its referenced V0.1 material and geometry
   dependencies or reports which dependency blocked the cook.
4. cooked output contains `.cooked/generations/<SourceKey>/container.index.bin`
   and cooked `.oscene`, `.omat`, and required `.ogeo` entries for the test
   scene.
5. inspect output shows asset entries and file entries for the cooked root.
6. validation runs before runtime mount refresh; failed validation blocks mount
   and publishes a visible operation result.
7. successful validation refreshes Content Explorer asset states without editor
   restart.
8. successful validation requests runtime cooked-root refresh and reports mount
   failure as `AssetMount`, not as a cook failure.
9. dirty save-time cook coupling does not reappear in project services.
10. scene/material save, material cook, Content Explorer import/cook, and
    catalog-only refresh paths no longer publish unvalidated cooked-root refresh
    messages.
11. URI normalization tests prove `.omat.json`/`.ogeo.json` authored asset URIs
    become native `.omat`/`.ogeo` descriptor paths and reject invalid inputs.
12. manifest layout tests prove the selected Content generation uses virtual
    root `/Content` and cooked-index virtual paths round-trip to
    `asset:///Content/...`.
13. scene descriptor name tests prove display/file names with spaces normalize
    to native schema identifiers without changing authored display names.
14. environment mapping covers every canonical authored field; a missing
    required mapping fails publication without partial native payloads.
15. Opt-in development qualification verifies native/editor parity without
    adding a production dependency.

## 15. Scheduling And Product Scope

The [content workflow contract](content-cooking-workflows.md) specifies shared
status, incremental reuse, browser/picker behavior and recovery. Cooking runs
on import, on asset/active-scene demand and, while the **Cook on change**
project setting is on, on the file changes the asset catalog reports
([editor documents §5.3](editor-documents.md#53-behavior)). Browse and
transient-edit events do not cook; external-source reimport stays explicit.

V0.1 retains existing Cook Asset/Folder/Scene/Project actions to rebuild stale
content explicitly through the same coordinator. No separate stale-only
scheduler, generic project-settings panel, renderer-preset selector or
descriptor/manifest editor/launcher is required. Source/generated paths are
visible and copyable in content/result information; Inspect and Validate operate
on cooked products. Project mounts supply cook policy; scene settings supply
render intent. PRD section 8 and the current field tables define the supported
surface.

## 16. Saved Inputs And Publication Transaction

One immutable publication document owns the accepted root order, native index
identities, products and consumed-input evidence. `.cooked/head.json` atomically
selects its ID and digest. All producers use this transaction; development
qualification borrows its normal publication and read APIs.

### Input Capture And Serialization

- ContentPipeline is the sole writer/coordinator for scene, material, asset,
  folder, and project cooks. Existing material helpers execute through it and
  cannot publish independently. One cook/publish operation per project runs at a
  time; a later request waits or can be cancelled, and captures inputs only when
  it obtains the project operation gate.
- Finish an active edit gesture first. An explicit cook or Play asks to save
  the unsaved documents in its dependency closure before it runs, naming them
  (Save and Cook, or Cancel); a cook never waits for a save and never saves
  implicitly. Automatic requests cook what is saved.
- Resolve the saved dependency closure, including importer settings and source
  media, then capture a coherent byte snapshot of the files on disk. Read
  participating files through handles that exclude concurrent writes while
  their bytes are copied. If the set changes while dependencies are resolved,
  retry capture at most three times, then fail visibly. Do not guess a mixture
  of revisions. External inaccessible/conflicting files fail before cooking.
  Optional import settings record absence as an input fact; settings created
  during discovery/capture cause rediscovery. Discovery includes geometry buffer
  files and separates engine recipes and cooked-only references from authored
  inputs. Cooked-only references require publication validation and output
  leases before reuse; their presence in the graph does not establish freshness.
- Normal cooking copies inputs to `.build/cook/<OperationId>/inputs`, preserving
  their logical mount-relative relationships. Record source hashes, import
  settings, schema/build fingerprint, project lifetime, target scope, and
  operation identity. Native jobs read this private snapshot. `ContentImportExecution` supplies the input root, operation
  directory and coordinator operation ID separately from the native manifest's
  output root. The adapter must not infer input paths from the published
  directory layout. Temporary manifests remain in the operation directory until
  its worker drains.
- Authoring may continue after capture. A later edit or source change marks the
  result stale relative to current authoring, while a successful cook of the
  captured input remains a successful, identifiable historical result.

The saved-snapshot core accepts a caller-owned canonical private input directory
and reads the files on disk. `CookInputSnapshotCapture` keeps the normal
cooking destination above; development preparation supplies
`<EvidenceRoot>/<OperationId>/inputs`. Both use the same closure discovery,
coherent copy and hash implementation. Validate directory ownership, reparse
boundaries and non-overlap with authored/published roots before writing. This is
an ordinary destination-parameterized snapshot capability: no qualification
protocol types, field expectations or validation-specific path policy enter the
production API.

### Staging And Validation

- Reserve fresh native SourceKeys in the operation journal before creating
  `.cooked/generations/<SourceKey>` candidates. Cook directly at those final
  paths; virtual paths remain `/<Mount>/...`. Source snapshots and backups stay
  in `.build`. Exclude `.generation.lock` when seeding; seal only after the
  final native writer. Retain the final write-denying validation opening through
  sealing and publication.
- For a partial asset/folder cook, seed staging from the previous published root
  and replace the selected assets and dependencies. Preserve unrelated entries.
  Track generated subassets so reimport removes superseded outputs from that
  source without deleting unrelated assets or changing stable IDs.
- Run native inspect, complete-root validation, reference/dependency validation,
  and all required descriptor-field checks on staging. Unsupported required V0.1
  content is an error, not a successful result with omitted values.
- Before publication, ensure the operation still owns the project lifetime and
  publication gate. A closed/replaced project invalidates the operation. Failure
  or cancellation here leaves published output and preview untouched.

### Removing Retired Products

A deleted or moved source retires its prior products without rebuilding their
mount. The cook repair (`CookRootRepair`) lists each inherited root's retired
outputs: the asset entries and the path-keyed auxiliary files no remaining
product keeps. The cook removes them from the staged copy of the root, under
its fresh generation identity, before it cooks anything into it
(`ContentPipelineService.RemoveRetiredOutputsAsync`), then verifies the root
that is left. The removal runs the ImportTool's `remove-cooked` command from a
JSON request in the operation folder (`ImportToolContentPipelineApi`), which
calls the engine's `LooseCookedWriter.RemoveAssetDescriptor` and
`RemoveAuxiliaryFile`: the writer publishes the index without them, then
deletes their files. Resource table entries stay, as they do when a cook
replaces an asset; only a rebuild of the root compacts the tables. The other
products of the mount are reused. A product that used a removed one is cooked
again, so a scene that lost its material reports it instead of keeping a
reference to cooked content that is gone.

A mount whose every product was retired, and that nothing in the cook goes
into, leaves the publication as if it was never cooked: the preview mounts the
publication without its root, and maintenance reclaims its generation. A root
that still holds assets without source ownership cannot be vacated and fails
the cook with a diagnostic.

### Publication And Rollback

1. Write `.cooked/publications/<OperationId>.json` with one ordered binding
   table: owned logical mounts name SourceKey generations; external libraries
   name absolute paths and observed index identities. Products reference logical
   mounts. Preserve the last cook's consumed inputs during mount-only changes so
   changed dependencies remain stale. `Project.oxy` is authored intent, updated
   through project-manager CAS.
2. Prepare and validate the complete native mount set before the selection gate.
   Capture one publication lease for mounting, catalog refresh and readers.
   Fixed catalogs verify the bound index digest and never watch/reload that
   index in place. Passive status/catalog reads cannot publish or cook.
3. Under the selection gate, recover abandoned mutating journals using saved
   project configuration, then check this operation's original head,
   configuration and source baselines. Never silently rebase a cook. A live
   operation lock blocks recovery.
4. Pause preview and drain pending I/O while retaining old mounts and generation
   readers. The operation journal is
   `.build/cook/<OperationId>/publication.json`. Persist `Applying` before any
   authored-source or head mutation. The journal phases are `Building`,
   `Prepared`, `Applying`, `SourcesApplied`, `HeadSelected`, `RuntimeReady`,
   `Committed` and `RolledBack`.
5. Apply source changes, atomically select the candidate head, accept native
   mounts, commit any confirmed `Project.oxy` change, resume preview and durably
   commit. The gate covers this sequence and rollback. Loader and resolver
   switch before retirement callbacks run; old objects retain their exact source
   bindings.
6. Return the existing candidate lease, then refresh derived catalogs and notify
   readers outside the gate. With no runtime, report disk publication and
   `NotMounted`. Failure restores source/configuration/head state and the prior
   usable preview. Failed restoration retains the journal and leaves preview
   unavailable.
7. Recover before project validation/activation on reopen; re-read the manifest
   after recovery. A recovery conflict blocks cooked admission while preserving
   valid authoring access. Missing owned generation markers preserve provenance
   for repair but forbid reuse and mounting. Rebuild fresh generations; never
   recreate a marker.
8. A loose root whose index declares another format is stale, not damaged: the
   index header names its version, so no parse is attempted. Opening the
   publication lists it with the unavailable roots, which status, catalogs and
   mounting skip and which a cook rebuilds; unlike a missing generation marker,
   it does not block admission of the rest of the publication. A library that is
   stale or cannot be read is left out of the publication's bindings, so the
   rest of the content still mounts; a cook that needs it fails with
   `asset_cook.library_unavailable`. The publication service logs each
   unavailable root once per publication, and each unreadable library once for
   as long as it stays unreadable. Every index read hashes the file's bytes, and
   the parse and catalog records of identical bytes are shared by every reader.

After publication, maintenance reclaims unused generations automatically. It
protects selected documents, active/recovery roots and native or managed reader
leases, claims under the selection gate, and deletes payloads outside it. Delete
the index first, payloads next and marker last; partial deletion retains the
marker for retry. Nonempty markerless roots require proof they were never
prepared for publication; otherwise retain them. An empty shell can be removed
nonrecursively.

Completed operation scratch needs both the operation and retry-input claims
before deletion. Retry input does not block source/head recovery. Keep the
reservation journal if an unsealed candidate cannot be deleted. Settled scratch
is owned generated data: cleanup checks containment and reparse points without
rehashing its payloads. Reverting authored sources recooks them; retained
generations are not a history UI.

Cancellation before publication drains the worker and ends as Cancelled without
changing published output. Cancellation during publication is deferred until
commit or rollback reaches a safe state; the final result reports what actually
happened. The UI may show Cancelling, but a cancellation token alone is not
proof that native I/O stopped. Publication is also valid when preview is
unavailable; no engine startup is required merely to cook saved source.

A cook on change that foreground work stops ends the same way, before
publication, and reports `CookPreemptedException`; one already publishing
finishes ([editor documents §5.3.6](editor-documents.md#536-bursts-of-changes-and-requests)).
A stopped run is not run again: its operation folder holds the stopped
attempt's snapshots and journal. Cook on change cooks the sources that are
still stale in a new run.

### Open Documents

The cook reads files only and holds nothing of an open document. It knows open
documents through `IUnsavedDocuments` (`Oxygen.Editor.Documents`), which the
Shell implements: `IUnsavedCookInputs` finds the unsaved documents an explicit
cook or Play reads, from the target's assets and the assets their saved
descriptors reference, and the caller asks the Shell to save them before the
cook starts. The cook opens the files it reads with
`FileShare.Read | FileShare.Delete`: in-place writes stay excluded, and a save's
atomic replace proceeds; the save is a change of its own that is cooked again.

A status (`AssetCookStatusReader`) reads source files from disk through
`CookSavedSourceReader`, sharing them with any writer
(`FileShare.ReadWrite | FileShare.Delete`), so it never blocks a save or a tool
writing in place; a write during the read changes the hash, which reads as
changed. It waits for no document or writer and holds no document: the asset
feed adds the unsaved documents that edit an asset from `IUnsavedDocuments`.

### Result And UI Contract

Extend the pipeline result with input provenance, the captured/current freshness
comparison, publication state, and mount state. Distinguish CookFailed,
ValidationFailed, PublicationFailed, MountFailed, Cancelled, and successful
publication in structured diagnostics. A staged success plus failed publication
is partial success for the composite workflow, never a usable new mounted cook.
Copyable generated descriptor/manifest paths are diagnostic affordances; they
are not authored identities or required manual repair steps.

### Publication Validation Gates

- [ ] A cook captures the saved bytes; later edits do not mutate the captured
      bytes or get cleared by cook completion. Overlapping requests serialize.
- [ ] Asset/folder recook preserves unrelated published entries and stable
      source/subasset identity; deleted generated subassets are reconciled.
- [ ] Inspect and validation run on staging before any published file changes.
- [ ] Failure/cancellation at every staging and publication boundary preserves
      or restores the prior complete output; recovery handles interruption
      between each pair of journaled steps, including first publication with no
      prior root.
- [ ] Preview pause/drain/resume, active-scene change, runtime fault, failed new
      mount, failed rollback mount, and standalone reader leases are exercised.
- [ ] Input/output hashes and publication state are queryable in results;
      stale/newer authoring never becomes falsely current. User workflows need
      no manual generated-file edits.

## 17. Qualified Import And Reproduction Policy

V0.1 imports glTF 2.0 (`.gltf`/`.glb`) and FBX through the native importers
and carries everything the engine can represent: the node hierarchy with local
transforms, triangle geometry with its LODs and skinning data, every material
channel the material format has a field for, perspective and orthographic
cameras, and directional, point and spot lights. The editor applies no
narrower content policy than the engine's; there is one import behavior for
initial import, retained source cooking, replacement and captured-input
execution. The qualification fixtures include one small self-contained source
of each format, and the import report records effective units, handedness,
axis conversion, winding, and resulting bounds.

A source feature the engine cannot represent is dropped with a source-attributed
warning, `import.source_feature_dropped`, and the import continues: animation,
skeletons (skinned geometry imports in its bind pose), morph targets, material
variants, texture wrap and filter modes, per-texture UV mappings that differ
within a material, material fields the format has no slot for, and FBX vertex
caches, constraints, curves and procedural geometry; a glTF extension listed as
required that the importer does not read is reported as
`import.source_extension_required` and its content imports as far as it can be
read. FBX without unit metadata imports with `fbx.units_unknown` and without
axis metadata with `fbx.axis_unknown`.

Broken source data drops the smallest unit that cannot be built, with a warning
that names it, and the model imports: a triangle with an out-of-range index, a
non-triangle or unreadable primitive, an FBX LOD level, and a mesh left with
no usable triangles. A scene node whose mesh was dropped keeps its place and
transform and shows no geometry; every other node keeps its own geometry. Skin
data that cannot bind its mesh is dropped and the mesh imports as static
geometry in its bind pose. A spot light whose cone angles form no representable
cone is dropped. Material values outside their range are clamped, and a
missing texture binds the error texture; a texture that cannot be read at its
authored path is first looked for by file name beside the model and in the
model's `textures` and `Textures` folders, then the same in the model folder's
parent, as model-sharing sites lay them out (`import.texture.found_by_name`). A
texture path that names a folder names no image and is missing. Source analysis declares those places, so a
captured cook sees them. An import fails only when the source
file itself cannot be read. Perspective cameras map explicit source ratios to
Fixed and omitted
ratios to Auto; for FBX, window-size aspect mode maps to Auto and its fixed
aspect modes map to Fixed using the importer-resolved ratio. Canonical
descriptors store an explicit `aspect_mode` and retain a positive
`aspect_ratio` in both modes. Per-light atmosphere assignment uses the
canonical None/Primary/Secondary contract. Imported textures remain read-only
in the editor. A texture binding must resolve to an emitted resource, not an
error index advertised as successful support.

Initial intake uses the same native `analyze-sources` recipes as cooking. The
request owns its native surface source identity through discovery,
retention, settings creation and retries. Native declarations and observations
identify the input closure; capture checks their existence, metadata and read
proofs against private copies. Relative paths remain relative to the primary
source, including parent-relative buffers and external images. A missing file
the source names outside the folder its inputs are kept under, typically a
DCC's temporary folder, is not an input: it is reported as
`MissingDependencyOmitted` and the import proceeds without it. An existing file
outside that folder cannot be retained and fails the source with
`DependencyOutsideSource`. One reader classifies every native input this way
for both import discovery and cooking. A missing file is never fatal: during a
captured cook, a path the capture does not list reads as absent, so a missing
texture binds the error texture with a warning and the author overrides it. Embedded data
needs no extra file. Failed analysis cannot publish a partial source bundle.

Each attempt supplies the primary-relative path with the complete input set, so
a retry can change the bundle's common root. Positive directory structure is
preserved during retention and replacement staging. Full timestamps remain
in-attempt coherence evidence; durable freshness uses file content and probe
presence/type/size/link state, so producer-owned metadata writes do not make an
otherwise unchanged project stale.

Retained model bundles live under `Content/SourceMedia/DCC` through the declared
Content mount. Operation ownership and native query files survive a failed
worker termination until native drain and cleanup complete. Query output remains
in its operation directory and cannot overwrite an authored source.

A model's import state is its model import record,
`<ProjectRoot>/imports/<source_identity>.import.json`, the engine's
`oxygen.model-import.schema.json` shared with the ImportTool
([scene instances](scene-instances.md) NGN14): source, virtual path, settings,
source identity, surface provenance, node continuity, published nodes and
source fingerprint. The product reads no other format; older sidecars become
records by hand, keeping their source identity. Normals
and tangents use native generation of missing attributes, preserving authored
values. This supplies the UV-derived tangent basis required by normal maps in
FBX files that omit tangents. Canonical retained settings also carry the
engine-owned surface identity provenance defined in section 20. Creation uses the
ordinary atomic file store with a missing-file baseline.

New imports follow the existing type folders: `/Content/Materials/<model>`,
`/Content/Geometry/<model>` and `/Content/Scenes/<model>`. The default review
starts at the selected authoring mount's root, never at the selected Scenes or
Materials folder. An optional relative group is applied within each type folder;
the dialog shows all resulting paths before acceptance. Native mesh/material
names already include the source-file namespace, which is not repeated when it
matches the model folder. Bulk resource tables remain native-owned shared files.

One-time migration normalizes useful older records/output paths and their
authored references together, then rebuilds affected content. Production
reimport accepts only the canonical settings/layout; it does not retain
version-2 grouped path behavior. Changing a record alone cannot relocate
content because native asset keys derive from virtual paths; output groups move
only through the reference-aware relocation in
[Content Explorer §9.5](content-explorer-asset-identity.md#95-asset-relocation),
which rewrites the record's `source` with the files. After a relocation or delete, a prior
product whose authored source is gone is retired for every asset kind (a model
only when its record no longer names it), and only its outputs leave the
root, so its old native keys leave the index; this is not a forbidden identity
change or a namespace overlap with the retired owner. Whether moving a model's
outputs to another group retires its old outputs is to be verified
([#97](https://github.com/abdes/DroidNet/issues/97)). Section 16
([Removing retired products](#removing-retired-products)) describes the
removal.
Ownership, collision checks, folder cooking and source inspection cover every
disjoint output namespace, without claiming the whole mount for one imported
model.

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
URI and require that output in the verified emitted/reused set before
publication. Folder cooks include sources whose output namespace intersects the
selected folder. Capture and freshness checks use the same resolved source
scope. Read-only status uses saved settings and prior output evidence without
native discovery.

Descriptor jobs use the engine's ordered `cooked_context_roots` lookup context
for other staged project mounts and leased cooked libraries. Cook dependencies
before consumers across mount boundaries, use staged roots for affected mounts,
and retain published roots for reused dependencies. Reference lookup follows the
saved content priority; explicitly listing the destination root places it at
that position, with the last occurrence of a duplicate root winning. The highest
priority matching asset must have the requested type; a lower-priority asset
must not hide a type error in the effective source. Check each root's index and
newly written descriptors before proceeding to the next root; index publication
must not change the selected source.

Cooked-library dependencies retain the selected source, native key/type and a
fingerprint of its protected container files. These facts participate in
consumer freshness and the selected publication document. Library inputs remain
read-only and leased through native work, publication and failed-worker drain.
Source priority is resolved from the saved project order for both cooking and
preview; no library is copied into project authoring or claimed as a newly
produced project asset.

Dependency inspection uses the existing native Inspector in metadata-only mode.
Cache its versioned report under `.build/cache/cooked-dependencies-v3`, keyed by
the verified container-file fingerprint. Validate the report schema,
cached-report digest, container identity and indexed asset keys/types/paths
before reuse. Missing or damaged cache entries require inspection; they cannot
prove freshness. Cooking populates cache misses while retaining library and
native-tool readers through worker completion or failed-termination drain.
Status reads consume cached dependency facts without starting native processes:
an asset whose closure reaches an uninspected library reads Unknown with
`asset_cook.library_inspection_required`, and a changed library remains Out of
date while further inspection is pending. Owned cooking and source admission
populate missing native metadata; there is no separate background inspection,
because automatic cooking cooks every asset that is not current. Browser refresh
does not start that work or force SDK rediscovery. Project changes cancel
obsolete work and schedule the latest catalog revision.

Cooking freshness and output integrity are separate facts. A **Cooked** badge
means the acknowledged saved inputs, dependencies and observed producer match a
successful publication. Browser rows consume immutable status snapshots. Save,
catalog, completed-cook, producer and project events invalidate those snapshots;
unchanged refresh/filter/resolve requests reuse them. Dirty edits, progress and
runtime availability update overlays without rescanning content.

Output availability is **Present**, **Missing** or **Unknown**, based on
publication and index metadata plus file presence. It is not a payload integrity
certificate. Unknown facts are neutral (**Status pending**), and missing output
offers **Needs cooking**. Only an actual validation/operation failure reports
damage or failure. Keep source errors, cook progress and preview failures
actionable in their existing diagnostics and Cooking surfaces.

The shared cooking compatibility owner supplies its observed producer identity.
Status reads neither verify SDK binaries nor launch discovery. Changes to engine
or editor producer files invalidate that observation; an owned discovery or
native operation establishes the next identity. Full native verification remains
required for cook reuse, publication and explicit integrity validation. Normal
mounts perform metadata admission; full hash verification remains opt-in.

When verification finds damaged shared cooked data, cooking automatically
rebuilds the affected root from all its known source owners. Cooking reports
**Rebuilding affected content** and captures those sources together with the
requested work, including an incoming replacement. The candidate starts empty:
damaged shared payloads must never be retained and assigned new checksums. Every
product in that root is rebuilt; unaffected roots remain reusable. Missing
sources and unknown ownership produce path-specific diagnostics before
publication. The current publication remains selected until native validation
and transactional publication succeed. Same-size descriptor-only damage can be
repaired selectively when shared files remain verified. Staging checks inherited
bytes against the accepted native inventory before allowing a writer to update
it.

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
resolution with cached metadata and never starts an Inspector process. For
dependency keys absent from every library index, resolve project descriptor
identities through the native Inspector using the engine's public virtual-path
key policy. Batch candidate paths and cache the verified key map as derived
data; status reads consume cached identities without launching tools. An
uncooked project descriptor can then satisfy a cooked-library dependency on its
first scene cook. Managed code must not reimplement the native key algorithm.
The derived map lives under `.build/cache/project-asset-keys-v1`, keyed by the
canonical descriptor and known imported-output path set. Its schema version
fixes the native identity policy; a policy change requires a new map version.
Validate the report digest and exact candidate set before reuse. Content edits
reuse the map, while creation, rename and removal change its key.
Missing/corrupt maps remain pending during status reads and are regenerated by
owned cooking. Lookup request/output files and native readers remain owned
through failed-worker drain.

Authored references resolve virtual paths according to source priority.
References already embedded in cooked descriptors retain their native asset
keys. A project source can satisfy such a key only when the engine identity map
proves an exact match; a coincident virtual path is insufficient. Record library
dependencies per consuming asset so an explicit project material and a different
library material at the same path can coexist without sharing freshness or
ownership facts.

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
facts; name/ID changes cannot silently redirect existing scene references.
General file rename/move/reference repair UI is excluded; per-surface layout repair
is required by section 20; externally missing references remain visible and
retain their URIs. External source edits trigger stale state and an explicit
reimport/cook; they cannot silently replace a dirty authored material.

Portable project truth is Project.oxy, authored Content, retained
`Content/SourceMedia`, import descriptors/settings, and Config. `.cooked`,
`.imported`, `.pipeline`, `.build`, and `.oxygen` are reproducible/local and
excluded from version control. Absolute local mounts are explicit nonportable
dependencies, reported as such; the portable qualification fixture uses
project-relative mounts only.

Reproduction validation deletes derived products from a copied fixture, then
reimports/cooks using only retained sources/settings and the matched toolchain.
Compare stable asset identities, canonical descriptors, resolved dependencies,
and loaded values. Byte-for-byte reproducibility is required only for outputs
whose format declares it; timestamps and diagnostic operation IDs are excluded
from semantic comparison. Opt-in development qualification proves rendered
equivalence.

## 18. Owned Native Worker Lifetime

Run each native cook worker in an operation-owned Windows job with kill-on-close
descendant containment. Establish job membership before the worker can execute
or create descendants; no start-then-attach escape window is allowed. Start with
structured arguments and hidden window behavior, and drain stdout/stderr
concurrently. The present native CLI has no cooperative cancellation protocol:
cancellation terminates only that job, then awaits the child/descendant
termination and observes reader tasks. Handle exit/cancel races by the actual
outcome. Cleanup of manifests/snapshot/staging and release of the project
operation gate occur only after termination/I/O drain.

If termination fails, report an explicit termination failure, retain the job and
operation/input ownership, and keep published output protected; never return a
successful Cancelled outcome while writes can continue. Stream cancellation
alone or disposal of Process is not process termination. No embedded-engine or
unrelated process may be killed. Controlled subprocess tests include a periodic
writer, a descendant writer, large output on both streams, startup failure,
natural exit races and failed termination. After successful cancellation, no
writer survives and no additional writes occur.

The managed runner uses CreateProcessW startup job/handle attributes so
ownership exists before child execution, while request arguments remain
structured until native command-line encoding. Both output readers run without
the request's cancellation token. A failed termination throws
`ContentPipelineTerminationException` with `DrainCompletion`; the manifest
adapter retains input until that task finishes. The project coordinator must
likewise retain its operation gate and staging until this drain completes.

## 19. One Engine Content Authority

The engine owns its built-in content and ships it cooked. The engine build's
`oxygen-engine-content` target cooks a loose `/Engine` root (procedural geometry
descriptors with engine-owned recipes, bounds from the generated meshes, and the
default material) and packs it with PakTool into `engine.pak`, with a browse
index and its own source key. The editor neither reconstructs geometry nor
copies material defaults.

The canonical ten-shape palette is Cube, Sphere, Capsule, Cylinder, Cone, Plane,
Quad, IcoSphere, Torus and SubdividedCube, under
`/Engine/Generated/BasicShapes`; the default material is
`/Engine/Generated/Materials/Default.omat`. Tool-only assets such as the arrow
gizmo live under `/Engine/Internal`, which pickers hide by folder.
`EngineAssets` (Managed.Assets) names these paths for editor code.

The editor reads `engine.pak` through `IEngineContentPak`:

- the asset catalog lists its assets as Packaged assets with the Engine badge;
  the Content Explorer shows it as the read-only `Engine` root.
- it is the lowest cooked library and the first native context root; the cooking
  artifact inventory leases and verifies it like the tools and schemas, so its
  content identity joins the cook fingerprint.
- the preview mounts it beneath the project's content; a missing pak is reported
  loudly and leaves `/Engine` references unresolved without failing the engine.

Geometry surface inventories of engine shapes come from the native Inspector
`geometries` report on `engine.pak`, as for any cooked geometry, so engine
shapes have inventories without a project cook.

Required regression: assign Cylinder to a scene node, explicitly save, then Cook
Current Scene and Cook Project. Both succeed, preserve the authored identity and
reference the engine asset without a project copy; repeat for every canonical
shape. Every offered choice must work; do not downgrade a failed required cook
to a successful warning or hide a required choice to evade qualification.
Failure diagnostics carry captured scene identity, node ID/path/name, geometry
identity, and saved revision, and can navigate to the matching current node. If
the node has been renamed/deleted since capture, explain that relationship
instead of selecting a different node with a similar display name.

## 20. Canonical Surface Identity And Reimport

### 20.1 Native inventory and authored assignments

The engine owns surface identity and publishes it through geometry records and
normal Inspector/content metadata. Extend the canonical geometry/scene formats
and their schema versions; a managed decoder or editor-generated surface catalog is
not an alternative implementation.

`SurfaceId` is an opaque 128-bit identifier: 16 bytes natively and
canonical lower-case UUID text in JSON. It is scoped by geometry identity.
Labels and indices are not IDs. Separate declared surfaces remain separate even
when their labels or default materials coincide. A native definition may bind
one proven semantic surface to several LOD/submesh locations; ordinal coincidence
across LODs is insufficient to establish that relation.

The engine-owned inventory schema contains:

| Field                      | Contract                                                                                                                                                               |
| -------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `schema_version`           | Inventory schema version, checked before use.                                                                                                                          |
| `geometry_asset_key`       | Exact native geometry identity; authored callers retain its URI and winning source.                                                                                    |
| `layout_revision`          | SHA-256 of canonical surface IDs, binding locations and default keys. Excludes display labels, material scalar/texture bytes, producer timestamps and temporary paths. |
| `surfaces[].surface_id`    | Unique, nonempty SurfaceId.                                                                                                                                            |
| `surfaces[].display_name`  | Presentation only; may be duplicated or renamed.                                                                                                                       |
| `surfaces[].bindings[]`    | Explicit `lod_index`, `submesh_index` and `default_material_key` for each existing binding. Each material-bearing submesh belongs to exactly one declared surface.     |
| `surfaces[].visible`       | The surface's asset default for the scene schema's surface `Visibility` (section 20.5).                                                                                |
| `surfaces[].casts_shadows` | The surface's asset default for the scene schema's surface `CastsShadows` (section 20.5).                                                                              |
| `lod_policy`               | The geometry's default LOD policy (section 20.5).                                                                                                                      |

Inventory order is display order only. The surface defaults and the LOD policy
are values, not layout: they are excluded from `layout_revision`, so changing a
default never makes an override stale. The native canonical hash writer sorts
surfaces by ID and each binding list by LOD/submesh before hashing the fixed-schema
UTF-8 record. Managed code transports the native revision and validates the
report/record schema; it does not invent another canonical hash algorithm.

`IGeometrySurfaceProvider.ReadAsync(project, geometryUri, cancellationToken)`
is the editor's authoring entry point. The existing `ContentPipelineService`
selects the winning project/library source, retains publication and file
readers, and obtains typed inventory through native Inspector
`geometries --virtual-path`. Project output must pass the shared freshness
checks before and after inspection; uncooked, stale or missing output yields no
current inventory. Project replacement cancels the request. Engine geometry is
read from `engine.pak` the same way, so its inventory exists before any cook. No
managed code decodes geometry bytes or hashes surfaces.

Canonical scene surface overrides contain GeometryUri, SurfaceId, the last
resolved layout revision and the overridden values of the surface's schema
properties (`Material`, `Visibility`, `CastsShadows`;
[scene model 5.2](scene-authoring-model.md#52-parts-sources-and-overrides)).
An absent value means no override; clearing removes it and restores the native
default for every binding of that surface. If defaults differ across LOD bindings, show that fact rather than
changing the mesh. Choosing the engine default is an explicit ordinary material
assignment.

The last resolved revision detects stale edit targets; it is not a demand to
repair every revision change. A fresh inventory with established SurfaceId
continuity can resolve the same authored assignment. Library priority changes
must resolve against the winning geometry inventory and cannot reuse another
source's surface merely because the URI, label or ordinal looks similar.

### 20.2 Retained continuity witness

Retained source settings carry versioned native surface provenance as portable
import data. Derived `.build` metadata alone cannot own these IDs: removing
reproducible output/cache directories must not change surface identities. The
source/import-settings publication journal commits updated identity provenance
with the produced geometry, never ahead of it.

Captured inputs and the selected publication document retain the exact consumed
bytes. Returned native provenance is a produced source-file update with distinct
before and after hashes. The existing publication journal installs ordinary
sidecar updates through atomic compare-and-swap writes and restores them with
cooked roots on failure. Explicit source-bundle replacement uses separate
publication staging; it never moves or edits the consumed input snapshot.

Product provenance records both the consumed fingerprint and the fingerprint
accepted for subsequent reuse after those source updates. Freshness compares
committed source updates against their after hashes and all other inputs against
their consumed hashes. Later authoring edits make products stale; they do not
invalidate immutable generation receipts. No provenance field is omitted from
dependency fingerprints, and no consumed hash is rewritten after cooking.

For each source geometry, the native provenance schema records:

- Retained source identity and source-geometry anchor.
- Previous inventory revision and its SurfaceId allocations.
- Stable source surface/LOD IDs when the format supplies explicit durable
  identities; display names are not accepted as durable IDs.
- A versioned `source_layout_witness` for layouts without sufficient durable
  IDs. It contains the ordered source LOD/mesh/surface ownership, declared
  surface-binding relation, primitive modes, vertex/index counts and
  hashes of source vertex positions and index connectivity for each bound
  surface.
- Surface labels and prior binding context separately for diagnostics/repair.

The witness excludes material scalar values, texture-reference payloads/image
contents, render colours, normals/tangents, producer fingerprints, timestamps
and temporary paths. Adapter-owned witnesses are captured before producer
coordinate/unit conversion and optimization. Material parameter or
texture-content edits therefore do not change it. Material
assignment/surface-binding relations do participate; matching only material
names or list positions is never continuity proof.

Transform-bake or instance-material policies may legitimately split or rename
output geometry. Their changed geometry identity/anchor can change SurfaceIds even
when the raw source-layout witness matches; witnesses do not merge different
geometry assets. Format-specific capture and cost are owned by the native
[geometry pipeline](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Docs/Import/geometry_work_pipeline_v2.md#surface-provenance).

Resolve a reimport in this order:

1. Validate prior provenance against its retained source identity, schema,
   recorded inventory and any available committed geometry. When derived output
   is absent, validate the retained allocations and recomputed source witness;
   do not allocate replacement IDs merely because the cache was deleted.
2. Reuse a SurfaceId when unique durable source identities establish the same surface
   and its surface ownership. The inventory revision may change while that
   semantic identity remains valid.
3. Without such identities, reuse the recorded allocation only when the complete
   canonical source-layout witness matches exactly. This is a whole-layout
   proof, not a nearest-name/index matching heuristic. Parameter/texture-only
   changes preserve IDs under this rule.
4. A new or structurally changed surface without proven continuity receives a new
   native ID. Record the old IDs as unresolved for affected authored overrides;
   do not pair them by name, ordinal, material equality or spatial proximity.
   Duplicate/ambiguous persistent IDs are errors, not an invitation to guess.

Native IDs for first-seen surfaces are allocated deterministically within the
retained source/geometry namespace from the proven durable anchor or complete
witness plus declaration identity. The engine owns this versioned allocation
policy. Retained mappings prevent later producer changes from reallocating
already established IDs. Any policy migration is explicit development tooling;
no shipping legacy resolver remains.

### 20.3 Kept changes and publication

An override of a surface that is no longer proven is a **kept change**
([scene model 5.2](scene-authoring-model.md#52-parts-sources-and-overrides),
[scene instances](scene-instances.md) SI-7, SI-8). The scene keeps its
GeometryUri, SurfaceId, values, previous layout revision and human-readable
prior surface context: authored intent, not a hidden compatibility binding. The
instance shows Missing surface / Layout changed in the caution box with
explicit Reassign and Remove commands. Reassign chooses an existing candidate
SurfaceId; Remove deletes the obsolete override. Neither changes mesh
topology, another surface, another instance or the shared material. Both are
ordinary undoable scene edits; failed validation changes no targets.

A kept change never blocks anything. Changed geometry publishes with its new
inventory; scenes that reference it keep their source unchanged. Their next
cook reports each kept change as a warning
(`scene.descriptor.surface_invalid`, Warning) and leaves it out of the cooked
override table, so the runtime shows the geometry's default for that surface.
A later reimport that proves the surface again makes the change apply again
without any edit. Malformed overrides (a nil or duplicate SurfaceId, an unknown
property) remain cook errors: they are invalid source, not lost continuity.

### 20.4 Required surface regressions

- Two instances share a multi-surface mesh; editing a nonzero surface changes only the
  selected instance/surface, including all declared LOD bindings.
- Separate surfaces with equal names/default materials remain independently
  editable.
- Clear restores each native mesh default; Undo/Redo and pending callbacks
  cannot restore a superseded assignment.
- Unchanged source, producer-only recook with the same structural layout,
  derived-cache deletion and material/texture-only edits retain established
  SurfaceIds.
- Proven durable-ID changes retain correct assignments through surface
  reordering; unproven structural changes/removal produce kept changes without
  guessed remaps, and the scene still cooks with a warning.
- Geometry publication with kept changes in consuming scenes succeeds; rollback
  retains source identity/provenance and all unaffected assets.

### 20.5 Surface defaults and the default LOD policy

A geometry asset supplies the asset defaults of its surfaces' `Visibility` and
`CastsShadows` and of its geometry component's `LodPolicy`
([scene model 5.3](scene-authoring-model.md#53-the-scene-property-schema)).
The cooked geometry carries them once:

- one `SurfaceDesc` per declared surface (`surface_id`, `flags` with
  `kSurfaceFlag_Visible` and `kSurfaceFlag_CastsShadows`), listing exactly the
  SurfaceIds its bindings declare. The default material stays per binding,
  since LODs may bind different defaults;
- `default_lod_policy`, a `LodPolicyRecord`: `type` (`kFixed`, `kDistance`,
  `kScreenSpaceError`), `lod_index`, `boundary_count`, `thresholds`,
  `hysteresis_ratio`, `enter_finer_sse` and `exit_coarser_sse`. A geometry has
  at most `kMaxLodCount` (8) LODs, so each threshold array holds 7 boundaries.

The geometry descriptor spells them as the scene schema keys: a top-level
`surfaces` array of `{ surface_id, visible, casts_shadows }` and a
`lod_policy` object (`type` `fixed`, `distance` or `screen_space_error`, with
that policy's fields). An absent entry takes the scene schema's default. The
inventory reports the cooked values (section 20.1).

Model import settings carry the same values per imported geometry: the default
LOD policy and, by SurfaceId, each surface's Visibility and Cast Shadows. The
model import settings editor shows them with the geometry's surfaces, and the
adapters write them into the produced geometry. Without a setting, the
surfaces take the schema defaults, and so does the LOD policy of a geometry
with one LOD. A geometry with several LODs, such as an FBX LOD group, takes a
Screen-space error policy instead, so that an application that sets no policy,
such as a native demo, still sees its LODs switch. The error is the bounding
sphere's projected radius in pixels. Boundary `i` enters its finer LOD at
256 / 2^i pixels (256, 128, 64, …) and its coarser LOD at 0.8 of that, a band
that keeps the LOD from flickering.

The policy units are the engine's. A Distance threshold is the view distance
in radii of the geometry's bounding sphere, and a Screen-space error is that
sphere's projected radius in pixels. Thresholds of 4, 8, 16, … radii match the
default errors in a 1080p view with a 60° vertical field of view. The
inspector starts a new policy of the asset's kind from the asset's values,
and a policy of another kind from these defaults.

A scene stores its changes of these values as overrides of the geometry
component (scene model 5.2). The cooked scene carries every override in one
`kOverride` table of `OverrideRecord`s, addressed by node, the schema's
`override_id`, the part ID and the witnessed layout revision, and sorted by
them. A value is typed by its property: a bool, an asset key of the property's
asset type, or, for a LOD policy, the index of its `LodPolicyRecord` in the
`kOverrideLodPolicy` table. An index rather than an offset keeps the value
valid when a later cook step relocates the descriptor's tables. The scene
descriptor (version 14) spells the overrides as a renderable's `overrides`:

```json
"overrides": {
  "values": { "/lod_policy": { "type": "fixed", "lod_index": 1 } },
  "parts": [ { "kind": "surface", "id": "…", "layout_revision": "…",
               "values": { "/material": "/Content/Materials/Red.omat",
                           "/visible": false } } ]
}
```

## 21. Material Emission Precision

The canonical source, colour conversion, finite HDR range and float32 compiled
factor are defined in
[Material Editor section 7.3](material-editor.md#73-field-table-and-numeric-contract).
Native cooking emits float32 emissive RGB after the colour-times-intensity
calculation, and the matching binary version is required by readers. Update
canonical examples, schemas, producer fingerprints and all dependent products;
retired binary16 material-factor content requires recook, not a runtime
fallback.

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
ContentPipeline does not embed qualification schemas, request/result DTOs,
command registration or capture-reservation state; normal Debug/Release stay
clean.

Earlier evidence covers ED-M07B content discovery/use, publication/recovery,
imported and cooked-only typed assets, numeric import values and workspace
publication. The canonical changes above require their own implementation and
evidence in ED-M08; this LLD is a contract, not a completion report.

## 23. Native source analysis

M08.1.9 builds on the immutable publication contract in
[section 16](#16-saved-inputs-and-publication-transaction).
[The milestone plan](../plan/ED-M08-runtime-parity-and-standalone-validation.md#m081-remaining-increments)
owns execution order and gates.

Native preparation and input observation are specified by the
[Cooker owner](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Docs/Import/async_import_pipeline_v2.md#source-analysis-and-input-observations).
Native source analysis extends the existing tool/API with a batch
request/result: producer/schema identity, declared outputs, logical references,
actual file/absence observations and diagnostics. Use the same native
schemas/import interpretation as cooking. Managed code keeps project/mount
resolution, dirty-document policy, scene DTO projection and capture ownership.
Analyze, capture the reported closure, compare input proofs, then cook captured
input; reject unreported reads or use the existing conflict/retry path. No
daemon or universal graph is introduced.

The cook owns one verified native artifact lease across its queries and
execution.

Analysis and capture schemas participate in that artifact identity; each report
is validated against the leased schema and correlated with the submitted job
IDs, types and logical source paths. Incomplete reports retain source-attributed
diagnostics. A failed worker termination retains manifests, reports, capture
maps and owned artifacts until its exposed drain completes cleanup.

Replacement analysis uses incoming captured bytes under the retained source
paths. The current publication and live source bundle remain unchanged until
publication succeeds. The same logical paths and capture map feed cooking, so
operation-directory names cannot change output identities.

Remove parallel managed descriptor-dependency parsers and repeated small native
queries where this batch supplies their facts. Preserve separate source
invalidation, import scheduling and runtime ownership graphs. Tests cover
multi-root crash/preview failure, source conflicts, shutdown, old readers, no
perpetual stale loop, exact analysis/cook closure and bounded process launches.

## 24. Input Sources

[Input authoring](input-authoring.md) owns the input asset model; this section
owns how the pipeline cooks it.

- **Jobs.** Every `*.input.json` under an authoring mount is one `input` job,
  named by the source's identity (`virtual_path`, the path without the authoring
  `.json`).
- **Placement.** An input source is a container: the cooker names each product
  under the source's identity ([asset identity 1.2][aid]). `Jump` declared in
  `Content/Gameplay/Player.input.json` cooks to
  `/Content/Gameplay/Player.input/Jump.oiact` and round-trips to
  `asset:///Content/Gameplay/Player.input/Jump.oiact`. Two sources in one folder
  that declare the same name produce two assets. Moving a source moves its
  products: relocation maps the source's identity as a folder.
- **Several outputs per source.** The import report lists every asset an input
  job produced; provenance records them under the input source. A product's
  source follows from its identity, so the declaration of an action or context
  (go to declaration, a context's declared priority) is read from that one file
  (`InputSourceFiles`); nothing scans a folder.
- **References.** A mapping names an action of its own source by name and an
  action of another source by virtual path
  (`/Content/Input/Common.input/Fire.oiact`). The source analysis reports those
  paths as references; discovery resolves a product named under a project input
  source to that source, so the manifest gives the job `depends_on` on it, and
  input jobs receive the cook's `cooked_context_roots` for actions of cooked
  libraries. The cooker resolves each path like a scene reference.
- **Checks.** The input reader (`InputSourceReader`, Managed.Assets) checks
  JSON, the engine schema, duplicate names and undeclared local names, each at
  its position; `InputRules.CheckActionPaths` (Input) checks that a path names
  an action a project source declares. The input facts (`IInputSources`) report
  both on save (`OXE.INPUT.Source.*`), and the scene cook checks starting input
  contexts with `StartingContextRules` (Input).
- **Renames.** Renaming an action or context is a relocation request
  (`InputProductRename`): the source is rewritten, local references included,
  the old and new product identities enter the relocation map, and every
  referrer is rewritten, with undo through the history.
- **Diagnostics.** Native `input.*` codes keep their codes and map to properties
  through their `$`-rooted object path.

Gates: input authoring IA-7 and IA-14.

[aid]: asset-identity.md#52-sources-and-containers
