# ED-M05 Scalar Material Authoring Detailed Plan

Status: `validated`

Planning note:

- ED-M05 remains the scalar material document/editor/picker/assignment
  milestone.
- Default project layout, predefined templates, material creation target
  resolution, Content Browser folder refresh, and picker row filtering are now
  finalized by
  [ED-M06A-game-project-layout-and-template-standardization.md](./ED-M06A-game-project-layout-and-template-standardization.md).
- ED-M05 validation closed after ED-M06A confirmed material create, save,
  browser refresh, and picker behavior against the accepted game project
  layout.

## 1. Purpose

Make scalar material authoring a real V0.1 editor workflow. ED-M05 adds
material documents, scalar PBR editing, material asset create/open, material
picker identity, geometry assignment through `AssetReference<MaterialAsset>`,
descriptor save/reopen, and the minimum material cook/catalog-state slice.

ED-M05 deliberately does not close the full content pipeline. Scene descriptor
generation, full dependency cooking, mount refresh, and standalone parity stay
in ED-M07/ED-M08.

The first implementation pass was rejected because the visible workflow did not
make content placement, cooked output placement, or picker refresh behavior
clear enough, and because the Material Editor used one-off controls instead of
the shared editor property controls. The corrective pass treats those as
milestone work, not polish.

## 2. PRD Traceability

| ID | ED-M05 Coverage |
| --- | --- |
| `GOAL-002` | Geometry material assignment persists as scene authoring data. |
| `GOAL-004` | Scalar material editor baseline is created. |
| `GOAL-005` | Material descriptor/cook/catalog state is visible and understandable. |
| `GOAL-006` | Material authoring, picker, save, and cook failures are visible. |
| `REQ-010` | Users can create/open scalar material assets. |
| `REQ-011` | Users can inspect/edit scalar material properties. |
| `REQ-012` | Users can assign material assets to geometry. |
| `REQ-013` | Users can select material assets through content browser/picker identity. |
| `REQ-014` | Material values save/reopen and cook where supported. |
| `REQ-021` | Material picker exposes asset state without raw cooked-path authoring. |
| `REQ-022` | Save/cook/picker failures produce visible operation results. |
| `REQ-024` | Diagnostics distinguish material authoring, document, asset identity, content pipeline, and live sync. |
| `REQ-037` | Supported material and material-slot data round-trips without manual repair. |
| `SUCCESS-002` | Material assignment and scene data survive save/reopen. |
| `SUCCESS-004` | Scalar material data is available for later cook/parity milestones. |
| `SUCCESS-007` | Material assets can be created, edited, assigned, cooked, and previewed at V0.1 baseline level. |

## 3. Required LLDs

Only these LLDs gate ED-M05 implementation:

- [material-editor.md](../lld/material-editor.md)
- [content-browser-asset-identity.md](../lld/content-browser-asset-identity.md), ED-M05 material-picker slice only
- [asset-primitives.md](../lld/asset-primitives.md), ED-M05 material slice only
- [content-pipeline.md](../lld/content-pipeline.md), ED-M05 material cook/catalog slice only
- [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md), ED-M05 operation kinds/domains/prefixes

Supporting context:

- [property-inspector.md](../lld/property-inspector.md), for the Geometry material slot command seam from ED-M04.
- [documents-and-commands.md](../lld/documents-and-commands.md), for dirty/undo command patterns.
- [project-services.md](../lld/project-services.md), for active project root and content roots.

## 4. Scope

ED-M05 includes:

- Create `Oxygen.Editor.MaterialEditor` as the material document/UI owner if it
  does not already exist, following the repo's standard project layout.
- Create `Oxygen.Editor.ContentPipeline` or the minimum approved material-slice
  service location if the project does not yet exist. The detailed code change
  must not hide cook orchestration in `Oxygen.Editor.Projects`.
- Add material diagnostics and operation-kind constants:
  - `Material.Create`
  - `Material.Open`
  - `Material.EditScalar`
  - `Material.Save`
  - `Material.Cook`
  - `Material.AssignToGeometry`
  - `Asset.Query`
  - `Asset.Resolve`
  - `Material.Pick`
- Add `FailureDomain.MaterialAuthoring` and `FailureDomain.AssetIdentity` if
  missing in code, plus ED-M05 diagnostic code prefixes.
- Implement a material document service over existing `Oxygen.Assets`
  material primitives.
