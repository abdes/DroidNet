# ED-M07 - Content Pipeline And Cooking

Status: `accepted - implementation active`

## 1. Purpose

Make content cooking an explicit editor workflow: generate engine descriptors,
build/import manifests, cook authored content and dependencies, inspect and
validate loose cooked output, refresh Content Browser state, and refresh runtime
mounts only after validation succeeds.

ED-M07 starts from the accepted game-project layout:

```text
Content/Scenes/*.oscene.json
Content/Materials/*.omat.json
Content/Geometry/*.ogeo.json
Content/SourceMedia/...
.cooked/<Mount>/container.index.bin
```

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `GOAL-004` | Authored content produces runtime cook artifacts. |
| `GOAL-005` | Descriptor, cook, inspect, validation, catalog, and mount states are visible. |
| `GOAL-006` | Pipeline failures are structured and actionable. |
| `REQ-015` | Supported authored content generates engine descriptor inputs. |
| `REQ-016` | Cook/import workflows are explicit user actions. |
| `REQ-017` | Cooked output and catalog state can refresh. |
| `REQ-018` | Cooked output validates before mount. |
| `REQ-019` | Project cook scope and authored mount policy are honored. |
| `REQ-022` | Pipeline failures surface operation results. |
| `REQ-023` | Pipeline/native tool failures produce correlated logs. |
| `REQ-024` | Failure domain identifies descriptor/import/cook/index/mount cause. |
| `REQ-036` | Content Browser reflects refreshed cooked state. |
| `REQ-037` | Authored data remains repairable and round-trippable. |

## 3. Required LLDs

Implementation may not begin until these are reviewed and accepted:

- `content-pipeline.md`
- `project-services.md`
- `asset-primitives.md`
- `runtime-integration.md`
- `diagnostics-operation-results.md`

This plan deliberately includes a native API audit in ED-M07.1. The outcome
must be recorded before coding ED-M07.2+ so editor code does not duplicate
engine cooker behavior that is already available through `Oxygen.Cooker`.

## 4. Scope

In scope:

- content pipeline operation-kind and diagnostic-code constants.
- `IContentPipelineService` contracts and DI registration.
- native cooker API adapter decision:
  - prefer in-proc Interop wrappers over duplicating native cooker behavior.
  - allow `Oxygen.Cooker.ImportTool` only as a bounded fallback when no in-proc
    wrapper exists yet.
- scene descriptor generation from editor scene documents into native
  `oxygen.scene` v3 descriptors.
- procedural/basic geometry descriptor resolution needed by scene cook.
- material, geometry, scene, folder, and project cook request orchestration.
- import manifest generation for dependency-ordered cooks.
- inspect and validate loose cooked output.
- Content Browser/catalog refresh after cook.
- runtime cooked-root refresh after successful validation.
- visible operation results for descriptor, import, cook, inspect, validation,
  catalog, and mount failures.

## 5. Non-Scope

Out of scope:

- standalone runtime parity and visual equivalence; ED-M08 owns it.
- multi-viewport stabilization; it remains deferred.
- material graph, texture material editing, custom shaders.
- full source-media UX beyond invoking supported import/cook on files already
  under authored mounts.
- project layout/template changes; ED-M06A owns that and is validated.
- save-time cook side effects.
- new progress subsystem beyond simple busy/result state.

## 6. Implementation Sequence

### ED-M07.1 - Baseline Audit And Native API Decision

- Audit managed pipeline code:
  - `Oxygen.Editor.ContentPipeline`
  - `Oxygen.Assets.Import`
  - `Oxygen.Assets.Cook`
  - `Oxygen.Editor.ContentBrowser` catalog refresh paths
  - `WorkspaceViewModel` cooked-root refresh
