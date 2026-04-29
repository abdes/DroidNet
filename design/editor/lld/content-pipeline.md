# Content Pipeline LLD

Status: `ED-M07 review-ready`

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
| `REQ-016` | Import/cook workflows are explicit user actions, not hidden save side effects. |
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
  primitives in `Oxygen.Assets`.
- `runtime-integration.md`: runtime cooked-root mount refresh.
- `diagnostics-operation-results.md`: pipeline operation kinds, failure
  domains, and diagnostic code prefixes.

## 4. Current Baseline

Reusable managed primitives already exist:

- `Oxygen.Assets.Import.IImportService` accepts `ImportRequest` with
  `ImportInput` rows and runs importer selection plus build.
- `Oxygen.Assets.Cook.LooseCookedBuildService` is invoked by
  `ImportService.ImportAsync`; callers must not call it again after import.
- `MaterialSourceReader` / `MaterialSourceWriter` own
  `oxygen.material.v1` material descriptors.
- `MaterialSourceImporter`, `GltfImporter`, `ImageTextureImporter`,
  `CookedMaterialWriter`, `CookedGeometryWriter`, `CookedSceneWriter`, and
  `LooseCookedIndexValidator` exist as reusable primitives.
- `Oxygen.Editor.ContentPipeline.MaterialCookService` implements the ED-M05
  scalar material cook slice.
- `ProjectAssetCatalog`, `ContentBrowserAssetProvider`, and
  `AssetIdentityReducer` project source/cooked state into Content Browser rows.
- ED-M07.1 identified save/import paths that previously published cooked-root
  refresh messages outside a validated content-pipeline result. ED-M07 removes
  or reroutes those publishers so runtime mount refresh can happen only after
  cooked-output validation succeeds.

Native engine content pipeline capabilities also exist:

- `Oxygen.Cooker.ImportTool` supports manifest-driven `batch` import and
  `scene-descriptor`, `material-descriptor`, `geometry-descriptor`, texture,
  glTF/FBX, script, input, and sidecar jobs.
- Engine schemas are the source of truth:
  `oxygen.import-manifest.schema.json`,
  `oxygen.scene-descriptor.schema.json`,
  `oxygen.material-descriptor.schema.json`,
  `oxygen.geometry-descriptor.schema.json`, and texture/input schemas.
- `oxygen::content::lc::Inspection` reads loose cooked `container.index.bin`
  assets/files for tooling.
- `oxygen::content::lc::ValidateRoot` validates native loose cooked indexes.
- Native `SceneDescriptorImportJob` consumes `oxygen.scene` v3 descriptors with
  `renderables[].geometry_ref`, optional `material_ref`, cameras, lights,
  environment systems, local fog volumes, and references.
- `EditorModule::AddLooseCookedRoot` / `ClearCookedRoots` sync roots into
  `AssetLoader` and `VirtualPathResolver` at frame start through the existing
  runtime mount path.

Brownfield gaps:

- there is no full `IContentPipelineService` for scene/folder/project cook.
- editor scene authoring JSON is not the native scene descriptor schema.
- managed `CookedSceneWriter` writes a limited scene path and default
  environment; full ED-M07 scene cook must target the native scene descriptor
  importer unless equivalent managed coverage is deliberately added.
- procedural editor geometry references need deterministic descriptor/key
  resolution before scene cook.
- runtime mount refresh currently fires from loose messages, not from a
  validated content-pipeline result.

## 5. Target Design

ED-M07 introduces an explicit workflow:

```text
User action
  -> resolve project cook scope and selected authored inputs
  -> generate/update engine descriptors where needed
  -> generate an import manifest or equivalent import request set
  -> execute cook through Oxygen.Assets or native Oxygen.Cooker APIs
  -> inspect cooked output
  -> validate loose cooked root
  -> refresh asset catalog rows
  -> request runtime cooked-root refresh only after validation succeeds
  -> publish one operation result
```

Runtime-facing descriptor schemas are engine schemas. Editor authoring files
remain editor files:

```text
Content/Scenes/Main.oscene.json        # editor authoring scene
Content/Materials/Red.omat.json        # oxygen.material.v1 descriptor
Content/Geometry/Cube.ogeo.json        # engine geometry descriptor when needed

.cooked/Content/Scenes/Main.oscene     # derived runtime output
.cooked/Content/Materials/Red.omat     # derived runtime output
.cooked/Content/container.index.bin    # derived loose cooked index
```

The pipeline may produce temporary/intermediate engine descriptors under a
derived cache root when the authored editor source cannot itself be consumed by
the engine cooker. Those intermediate files are derived artifacts and must not
replace authored asset identity in scenes, material slots, project manifests,
or recent state.