- Implement scalar material editor UI:
  - asset identity/status header;
  - deterministic CPU swatch/preview;
  - grouped scalar PBR fields using shared editor property sections and
    DroidNet numeric controls;
  - Advanced/Raw read-only texture/schema metadata;
  - Save and Cook actions;
  - visible descriptor/cook state.
- Implement material create/open flow from Content Browser and/or material
  picker:
  - default target folder `/Content/Materials`;
  - selected folder is honored only when it is already under `/Content`;
  - name + target folder prompt with mount-oriented path text;
  - create `{Name}.omat.json`;
  - open material editor document.
- Implement `IMaterialPickerService` and picker UI for material assignment.
- Replace ED-M04's temporary Geometry material-list behavior with the ED-M05
  material picker service while preserving the existing Geometry section
  ergonomics.
- Implement material descriptor save/reopen through `MaterialSourceWriter` and
  `MaterialSourceReader`, preserving unedited fields.
- Implement minimum material cook through `ImportService.ImportAsync`; its
  build phase invokes `LooseCookedBuildService`.
- Refresh or observe catalog state so picker rows can move through
  `Source -> Cooked -> Stale -> Missing/Broken` for the material slice.
- Refresh the Geometry material picker from the live catalog when its flyout is
  opened; watcher/index delivery is not enough for a user-visible picker.
- Route live material assignment sync warnings through the existing
  `Unsupported` policy; do not wire engine material override APIs in ED-M05.
- Add focused non-UI tests and prepare exact manual validation scenarios.

## 5. Non-Scope

ED-M05 does not include:

- Texture authoring/editing or texture asset picking.
- Material graph, shader graph, custom shader, layered material, or procedural
  shader workflows.
- Engine material preview view. ED-M05 uses a deterministic CPU swatch.
- Scene descriptor generation.
- Full dependency graph cooking.
- Project/folder/scene cook workflows.
- Cooked-root mount refresh after material cook.
- Standalone runtime parity.
- Broad Content Browser redesign beyond the material create/open/picker slice.
- Full ED-M06 source/generated/cooked/pak/broken browser views.
- Full ED-M07 content pipeline inspect/manifest/mount work.
- Viewport material override live preview. The scene material-slot sync result
  remains `Unsupported` by V0.1 editor policy.
- Adding new editor-only material JSON schemas unless the LLDs are reopened.

## 5.1 User Workflow Contract

ED-M05 must present one simple material workflow:

```text
New Material
  target folder: /Content/Materials
  creates:       <ProjectRoot>/Content/Materials/Gold.omat.json
  asset URI:     asset:///Content/Materials/Gold.omat.json

Save
  writes the same oxygen.material.v1 descriptor; scene files store only the asset URI.

Cook
  imports asset:///Content/Materials/Gold.omat.json
  writes: <ProjectRoot>/.cooked/Content/Materials/Gold.omat
  index:  <ProjectRoot>/.cooked/Content/container.index.bin

Assign
  Geometry > Material opens the picker, refreshes the catalog, and lists the
  newly-created material without requiring editor restart.
```

For V0.1, material creation is content-mount oriented. Authoring target
selection is governed by `IAuthoringTargetResolver` from ED-M06A. The
`/Content/Materials` default shown here is the fallback returned for
project-root, non-authoring, non-matching, or derived-root selections.

Material picker state vocabulary is superseded by ED-M06/ED-M06A: material
descriptors use `PrimaryState = Descriptor`; cooked companions use
`DerivedState = Cooked` or `Stale`; compact badges follow the ED-M06 asset
identity precedence rule.

## 6. Implementation Sequence

### ED-M05.1 - LLD Lock, Baseline Audit, And Project Placement

Goal: start implementation with the exact material/pipeline surface known.

Tasks:

- Confirm the corrected ED-M05 LLDs are accepted.
- Inventory current material-related code:
  - `Oxygen.Assets` material source, importer, cooked writer, import service,
    loose cooked build service.
  - `ProjectAssetCatalog`, generated/default material catalog entries, and
    current Content Browser asset panes.
  - ED-M04 `GeometryViewModel` material slot UI and command usage.
- Decide physical placement before code:
  - `projects/Oxygen.Editor.MaterialEditor` for material document UI/service.
  - `projects/Oxygen.Editor.ContentPipeline` for material cook orchestration.
  - material picker contracts under `Oxygen.Editor.ContentBrowser`, unless the
    codebase already has a better content-browser contracts location.
- Confirm test hosts. If a new project is created, create the matching tests
  project only when it has real non-UI seams to test.