- Audit native engine APIs:
  - `Oxygen.Cooker.ImportTool` batch and descriptor commands.
  - `Oxygen.Cooker.Import.AsyncImportService` and `ImportManifest`.
  - `oxygen::content::lc::Inspection`.
  - `oxygen::content::lc::ValidateRoot`.
  - `SceneDescriptorImportJob` and `oxygen.scene` schema.
  - `EditorModule::AddLooseCookedRoot` / `ClearCookedRoots`.
- Record the chosen execution path:
  - material cook keeps the ED-M05 managed path unless it diverges.
  - full scene/folder/project cook uses native scene descriptor import through
    Interop or a bounded ImportTool fallback.
  - inspect/validate uses native `Inspection`/`ValidateRoot` through Interop if
    wrappers are needed for runtime-compatible strictness.
  - procedural geometry uses derived descriptors under
    `<ProjectRoot>/.pipeline/Geometry` with manifest `geometry-descriptor` jobs.
  - manifest validation uses the native `oxygen.import-manifest.schema.json`
    before any import/build execution.
- Record manifest layout policy for each authored mount:
  - physical output root is `<ProjectRoot>/.cooked/<MountName>`.
  - manifest `layout.virtual_mount_root` is `/<MountName>`.
  - cooked-index virtual paths must round-trip to `asset:///<MountName>/...`.
- Audit and record every current direct cooked-root refresh publisher:
  - remove or reroute `AssetsCookedMessage` publication from scene save,
    material save/cook, Content Browser import/cook, and catalog-only refresh
    paths before ED-M07.7 cook orchestration lands.
  - workspace cooked-root refresh must be triggered only by validated
    `IContentPipelineService` results.
- Add `IMPLEMENTATION_STATUS.md` note only after the decision is documented.

### ED-M07.2 - Operation Vocabulary And Contracts

- Add constants to `Oxygen.Core.Diagnostics`:
  - `Content.Descriptor.Generate`
  - `Content.Manifest.Generate`
  - `Content.Import`
  - `Content.Cook.Asset`
  - `Content.Cook.Scene`
  - `Content.Cook.Folder`
  - `Content.Cook.Project`
  - `Content.CookedOutput.Inspect`
  - `Content.CookedOutput.Validate`
  - `Content.Catalog.Refresh`
  - `Runtime.CookedRoot.Refresh`
- Add diagnostic constants:
  - `OXE.CONTENTPIPELINE.SCENE.DescriptorGenerationFailed`
  - `OXE.CONTENTPIPELINE.SCENE.UnsupportedField`
  - `OXE.CONTENTPIPELINE.GEOMETRY.DescriptorGenerationFailed`
  - `OXE.CONTENTPIPELINE.MANIFEST.GenerationFailed`
  - `OXE.ASSETIMPORT.SourceMissing`
  - `OXE.ASSETIMPORT.ImportFailed`
  - `OXE.ASSETCOOK.CookFailed`
  - `OXE.ASSETCOOK.IndexInvalid`
  - `OXE.CONTENTPIPELINE.INSPECT.Failed`
  - `OXE.CONTENTPIPELINE.VALIDATE.Failed`
  - `OXE.ASSETID.RefreshFailed`
  - `OXE.ASSETMOUNT.RefreshFailed`
- Reuse existing codes:
  - catalog browse/query failures keep `OXE.ASSETID.QueryFailed`; post-cook
    catalog refresh failures use `OXE.ASSETID.RefreshFailed`.
  - material-document cook keeps ED-M05 material cook diagnostic codes when the
    operation remains `Material.Cook`.
- Add ContentPipeline contracts:
  - `IContentPipelineService`
  - `ContentCookScope`
  - `ContentCookInput`
  - `ContentCookResult`
    - includes the complete workflow `Diagnostics` list; UI command producers
      publish that list directly instead of validation diagnostics only.
  - `ContentCookedAsset`
  - `CookInspectionResult` with `Succeeded` and inspection diagnostics
  - `CookValidationResult`
  - `IEngineContentPipelineApi`
  - `ISceneDescriptorGenerator`
  - `IProceduralGeometryDescriptorService`

### ED-M07.3 - Engine Content Pipeline Adapter