### User Workflow

The visible ED-M07 workflow is:

1. User opens a project and scene.
2. User chooses `Cook Current Scene`, `Cook Selected Folder`, or
   `Cook Project`.
3. The editor shows a result summary with descriptor/import/cook/inspect/
   validation/mount phases.
4. Content Browser refreshes descriptor/cooked/stale badges.
5. If validation succeeds, the workspace refreshes runtime cooked roots.
6. If validation fails, cooked output remains inspectable but is not mounted.

There is no save-time cook side effect.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.ContentPipeline` | Editor orchestration service, descriptor adapter coordination, import/cook requests, inspect/validate adapters, catalog refresh, operation results. |
| `Oxygen.Assets` | Managed reusable import/cook/index primitives and editor-side readers/writers. |
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
- V0.1 policy always refreshes the catalog after a successful cook attempt and
  requests runtime mount refresh only after cooked-output validation succeeds.
  These are not caller-controlled flags in ED-M07.

### 7.2 Cook Input

```csharp
public sealed record ContentCookInput(
    Uri AssetUri,
    AssetKind Kind,
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

ED-M07 needs a scene adapter from editor scene documents to native
`oxygen.scene` v3 descriptors:

```csharp
public interface ISceneDescriptorGenerator
{
    Task<SceneDescriptorGenerationResult> GenerateAsync(
        Scene scene,
        ContentCookScope scope,
        CancellationToken cancellationToken);
}

public sealed record SceneDescriptorGenerationResult(
    Uri SceneAssetUri,
    string DescriptorPath,
    string DescriptorVirtualPath,
    IReadOnlyList<ContentCookInput> Dependencies,
    IReadOnlyList<DiagnosticRecord> Diagnostics);
```

Mapping requirements:

| Editor authoring value | Native descriptor output |
| --- | --- |
| scene node order and hierarchy | `nodes[]` with stable parent indexes |
| `TransformComponent` | `nodes[].transform.translation/rotation/scale` |
| `GeometryComponent.Geometry.Uri` | `renderables[].geometry_ref` canonical `.ogeo` virtual path |
| first material slot material URI | `renderables[].material_ref` canonical `.omat` virtual path when set |
| `PerspectiveCamera` | `cameras.perspective[]` |
| `DirectionalLightComponent` | `lights.directional[]` including `intensity_lux`, `angular_size_radians`, `environment_contribution`, `is_sun_light` |
| point/spot lights | best-effort descriptor output when component data exists; unsupported fields produce warnings, not silent drops |
| `SceneEnvironmentData.AtmosphereEnabled` + `SkyAtmosphere` | full native `environment.sky_atmosphere` payload with authored V0.1 scalar/vector values overlaid onto fixed native defaults for fields not exposed by the editor |
| `SceneEnvironmentData.SunNodeId` | encoded through the selected directional light's `is_sun_light` / `environment_contribution` fields, not through a separate editor-only environment field |
| `SceneEnvironmentData.PostProcess` native `PostProcessVolume` fields and `BackgroundColor` | `OXE.CONTENTPIPELINE.SCENE.UnsupportedField` warnings in ED-M07 unless the native scene descriptor schema accepts the exact field; do not encode invented native environment/post-process fields |
| unmapped editor fields | one `OXE.CONTENTPIPELINE.SCENE.UnsupportedField` warning per field; never silently dropped |

The adapter must reject an empty scene descriptor because the native schema
requires at least one node. The user-facing message must name the scene.

Native environment emission rule:

- ED-M07 may emit an `environment` block only as a complete engine-default
  payload with supported editor overrides applied. Supported editor overrides
  are `AtmosphereEnabled` plus `SkyAtmosphere.{PlanetRadiusMeters,
  AtmosphereHeightMeters, GroundAlbedoRgb, RayleighScaleHeightMeters,
  MieScaleHeightMeters, MieAnisotropy, SkyLuminanceFactorRgb,
  AerialPerspectiveDistanceScale, AerialScatteringStrength,
  AerialPerspectiveStartDepthMeters, HeightFogContribution, SunDiskEnabled}`.
- Post-process authoring is fully persisted and live-synced by ED-M04, but
  ED-M07 descriptor generation emits warnings for post-process fields until the
  descriptor schema has exact native fields for them.
- ED-M07 must not emit partial native environment subobjects to satisfy schema
  shape by guesswork.
- If a complete default payload is not available, omit `environment` and emit
  warnings for each supported editor environment value that could not be
  represented.

Scene descriptor `name` normalization:

- The native descriptor `name` is a stable identifier derived from the scene
  asset file stem, not the display title.
- Remove the editor suffix first (`Main.oscene.json` -> `Main`).
- Replace characters outside `[A-Za-z0-9_.-]` with `_`.
- If the first character is not `[A-Za-z0-9_]`, prefix `_`.
- Truncate to 63 characters after prefixing.
- If normalization would produce an empty identifier, descriptor generation
  fails with `OXE.CONTENTPIPELINE.SCENE.DescriptorGenerationFailed`.
- The original scene display name/path remains in diagnostics and UI text.

Asset URI to native descriptor path normalization:

| Editor URI | Native descriptor path |
| --- | --- |
| `asset:///<Mount>/<Path>.omat.json` | `/<Mount>/<Path>.omat` |
| `asset:///<Mount>/<Path>.omat` | `/<Mount>/<Path>.omat` |
| `asset:///<Mount>/<Path>.ogeo.json` | `/<Mount>/<Path>.ogeo` |
| `asset:///<Mount>/<Path>.ogeo` | `/<Mount>/<Path>.ogeo` |
| `asset:///<Mount>/<Path>.oscene.json` | `/<Mount>/<Path>.oscene` |

Normalization rejects URIs that do not use the `asset` scheme, do not resolve
under a known project mount, or do not normalize to the expected native
extension for the target field. Native scene descriptors must never contain
`asset:///` URIs or `.json` suffixes in `geometry_ref`, `material_ref`, or
scene-reference fields.

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
- ED-M07 uses derived procedural geometry descriptors under
  `<ProjectRoot>/.pipeline/Geometry/<StableName>.ogeo.json` and adds
  `geometry-descriptor` jobs to the manifest for those descriptors.
- generated descriptors are derived files and are not shown as authored user
  files unless the Content Browser already presents them as generated assets.
- a scene renderable must not silently cook with an all-zero geometry key.
- missing or unsupported geometry produces a failed scene cook diagnostic.

### 7.5 Import Manifest

ED-M07 supports an explicit manifest model that can be serialized to the native
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
- ED-M07 must not depend on a native default `/.cooked` virtual root or on
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
ED-M07 uses a managed schema-aligned validator for the accepted V0.1 manifest
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
    AssetKind Kind,
    string MountName,
    string VirtualPath);