- Record any existing temporary ED-M04 material-slot behavior that ED-M05 will
  replace.

Exit:

- LLDs accepted.
- Project/module placement is explicit.
- File/test touch list is confirmed.

### ED-M05.2 - Diagnostics And Core Contracts

Goal: land stable vocabulary before producers publish results.

Tasks:

- Add operation-kind constants for ED-M05 material and asset operations.
- Add `FailureDomain.MaterialAuthoring` and `FailureDomain.AssetIdentity` if
  missing.
- Add diagnostic-code constants for:
  - `OXE.MATERIAL.Field.Rejected`
  - `OXE.MATERIAL.Name.Invalid`
  - `OXE.DOCUMENT.MATERIAL.SaveFailed`
  - `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty`
  - `OXE.CONTENTPIPELINE.MATERIAL.SourceMissing`
  - `OXE.CONTENTPIPELINE.MATERIAL.InvalidDescriptor`
  - `OXE.CONTENTPIPELINE.MATERIAL.CookFailed`
  - `OXE.CONTENTPIPELINE.MATERIAL.IndexFailed`
  - `OXE.ASSETID.MATERIAL.Missing`
  - `OXE.ASSETID.MATERIAL.Broken`
  - `OXE.ASSETID.MATERIAL.NotCooked`
  - `OXE.LIVESYNC.MATERIAL.Unsupported`
- Define shared ED-M05 records in their owning projects:
  - `MaterialCookRequest`, `MaterialCookResult`, `MaterialCookState` →
    `Oxygen.Editor.ContentPipeline`.
  - `MaterialPickerResult`, `MaterialPreviewColor`, `MaterialPickerFilter` →
    `Oxygen.Editor.ContentBrowser`.
  - `MaterialEditResult`, `MaterialSaveResult` →
    `Oxygen.Editor.MaterialEditor`.
- Do not introduce enums where current diagnostics contracts use stable
  strings unless the diagnostics LLD is updated first.

Exit:

- Later ED-M05 services compile against stable operation/domain/code names.

### ED-M05.3 - Material Document Service And Descriptor Round Trip

Goal: make material assets real editor documents before UI is wired.

Tasks:

- Implement `IMaterialDocumentService`.
- Implement `CreateAsync`:
  - build target URI under an active content root;
  - create default `oxygen.material.v1` PBR scalar descriptor;
  - handle name conflicts visibly.
- Implement `OpenAsync`:
  - read `*.omat.json`;
  - parse through `MaterialSourceReader`;
  - classify missing/invalid descriptors.
- Implement `EditScalarAsync`:
  - replace immutable `MaterialSource` branches;
  - clamp valid numeric ranges on commit;
  - reject non-numeric input with `MaterialAuthoring` diagnostic;
  - preserve unedited texture refs/schema/raw metadata.
- Implement material document dirty state and undo/redo:
  - one undo entry per committed slider/drag session;
  - no descriptor write until `SaveAsync`.
- Implement `SaveAsync`:
  - write through `MaterialSourceWriter`;
  - use temp file + replace/rename semantics where storage supports it;
  - leave dirty state set on failure.
- Add non-UI tests for create, open, edit, undo/redo session behavior,
  save/reopen, invalid input, and unedited field preservation.

Exit:

- A material can be created, edited, saved, reopened, and round-tripped without
  any UI-specific code.

### ED-M05.4 - Material Editor UI

Goal: expose the material document service as a usable editor surface.

Tasks:

- Add Material Editor document route/tab/pane according to the existing editor
  document pattern.
- Build UI using the same ergonomic property-editor model as Transform and
  Geometry: shared `PropertiesExpander` sections, shared `PropertyCard` rows,
  and DroidNet `NumberBox` controls for scalar numeric editing.

```text
+--------------------------------------------------------------+
| Material: Gold                                      [Save]    |
| [swatch preview]  asset:///Content/Materials/Gold.omat.json  |
| Descriptor: Dirty   Cooked: Stale                    [Cook]  |
+--------------------------------------------------------------+
| Identity                                                     |
|   Asset URI      asset:///Content/Materials/Gold...   [copy] |
|   Asset GUID     0f2d...                              [copy] |
| PBR Metallic Roughness                                      |
|   Base Color     R [ ] G [ ] B [ ] A [ ]                    |
|   Surface        Metallic [ ] Roughness [ ]                 |
| > Advanced                                                  |
|   Alpha          Mode [ Opaque v ] Cutoff [ ]               |
|   Rendering      Double Sided [toggle]                      |
+--------------------------------------------------------------+
```