- Implement the chosen `IEngineContentPipelineApi` path.
- If using Interop:
  - add only a narrow managed `OxygenContentPipeline` facade for import
    manifest, loose cooked inspect, and loose cooked validate.
  - expose `ImportAsync(ContentImportManifest)`, `InspectLooseCookedRoot`, and
    `ValidateLooseCookedRoot`; do not expose native structs.
  - catch native exceptions at the C++/CLI boundary and return managed failure
    DTOs with native messages preserved as technical diagnostics.
  - keep lifetime under the same host/runtime composition that owns the engine
    context; calls may originate from background editor workflows.
  - return managed DTOs; do not leak native structs into editor contracts.
- If using ImportTool fallback:
  - resolve the tool path from the same engine install/runtime discovery policy
    used by the editor.
  - serialize temporary manifest files inside the fallback adapter only.
  - pass `--no-tui` and capture stdout/stderr.
  - parse report files where available; preserve native diagnostic text as
    technical detail.
- Add tests for command construction/result adaptation without launching the
  full editor.

### ED-M07.4 - Scene Descriptor Generation

- Implement `ISceneDescriptorGenerator`.
- Map editor scene authoring to `oxygen.scene` v3:
  - nodes and parent indexes.
  - transforms.
  - geometry renderables.
  - first material slot as `material_ref`.
  - perspective cameras.
  - directional lights and sun/environment flags.
  - supported environment fields.
- Normalize native descriptor `name` from the scene asset file stem:
  - strip `.oscene.json`.
  - replace invalid identifier characters with `_`.
  - prefix `_` if the first character is invalid.
  - truncate to 63 characters.
  - fail descriptor generation if no valid identifier remains.
- Emit environment data only as a complete engine-default payload with
  supported editor overrides applied:
  - `AtmosphereEnabled` overlays `sky_atmosphere.enabled`.
  - `SunNodeId` is represented through the selected directional light's
    `is_sun_light` / `environment_contribution` fields.
  - exposure, tone, and background editor fields produce
    `OXE.CONTENTPIPELINE.SCENE.UnsupportedField` warnings in ED-M07.
  - if complete native defaults are unavailable, omit `environment` rather
    than emitting partial native environment subobjects.
- Normalize authored asset URIs to native descriptor paths:
  - `asset:///<Mount>/<Path>.omat.json` -> `/<Mount>/<Path>.omat`
  - `asset:///<Mount>/<Path>.ogeo.json` -> `/<Mount>/<Path>.ogeo`
  - `asset:///<Mount>/<Path>.oscene.json` -> `/<Mount>/<Path>.oscene`
  - reject invalid scheme, mount, or extension combinations.
- Reject empty scenes before native cook and publish a useful diagnostic.
- Unsupported ED-M04 or future scene fields produce warnings that name the
  field; they are not silently dropped.
- Ensure generated descriptor path is derived/cache state, not authoring
  identity.

### ED-M07.5 - Procedural Geometry Descriptor Resolution

- Implement `IProceduralGeometryDescriptorService`.
- Resolve generated/basic shape geometry URIs used by editor scenes.
- Produce deterministic derived descriptor inputs under
  `<ProjectRoot>/.pipeline/Geometry` and add manifest `geometry-descriptor`
  jobs for them.
- Fail scene cook if a renderable would otherwise cook with an invalid geometry
  key.
- Add tests for the procedural geometry URIs emitted by the editor quick-add
  commands: cube, sphere, and plane if present in the generated asset catalog.

### ED-M07.6 - Manifest Builder And Dependency Expansion

- Implement manifest/request builder for:
  - current scene.
  - one asset.
  - selected folder.
  - full project.
- Dependency rules:
  - scene depends on geometry and material references.
  - material descriptor dependencies remain scalar-only in V0.1; texture
    material editing is out of scope.
  - raw source media under `Content/SourceMedia` is importable only when a
    supported importer exists for the selected file.