```

`Diagnostics` is the complete workflow diagnostic set for the command result:
descriptor-generation warnings/errors, missing source diagnostics, native import
failures, and cooked-output validation diagnostics. UI producers must publish
this list directly; they must not reduce the result to validation diagnostics
only.

### 7.7 Inspect And Validate

```csharp
public sealed record CookInspectionResult(
    string CookedRoot,
    bool Succeeded,
    Guid? SourceIdentity,
    IReadOnlyList<CookedAssetEntry> Assets,
    IReadOnlyList<CookedFileEntry> Files,
    IReadOnlyList<DiagnosticRecord> Diagnostics);

public sealed record CookValidationResult(
    string CookedRoot,
    bool Succeeded,
    IReadOnlyList<DiagnosticRecord> Diagnostics);
```

Managed inspection may use `LooseCookedIndex` where sufficient. ED-M07 may add
Interop wrappers over `oxygen::content::lc::Inspection` and
`oxygen::content::lc::ValidateRoot` when native validation is the stricter
runtime-compatible authority.

Native `Inspection.LoadFromRoot` and `ValidateRoot` are exception-throwing
pass/fail APIs in the current engine. The ED-M07 adapter returns at most one
synthesized diagnostic for each native failure, mapping the native exception
type/message to `OXE.CONTENTPIPELINE.INSPECT.Failed` or
`OXE.CONTENTPIPELINE.VALIDATE.Failed` and preserving the native message in
`TechnicalMessage`. Inspection failures return `Succeeded=false` and block
validation. Multi-error native validation is future engine work.

## 8. Commands, Services, Or Adapters

### 8.1 Main Service

```csharp
public interface IContentPipelineService
{
    Task<ContentCookResult> CookCurrentSceneAsync(
        SceneDocument document,
        CancellationToken cancellationToken);

    Task<ContentCookResult> CookAssetAsync(
        Uri assetUri,
        CancellationToken cancellationToken);

    Task<ContentCookResult> CookFolderAsync(
        Uri folderUri,
        CancellationToken cancellationToken);

    Task<ContentCookResult> CookProjectAsync(
        CancellationToken cancellationToken);

    Task<CookInspectionResult> InspectCookedOutputAsync(
        Uri? scopeUri,
        CancellationToken cancellationToken);