- Do not implement one-off numeric controls or one-off section headers.
- Treat the material source file stem as the canonical asset name. The Material
  Editor shows that name in the header and saves `MaterialSource.Name` from the
  `*.omat.json` file stem; it does not allow descriptor-only renames.
- Show the material asset URI and editor-side asset GUID in Identity as
  non-editable text, each with a copy button. Do not store the GUID in
  `oxygen.material.v1`.
- Promote the existing property-section controls to a shared editor-controls
  location if MaterialEditor cannot depend on WorldEditor without violating
  project boundaries.
- Preview is deterministic CPU swatch/ball; no engine material preview.
- Show read-only texture refs under Raw/Advanced when present.
- Show descriptor/cook state in the header/status strip.
- Wire Save/Cook buttons to document and pipeline services.
- Keep UI dense and editor-like; do not make a generic JSON editor.

Exit:

- User can visually create/open/edit/save scalar material values with clear
  state feedback.

### ED-M05.5 - Material Picker And Content Browser Slice

Goal: make material identity selectable through a reusable picker.

Tasks:

- Implement `IMaterialPickerService` over `IAssetCatalog`.
- Query with `AssetQueryScope.All` for global picker search.
- Resolve exact current assignments with `Roots = [assetUri]` and
  `AssetQueryTraversal.Self`.
- Subscribe to `AssetChange.Kind` and refresh rows on `Added`, `Removed`,
  `Updated`, `Relocated`.
- On `Relocated`, use `AssetChange.PreviousUri` to invalidate the previous-URI
  row.
- Force a picker refresh when the Geometry material flyout opens so newly
  created descriptors are visible even if filesystem watcher delivery is
  delayed.
- Compute material row state through the ED-M06/ED-M06A shared browser
  provider: `PrimaryState = Descriptor` for material descriptors and
  `DerivedState = Cooked` or `Stale` for cooked companions.
- Populate `DescriptorPath` / `CookedPath` as diagnostic affordances only,
  never persisted.
- Implement `BaseColorPreview` as `MaterialPreviewColor` from descriptor base
  color where available.
- Add picker UI:

```text
+--------------------------------------------------+
| Pick Material                       [search...]  |
| [x] Generated  [x] Descriptor  [x] Cooked  [ ] Miss. |
+--------------------------------------------------+
| [swatch] Default          Engine/Generated   GEN |
| [swatch] Gold             Content/Materials  DESC |
| [warn ]  MissingMat       unresolved         MIS |
+--------------------------------------------------+
| [Open in Material Editor] [Create New] [Clear]   |
+--------------------------------------------------+
```

- Implement `Create New` from picker:
  - prompt name + target folder;
  - target folder is resolved by `IAuthoringTargetResolver` per ED-M06A;
  - `/Content/Materials` is the fallback for project-root and invalid
    authoring-target selections;
  - create/open material document;
  - assign only if launched from a material slot and the user confirms.
- Ensure missing/broken current assignment row is shown when picker opens from
  that slot even when `IncludeMissing = false`.
- Add non-UI tests for filtering, missing resolution, change-stream refresh,
  and persistence-neutral picker result fields.

Exit:

- Picker returns typed material identity and never asks consumers to persist
  descriptor/cooked filesystem paths.

### ED-M05.6 - Geometry Material Assignment Integration

Goal: replace the ED-M04 temporary material slot affordance with the ED-M05
picker flow.

Tasks:

- Update Geometry material slot UI to open `IMaterialPickerService`.
- Keep the ED-M04 visual quality:
  - row in Geometry section;
  - swatch/placeholder;
  - material name or URI;
  - state badge;
  - warning icon for missing/broken.
- Assign selected URI through existing
  `EditMaterialSlotAsync(ctx, nodeIds, slotIndex: 0, materialUri, session)`.
- `<None>` calls `EditMaterialSlotAsync(..., materialUri: null)`.
- Multi-selection:
  - same URI shows value;
  - different URIs show `--`;
  - pick writes same URI to all selected geometry nodes in one command;
  - `<None>` clears all in one command.
- Preserve unresolved URI verbatim across save/reopen.
- Live sync `Unsupported` warning remains non-blocking.
- Add tests around command integration where non-UI seams already exist.

Exit:

- Geometry assignment is identity-based and uses the ED-M05 picker, not the
  temporary ED-M04 local list.