- Generated manifest must validate against the native
  `oxygen.import-manifest.schema.json` contract before execution. ED-M07 uses a
  managed schema-aligned validator for the accepted V0.1 manifest subset;
  native request/builder construction is a secondary adapter check, not the
  primary validation path.
- Manifest serialization must use the native schema property names:
  `version`, `output`, `layout`, `virtual_mount_root`, `jobs`, `id`, `type`,
  `source`, `depends_on`, `name`.
- For each authored mount, manifest `output` is
  `<ProjectRoot>/.cooked/<MountName>` and `layout.virtual_mount_root` is
  `/<MountName>`.
- Add tests for scene dependency ordering and source path normalization.

### ED-M07.7 - Cook Orchestration

- Implement `IContentPipelineService`.
- Wire commands:
  - `Cook Current Scene`
  - `Cook Asset`
  - `Cook Folder`
  - `Cook Project`
- Execution order:
  1. resolve cook scope.
  2. generate descriptors.
  3. generate manifest/request set.
  4. execute import/cook.
  5. inspect cooked output.
  6. validate cooked output.
  7. refresh catalog.
  8. publish the operation result, including catalog-refresh failure if it
     happened.
  9. request runtime mount refresh only on validation success and successful
     catalog refresh.
- Do not reintroduce `ProjectManagerService.SaveSceneAsync` cook side effects.
- Remove or reroute all direct `AssetsCookedMessage` publishers outside
  validated content-pipeline results. Scene save, material save/cook, Content
  Browser import/cook, and catalog-only refresh paths may refresh catalogs but
  must not refresh runtime mounts without validation.

### ED-M07.8 - Inspect, Validate, Catalog Refresh, Mount Refresh

- Add user actions for inspect and validate cooked output.
- Inspect reads `container.index.bin` and presents asset/file summary.
- Native inspect/validate failures return one synthesized diagnostic with the
  native exception message preserved as technical detail.
- Validate runs before mount and blocks mount on failure.
- Catalog refresh updates Content Browser rows without editor restart.
- Runtime mount refresh uses existing workspace/runtime path and frame-start
  engine root synchronization, and mounts only the validated root(s) returned
  by the successful cook/validate result.
- Mount failures are `AssetMount` diagnostics and do not rewrite cook result.

### ED-M07.9 - UI Integration

- Add Content Browser commands:
  - toolbar/context `Cook`
  - `Cook Folder`
  - `Inspect Cooked Output`
  - `Validate Cooked Output`
  - `Refresh`
- Add scene document command/menu affordance for `Cook Current Scene`.
- Use existing result/output panels for details; no new pipeline panel in
  ED-M07.
- Keep UI text concrete:
  - show source asset URI or selected folder.
  - show cooked root.
  - show validation/mount status.

### ED-M07.10 - Tests And Closure Sweep

- Add focused tests for:
  - descriptor generation.
  - manifest dependency ordering.
  - native API adapter result mapping.
  - validation blocking mount.
  - catalog refresh notification after successful cook.
  - no save-time cook side effect.
- Run only approved MSBuild-based validation if local build validation is
  needed. Do not use `dotnet build`.
- Record manual validation scenarios before asking for user validation.

## 7. Project/File Touch Points

Likely touched:

- `projects/Oxygen.Editor.ContentPipeline/src/...`
- `projects/Oxygen.Editor.ContentPipeline/tests/...`
- `projects/Oxygen.Core/src/Diagnostics/...`
- `projects/Oxygen.Editor.Interop/src/...` only for narrow engine content API
  wrappers if ED-M07.1 chooses that route.
- `projects/Oxygen.Editor.ContentBrowser/src/...`
- `projects/Oxygen.Editor.WorldEditor/src/...`
- `projects/Oxygen.Editor.Runtime/src/Engine/...`
- `projects/Oxygen.Editor.Projects/src/...` read-only service consumption;
  no cook execution moves here.
- `projects/Oxygen.Assets/src/...` only if a reusable primitive is missing and
  belongs below editor workflows.