    Task<CookValidationResult> ValidateCookedOutputAsync(
        Uri? scopeUri,
        CancellationToken cancellationToken);
}
```

`CookCurrentSceneAsync` is the ED-M07 minimum closure path. Folder/project
cook can initially reuse the same manifest builder over a broader input set,
but must not bypass descriptor generation or validation.

### 8.2 Engine API Adapter

```csharp
public interface IEngineContentPipelineApi
{
    Task<NativeImportResult> ImportAsync(
        ContentImportManifest manifest,
        CancellationToken cancellationToken);

    Task<CookInspectionResult> InspectLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken);

    Task<CookValidationResult> ValidateLooseCookedRootAsync(
        string cookedRoot,
        CancellationToken cancellationToken);
}
```

Implementation options:

- use native `Oxygen.Cooker` APIs through `Oxygen.Editor.Interop`.
- invoke `Oxygen.Cooker.ImportTool` only as a bounded fallback when an in-proc
  Interop wrapper is not yet available; the fallback implementation owns any
  temporary manifest-file serialization internally.
- keep ED-M05 `MaterialCookService` on managed `Oxygen.Assets` if it already
  produces the same loose cooked output and validation result.

ED-M07.1 records which option is used for each ED-M07 operation before
ED-M07.2+ implementation begins.
Interop implementation, if chosen, exposes a managed `OxygenContentPipeline`
facade with import/inspect/validate methods returning managed DTOs. Calls may
run from background editor workflows; native exceptions are caught at the
C++/CLI boundary and adapted into managed failure DTOs rather than crossing the
boundary. The facade lifetime is owned by the same host/runtime composition
that owns the engine context.

### 8.3 Catalog And Runtime Refresh

After successful validation:

1. publish/trigger catalog refresh (`ProjectAssetCatalog` /
   `ContentBrowserAssetProvider`).
2. publish a validated cook-refresh message to the workspace. Scene save,
   material save/cook, Content Browser import/cook, and catalog-only refresh
   paths must not trigger runtime mount refresh without validation.
3. `WorkspaceViewModel` refreshes runtime cooked roots through `IEngineService`.

Mount refresh must not happen if validation failed. Inspect-only operations do
not mount.

## 9. UI Surfaces

ED-M07 adds explicit actions to existing UI surfaces:

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

Cook progress can be a simple busy state in ED-M07. Do not invent a separate
progress subsystem unless the diagnostics LLD is updated.

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

- Cook is explicit and may run while the editor runtime is already running.
- Runtime cooked-root refresh happens only after cooked output validation
  succeeds.
- Scene save, material save/cook, Content Browser import/cook, and catalog-only
  refresh paths must not publish unvalidated cooked-root refresh messages.
  Keeping the source tree free of direct save-time cooked-root refresh
  publishers is an ED-M07 closure precondition.
- Runtime mount refresh is best-effort and reports `AssetMount` diagnostics on
  failure.
- ED-M07 proves embedded runtime can refresh mounted cooked roots after cook.
- ED-M08 owns standalone runtime parity and content visual equivalence.
- Multi-viewport remains deferred and is not an ED-M07 validation target.

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
| unsupported authored scene value omitted | `ContentPipeline` | `OXE.CONTENTPIPELINE.SCENE.UnsupportedField` |
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

- ContentPipeline depends on `Oxygen.Assets`.
- ContentPipeline depends on project services for project/cook scope facts.
- ContentPipeline may depend on `Oxygen.Editor.Interop` through
  `IEngineContentPipelineApi` for native cooker APIs.
- WorldEditor, MaterialEditor, and ContentBrowser invoke ContentPipeline
  service contracts.
- Runtime integration consumes validated mount refresh requests.

Forbidden:

- saving a scene or material must not implicitly cook.
- ContentPipeline must not mutate scene authoring state.
- ContentPipeline must not own Content Browser item layout or picker UX.
- ContentPipeline must not persist cooked paths into authoring files.
- Project services must not execute cook/import.
- Runtime services must not generate descriptors or decide cook scope.
- ED-M07 must not make multi-viewport stability a validation gate.

## 14. Validation Gates

ED-M07 is complete when:

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
14. environment mapping tests cover authored sky-atmosphere scalar/vector
    values, atmosphere enabled/disabled, and unsupported exposure/tone/background
    warnings without partial native environment payloads.
15. standalone parity remains deferred to ED-M08.

## 15. Open Issues

No ED-M07 design open issue is allowed before implementation starts. ED-M07.1
is the accepted audit step that records the Interop/ImportTool/managed choice
for each cooker/inspect/validation path; ED-M07.2+ coding is blocked until that
decision is recorded in `IMPLEMENTATION_STATUS.md`.