### ED-M05.7 - Material Cook Slice And Catalog State

Goal: prove the minimum material descriptor/cook/catalog path without taking
ED-M07 scope.

Tasks:

- Implement `IMaterialCookService` in ContentPipeline.
- Reject dirty descriptors before import/cook with
  `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty`.
- Run `ImportService.ImportAsync` for the material source path. Its Import
  phase dispatches to `MaterialSourceImporter`; its Build phase invokes
  `LooseCookedBuildService` for the imported material set.
- Pass the authoring mount name separately from the filesystem-relative source
  path, and pass an explicit cooked virtual path derived from the material asset
  URI. This prevents custom mount folder names from producing cooked output
  under the wrong `.cooked` folder.
- Verify:
  - cooked `.omat` exists under `<ProjectRoot>/.cooked/<Mount>/...`;
  - loose cooked index contains the material virtual path;
  - `LooseCookedIndexAssetCatalog` can expose the cooked material URI.
- Update material document `MaterialCookState`.
- Notify or refresh picker/catalog state so the visible row changes after cook.
- Do not mount cooked roots automatically.
- Do not claim runtime/standalone parity.
- Add focused tests for dirty rejection, successful cook, invalid descriptor,
  and source-to-cooked catalog visibility. If filesystem watchers make a direct
  UI refresh test flaky, test the service state transition and leave row update
  as manual validation.

Exit:

- One saved material descriptor can be cooked and become visible as cooked
  catalog state.

### ED-M05.8 - Validation, Cleanup, And Ledger

Goal: close ED-M05 with real evidence and no hidden deferred work.

Tasks:

- Run targeted tests through MSBuild only. Do not use `dotnet`.
- Remove or explicitly mark temporary ED-M04 material-slot code that was
  replaced.
- Update `IMPLEMENTATION_STATUS.md` after implementation and manual validation.
- Prepare exact manual validation scenarios for the user.
- Record any true limitation as an operation result or a named deferred item,
  not as a generic gap.

Exit:

- ED-M05 is ready for user manual validation.

## 7. Project/File Touch Points

Likely new projects:

- `projects/Oxygen.Editor.MaterialEditor/`
- `projects/Oxygen.Editor.MaterialEditor/tests/` if non-UI material document
  seams land there.
- `projects/Oxygen.Editor.ContentPipeline/`
- `projects/Oxygen.Editor.ContentPipeline/tests/` if material cook service has
  real testable seams.

Likely existing files/projects:

- `projects/Oxygen.Core/src/Diagnostics/FailureDomain.cs`
- `projects/Oxygen.Core/src/Diagnostics/DiagnosticCodes.cs`
- new `projects/Oxygen.Core/src/Diagnostics/MaterialOperationKinds.cs` or
  equivalent operation-kind constants.
- `projects/Oxygen.Assets/src/Import/Materials/*`
- `projects/Oxygen.Assets/src/Import/ImportService.cs`
- `projects/Oxygen.Assets/src/Cook/LooseCookedBuildService.cs`
- `projects/Oxygen.Assets/src/Cook/CookedMaterialWriter.cs`
- `projects/Oxygen.Assets/tests/*Material*`
- `projects/Oxygen.Editor.Projects/src/*` active project/context/content-root
  service surfaces, consumed read-only for project root and content roots.