- native `projects/Oxygen.Engine/src/Oxygen/Cooker/...` only if audit finds an
  engine API gap that must be closed at the source.

Docs/ledger:

- `design/editor/lld/content-pipeline.md`
- `design/editor/lld/diagnostics-operation-results.md`
- `design/editor/lld/project-services.md`
- `design/editor/lld/runtime-integration.md`
- `design/editor/lld/asset-primitives.md`
- `design/editor/PLAN.md`
- `design/editor/IMPLEMENTATION_STATUS.md`
- `design/editor/plan/README.md`

## 8. Dependency And Execution Risks

| Risk | Mitigation |
| --- | --- |
| Editor duplicates native cooker behavior. | ED-M07.1 audits `Oxygen.Cooker` and records the chosen Interop/ImportTool/managed path before implementation. |
| Managed scene writer omits material/environment data. | Full scene cook uses native `oxygen.scene` descriptor import unless equivalent managed coverage is intentionally added. |
| Material/geometry dependencies cook after scene. | Manifest builder adds dependency edges before scene jobs. |
| Validation is skipped because cook succeeded. | Service contract requires inspect/validate before mount refresh. |
| Cooked paths leak into scene/material/project files. | Tests assert authoring files persist asset URIs only. |
| Runtime mount refresh happens mid-frame. | Runtime path stays through `IEngineService` and `EditorModule` frame-start root sync. |
| Multi-viewport gets pulled back in. | Explicit non-scope; only supported single viewport/manual validation may be referenced. |

## 9. Validation Gates

Source/test validation expected before user manual validation:

1. Descriptor generation test maps transform, geometry URI, material URI,
   perspective camera, directional light, and supported environment fields.
2. Descriptor generation rejects empty scene with a visible diagnostic.
3. Procedural geometry resolver prevents invalid geometry keys.
4. URI normalization maps authored `.omat.json`/`.ogeo.json` asset URIs to
   native `.omat`/`.ogeo` descriptor paths and rejects invalid inputs.
5. Native scene descriptor name normalization handles scene file names with
   spaces and preserves the authored display name/path outside the descriptor
   identifier.
6. Environment mapping emits full defaults plus supported overrides or omits
   the block with unsupported-field warnings; it never emits partial native
   environment subobjects.
7. Manifest builder orders scene dependencies after material/geometry jobs.
8. Manifest writer emits native schema property names and mount layout:
   `.cooked/Content` with `virtual_mount_root=/Content`.
9. Manifest validation uses the native JSON schema before execution.
10. Cook orchestration does not call `LooseCookedBuildService` directly after a
   managed `ImportService.ImportAsync` path.
11. Validation failure blocks runtime mount refresh.
12. Successful cook publishes catalog refresh notification.
13. Scene save, material save/cook, Content Browser import/cook, and
    catalog-only refresh paths do not publish unvalidated cooked-root refresh
    messages.

Manual validation scenarios to hand to the user:

1. Create/open project, open starter scene, cook current scene.
2. Verify Content Browser shows cooked/stale state changes without restart.
3. Inspect cooked output and see scene/material/geometry entries.
4. Validate cooked output and see success.
5. After validation, runtime cooked-root refresh runs without breaking the
   supported single viewport.
6. Break one referenced material or geometry, cook current scene, and see a
   precise visible failure.
7. Using a controlled developer-only setup, break `container.index.bin`, run
   validate/refresh, and confirm mount is blocked with a validation diagnostic.

## 10. Status Ledger Hook

Before implementation starts:

- mark the ED-M07 LLD review checkbox only after review acceptance.
- mark this detailed plan checkbox only after plan review acceptance.
- update the detailed plan tracker to point to this milestone plan.
- keep `ED-WP05.1-manifest-driven-cooking.md` as historical/deferred context
  once this plan is accepted.

ED-M07 closes only after the user manually validates the scenarios in §9 and
one validation ledger row is recorded.