- `projects/Oxygen.Editor.ContentBrowser/src/Infrastructure/Assets/ProjectAssetCatalog.cs`
- new material picker services/views under `projects/Oxygen.Editor.ContentBrowser/src/`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/Geometry/GeometryViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/Geometry/GeometryView.xaml`
- `projects/Oxygen.Editor.WorldEditor/src/Documents/Commands/SceneDocumentCommandService.cs`
- `projects/Oxygen.Editor.WorldEditor/tests/SceneExplorer/*`
- editor host composition/DI files that register the new MaterialEditor and
  ContentPipeline services.

## 8. Dependency And Migration Risks

| Risk | Mitigation |
| --- | --- |
| Material editor invents editor-only JSON. | Use `oxygen.material.v1` and `MaterialSourceReader/Writer`; reopen LLDs before any schema divergence. |
| Cook orchestration sneaks into `Oxygen.Editor.Projects`. | ContentPipeline owns `IMaterialCookService`; Projects provides project root/content roots only. |
| Picker persists descriptor/cooked filesystem paths. | Scene data stores only `AssetReference<MaterialAsset>.Uri`; tests inspect persisted JSON. |
| `MaterialDocument.Source` and `MaterialAsset.Source` drift. | `MaterialDocument.Source` is authoritative while editing; `Asset` is identity-only. |
| Save and cook become one hidden operation. | Save writes descriptor; Cook is explicit and rejects dirty descriptor. |
| Material cook turns into full pipeline scope. | ED-M05 cooks one material slice only; no scene manifests, dependency graph, mount refresh, or standalone. |
| UI becomes a generic JSON form. | Build a focused material editor with grouped scalar controls, swatch preview, state strip, and advanced/raw disclosure. |
| Missing material references get auto-cleared. | Missing URI remains persisted; picker/slot show missing state and re-pick/clear affordances. |
| Filesystem watcher refresh is flaky. | Test service/catalog state directly; validate visible row transitions manually. |
| Engine material preview scope leaks in. | CPU swatch only; live scene material sync remains `Unsupported`. |

## 9. Manual Validation Script

The user should validate these visually after implementation and build:

1. Open the editor and Vortex project.
2. From Content Browser or material picker, create a material named `Gold`
   under `Content/Materials/`.
3. Material Editor opens with:
   - material name and URI visible;
   - deterministic swatch;
   - descriptor state;
   - cook state;
   - grouped Surface/Alpha/Advanced sections.
4. Edit base color, metallic, roughness, alpha mode/cutoff, and double-sided.
   Values clamp/reject according to the LLD and the UI shows the committed
   value.
5. Undo/redo a slider edit: one user drag is one undo entry.
6. Save the material. Close/reopen it. Scalar values are preserved.
7. Attempt Cook while descriptor is dirty. It is rejected with a visible
   `DescriptorDirty` result.
8. Save, then Cook. Cook state becomes `Cooked`; the picker/catalog row updates
   to cooked or an explicit visible limitation is shown.
9. Select a geometry node. Open the material slot picker.
10. Pick `Gold`. The Geometry material row shows swatch/name/state, and the
    scene material slot persists after save/reopen.
11. Multi-select two geometry nodes with different materials. The material slot
    shows `--`; picking one material assigns it to both with one undo entry.
12. Delete or rename the material descriptor outside the editor, then reopen or
    refresh. The assigned slot shows missing/broken state but preserves the URI.
13. Verify live sync unsupported appears as a warning and does not block the
    scene edit.

## 10. Code/Test Validation Gates

State assertions in this section are interpreted through the ED-M06/ED-M06A
browser model: material descriptors are `PrimaryState = Descriptor`; cooked or
stale companions are represented as `DerivedState`.

ED-M05 cannot be called landed until these are true:

1. Material descriptor test: create/edit/save/reopen preserves every V0.1
   scalar field and unedited texture refs.
2. Material edit-session test: 30 slider samples produce one undo entry and no
   descriptor write until Save.
3. Invalid field test: non-numeric or invalid name returns
   `MaterialAuthoring` diagnostics and leaves source unchanged.
4. Static persistence check on `SceneJsonContext` output: scene material slot
   JSON contains the asset URI only, never `DescriptorPath`, `CookedPath`,
   `ThumbnailKey`, or other picker fields.
5. Picker test: generated default material is returned in a fresh project.
6. Picker resolve test: missing assigned URI returns `Missing` without
   rewriting the URI.
7. Picker change test: `AssetChangeKind.Added/Removed/Updated/Relocated`
   refreshes or invalidates material results.
8. Cook dirty test: dirty descriptor cook returns `Rejected` and
   `OXE.CONTENTPIPELINE.MATERIAL.DescriptorDirty`.
9. Cook success test: saved material imports through `ImportService`, whose
   build phase produces loose cooked output, and exposes cooked material through
   `LooseCookedIndexAssetCatalog`.
10. Geometry assignment test: picker result URI flows into
    `EditMaterialSlotAsync`, persists round-trip, and live sync unsupported is
    non-blocking.
11. Manual validation script above is completed by the user.

## 11. Status Ledger Hook

Before implementation:

- ED-M05 required LLDs are accepted.
- this detailed plan is reviewed and accepted.
- `IMPLEMENTATION_STATUS.md` marks the detailed plan as accepted before any
  ED-M05.2 code lands.

After implementation:

- update the ED-M05 checklist in `IMPLEMENTATION_STATUS.md`.
- record one ED-M05 validation ledger row after user manual validation and
  real test evidence.
- keep ED-M06, ED-M07, and ED-M08 deferred scope out of ED-M05 closure.
