# ED-M06 Asset Identity And Content Browser Detailed Plan

Status: `validated`

Planning note:

- ED-M06 remains the shared asset identity, browser row, reducer, provider,
  typed picker, and persistence-boundary milestone.
- Project folder layout, predefined template payloads, browser root
  projection, folder-click refresh, source-media positioning, and default
  authoring target behavior are now finalized by
  [ED-M06A-game-project-layout-and-template-standardization.md](./ED-M06A-game-project-layout-and-template-standardization.md).
- ED-M06 validation closed after ED-M06A confirmed Content Browser navigation
  and material picker filtering against the accepted game project layout.

## 1. Purpose

Make the Content Browser a trustworthy asset identity surface instead of a
thin filesystem view. ED-M06 turns the ED-M05 material picker slice into shared
browser infrastructure that can show authoring descriptors, generated assets,
cooked outputs, stale/missing/broken references, and typed picker results
without leaking raw cooked paths into authoring data.

ED-M06 does not execute cook, mount cooked roots, or prove standalone parity.
It reads existing source/cooked/catalog facts and presents them clearly. Full
cook execution remains `ED-M07`; runtime parity remains `ED-M08`.

## 2. PRD Traceability

| ID | ED-M06 Coverage |
| --- | --- |
| `GOAL-004` | Material/content identity created in ED-M05 becomes browseable and selectable through shared browser rows. |
| `GOAL-005` | Source, descriptor, cooked, stale, missing, and broken asset state is visible. |
| `GOAL-006` | Missing/broken asset and root failures produce visible diagnostics. |
| `REQ-013` | Typed asset picking returns stable asset identity instead of display strings or cooked paths. |
| `REQ-020` | Content Browser exposes project content roots and asset rows as editor concepts. |
| `REQ-021` | Asset state distinguishes authoring descriptors, cooked output, generated assets, and invalid references. |
| `REQ-022` | Browser refresh, asset resolve, missing/broken references, and picker failures produce operation results where user-visible. |
| `REQ-024` | Diagnostics classify asset identity, project content-root, and content-pipeline state failures. |
| `REQ-036` | Asset identity and browser state support later cook/inspect workflows. |
| `REQ-037` | Authoring data persists stable asset URIs and does not serialize browser-only state. |
| `SUCCESS-006` | Import/cook/mount state visibility has the browser side of the contract ready for ED-M07. |
| `SUCCESS-007` | Material assets can be found and assigned through the generalized browser/picker surface. |

## 3. Required LLDs

Only these LLDs gate ED-M06 implementation:

- [content-browser-asset-identity.md](../lld/content-browser-asset-identity.md)
- [asset-primitives.md](../lld/asset-primitives.md)
- [project-services.md](../lld/project-services.md)
- [diagnostics-operation-results.md](../lld/diagnostics-operation-results.md)

Supporting context:

- [material-editor.md](../lld/material-editor.md), for the material rows and
  picker behavior delivered in ED-M05.
- [content-pipeline.md](../lld/content-pipeline.md), for cooked descriptor
  and stale-state facts only. ED-M06 must not invoke cook workflows.
- [property-inspector.md](../lld/property-inspector.md), for Geometry material
  slot consumption of typed picker results.

## 4. Scope

ED-M06 includes:

- Migrate browser/picker asset rows from `GameAsset.AssetType` style records to
  `ContentBrowserAssetItem` and `AssetKind` where rows are rendered or picked.
- Extend the shared `AssetState` vocabulary with `Descriptor`, while preserving
  `Source` for non-descriptor raw/source files.
- Migrate ED-M05 material picker rows in lockstep:
  - material descriptors (`*.omat.json`) publish `PrimaryState = Descriptor`;
  - cooked companions publish `DerivedState = Cooked` or `Stale`;
  - missing/broken references publish `Missing` or `Broken`;
  - consumers must not infer descriptors from legacy `Source` after ED-M06.
- Implement an asset identity reducer that merges generated, source,
  descriptor, cooked, stale, missing, and broken facts into browser rows.
- Implement broken descriptor detection for V0.1 material descriptors:
  - validate off the UI thread;
  - use `MaterialSourceReader`;
  - cache by identity and last-write timestamp;
  - invalidate on `AssetChange`.
- Implement `IContentBrowserAssetProvider` as the shared adapter over the
  existing `IProjectAssetCatalog` composition layer.
- Replace or adapt Content Browser list/tile/detail row models to consume
  provider snapshots.
- Replace `MaterialPickerFilter` internals with an `AssetBrowserFilter`
  projection constrained to `Kinds = { Material }`.
- Preserve the ED-M05 material picker API surface where practical, but migrate
  result semantics from single `State` to `PrimaryState`, `DerivedState`, and
  `RuntimeAvailability`.
- Add copy actions for authored asset URI and, where available, stable asset
  GUID/identity.
- Add visible row diagnostics for missing and broken references.
- Add operation-kind and diagnostic-code constants for ED-M06 browser identity
  workflows.
- Add focused unit tests for reducer behavior, picker projection, content-root
  policy, filter mapping, and broken-state classification.

## 5. Non-Scope

ED-M06 does not include:

- Running import, cook, build, inspect, or mount workflows.
- Treating a cooked file as an authored asset identity.
- Serializing `DescriptorPath`, `CookedPath`, `PrimaryState`, `DerivedState`,
  thumbnails, runtime availability, or diagnostics into scene/material/project
  authoring files.
- Authoritative runtime `Mounted` state. ED-M06 shows `Unknown` or
  `NotMounted` unless later runtime/pipeline work supplies authoritative
  availability.
- Texture authoring, material graph editing, or shader workflows.
- Full ED-M07 content pipeline validation.
- Standalone runtime load or parity validation.
- Broad visual redesign unrelated to asset identity rows, filters, diagnostics,
  and picker behavior.

## 5.1 User Workflow Contract

ED-M06 must make the user's mental model simple:

```text
Content Browser
  Content/
    Materials/
      Red.omat.json     DESC + COOKED or STALE
      MissingMat        MISS
      Broken.omat.json  ERR

Material Picker
  filters rows through the same provider and reducer
  returns asset:///Content/Materials/Red.omat.json
  also exposes Descriptor/Cooked/Stale/Broken badges for the slot UI

Scene/Material Files
  persist only asset:///... identity
  never persist descriptor path, cooked path, row state, thumbnail key, or UI badges
```

Create/open material workflows still default to `/Content/Materials` from
ED-M05. ED-M06 may show `.cooked` facts, but users do not author against the
`.cooked` folder and new material creation must never select `.cooked`,
`.imported`, `.build`, `Scenes`, or synthetic project roots as material
targets.

The Browser should be dense and operational, not decorative:

```text
+ Content -------------------------------------------------------------+
| Roots     | Search material... [Kind: Material] [DESC] [COOK] [ERR] |
| Content   |---------------------------------------------------------|
| Materials | Name          State        Identity                     |
| Scenes    | Red           DESC COOK    asset:///Content/.../Red...  |
|           | Gold          DESC STALE   asset:///Content/.../Gold... |
|           | MissingMat    MISS         asset:///Content/...         |
|           | Broken        ERR          parse failed                 |
+-----------+---------------------------------------------------------+
| Details: asset URI [copy]   GUID [copy]   descriptor/cooked paths   |
+---------------------------------------------------------------------+
```

## 6. Implementation Sequence

### ED-M06.1 - LLD Lock, Baseline Audit, And Plan Acceptance

Goal: enter implementation with the browser identity contract fixed.

Tasks:

- Confirm the ED-M06 LLDs are accepted after review remediation.
- Inventory current browser and picker paths:
  - `GameAsset` and `AssetType`;
  - `AssetsLayoutViewModel` list/tile rows;
  - `AssetsViewModel` item invocation;
  - `ProjectAssetCatalog` composition;
  - `MaterialPickerService`, `MaterialPickerFilter`, and
    `MaterialPickerResult`;
  - Content Browser state and root selection persistence.
- Identify every current consumer that branches on ED-M05 `AssetState.Source`
  for material descriptors and mark it for migration to `Descriptor`.
- Confirm test targets:
  - `Oxygen.Editor.ContentBrowser.Tests`;
  - `Oxygen.Editor.Projects.Tests` for content-root/cook-scope policy only;
  - `Oxygen.Core.Tests` for diagnostic vocabulary constants.
- Do not start source changes until this plan has been reviewed.

Exit:

- LLDs accepted.
- Detailed plan accepted.
- Migration touch list is explicit.

### ED-M06.2 - Diagnostics, Operation Kinds, And State Vocabulary

Goal: land stable names before browser/picker producers publish results.

Tasks:

- Add/verify ED-M06 operation-kind constants:
  - `Asset.Browse`
  - `Asset.Query`
  - `Asset.Resolve`
  - `ContentBrowser.Navigate`
  - `ContentBrowser.Refresh`
  - `Asset.CopyIdentity`
  - `Material.Pick`
- Place operation-kind constants in their owners:
  - `Asset.Browse`, `Asset.Query`, `Asset.Resolve`, and
    `Asset.CopyIdentity` extend `AssetOperationKinds`;
  - `Material.Pick` stays in `MaterialOperationKinds`;
  - `ContentBrowser.Navigate` and `ContentBrowser.Refresh` live in a new
    `ContentBrowserOperationKinds` constants type.
- Do not add `Asset.ReduceState`; reducer errors attach diagnostics to the
  operation that requested browsing, querying, or resolving. Reducer failures
  use `OXE.ASSETID.ReduceFailed` as a child diagnostic on `Asset.Browse`,
  `Asset.Query`, or `Asset.Resolve`.
- Add/verify diagnostic code constants:
  - `OXE.ASSETID.QueryFailed`
  - `OXE.ASSETID.ReduceFailed`
  - `OXE.ASSETID.Resolve.Missing`
  - `OXE.ASSETID.Descriptor.Broken`
  - `OXE.ASSETID.Cooked.Missing`
  - `OXE.PROJECT.CONTENT_ROOT.InvalidSelection`
  - reuse `OXE.CONTENTPIPELINE.Asset.Stale` for stale cooked-output warnings.
- Do not allocate `OXE.CONTENTBROWSER.*` in ED-M06.
- Move the ED-M05 `AssetState` type out of the material-specific namespace
  into a shared Content Browser asset-identity namespace before adding
  `Descriptor`. Migrate `MaterialPickerResult`, `MaterialPickerFilter`,
  `MaterialPickerService`, Geometry material slot consumers, and tests in the
  same slice.
- Add `AssetKind`, `AssetRuntimeAvailability`, and `ContentBrowserAssetItem`
  in the same shared asset-identity namespace. `AssetRuntimeAvailability` has
  ED-M06 values `Unknown` and `NotMounted`; do not fake `Mounted`.
- Add diagnostic vocabulary tests for any new constants.

Exit:

- Producers compile against one operation/domain/code vocabulary.
- ED-M05 picker rows have an explicit migration target for descriptor state.

### ED-M06.3 - Asset Identity Row And Reducer Model

Goal: make row state a testable transformation instead of view-model logic.

Tasks:

- Add `AssetKind` and `ContentBrowserAssetItem` in the shared Content Browser
  asset-identity namespace established in `ED-M06.2`.
- Implement `IAssetIdentityReducer`.
- Reducer input must accept primitive catalog facts without mutating
  `Oxygen.Assets.AssetRecord`.
- Define merge precedence:
  - `Broken > Missing > Stale > Cooked > Descriptor > Source > Generated`.
- For merged descriptor+cooked rows:
  - `PrimaryState = Descriptor`;
  - `DerivedState = Cooked` or `Stale`;
  - display badge shows the higher-priority state plus descriptor identity.
- For missing references:
  - preserve the authored `asset:///...` URI;
  - do not rewrite the URI to a filesystem path or fallback asset.
- For relocated changes:
  - use `AssetChange.PreviousUri` to invalidate stale cached rows.
- Add reducer tests:
  - generated-only row;
  - source-only row;
  - descriptor-only row;
  - descriptor+cooked merged row;
  - stale cooked companion;
  - missing reference;
  - broken descriptor;
  - relocated previous URI invalidation.

Exit:

- Browser row state is covered by unit tests independent of WinUI controls.

### ED-M06.4 - Broken-State Validation And Content-Root Policy

Goal: make error states visible without doing expensive UI-thread reads.

Tasks:

- Add a descriptor-validation component in Content Browser, not
  `Oxygen.Assets`.
- For V0.1 material descriptors:
  - validate through `MaterialSourceReader`;
  - run validation off the UI thread;
  - cache diagnostics by asset URI + last-write timestamp;
  - invalidate on `AssetChangeKind.Added`, `Updated`, `Removed`, and
    `Relocated`.
- Broken descriptor rows must be shown with:
  - `PrimaryState = Broken`;
  - diagnostic code `OXE.ASSETID.Descriptor.Broken`;
  - detail text that distinguishes parse/read failure from missing file.
- Content-root policy:
  - browser roots come from project authoring mounts;
  - output roots `.cooked`, `.imported`, and `.build` are derived facts, not
    authoring roots;
  - browser reads cooked-root policy through `IProjectCookScopeProvider` /
    `ProjectCookScope.CookedOutputRoot` for display, diagnostics, and reducer
    paths instead of scattering literal `.cooked/<MountName>` calculations.
    Catalog composition (`ProjectAssetCatalog`) may continue computing
    `.cooked/<MountName>` for index discovery where the LLD permits it.
- Add tests for broken descriptor classification and content-root
  normalization.

Exit:

- Broken rows are deterministic, cached, and visible.
- Project root/output root behavior matches `project-services.md`.

### ED-M06.5 - Shared Content Browser Provider

Goal: route browser rows through one provider above existing catalogs.

Tasks:

- Implement `IContentBrowserAssetProvider` over `IProjectAssetCatalog`.
- Keep `IProjectAssetCatalog` as catalog composition; do not replace it in
  ED-M06.
- Provider responsibilities:
  - subscribe to catalog changes;
  - materialize primitive facts;
  - call the reducer;
  - expose snapshot rows;
  - expose snapshot rows through `Items`;
  - expose only `RefreshAsync` and `ResolveAsync` as provider operations;
  - produce `Asset.Query` results from picker/browser filter passes that
    consume snapshots, not from an extra provider `QueryAsync` method;
  - attach diagnostics without publishing filter/search churn as operation
    results.
- Add provider tests for:
  - initial snapshot;
  - refresh after `AssetChangeKind.Added`;
  - removal and missing-row behavior;
  - relocated previous URI behavior;
  - query filters.

Exit:

- Content Browser and picker can share one provider/reducer path.

### ED-M06.6 - Browser Row UI, Filters, Details, And Copy Actions

Goal: make the new identity model visible and usable.

Tasks:

- Replace `GameAsset` row usage in the Content Browser list/tile/detail views
  where ED-M06 rows are rendered.
- If `GameAsset` must remain temporarily for invocation routing, adapt from
  `ContentBrowserAssetItem`; do not let it remain the primary shared contract.
- Migrate `AssetsViewItemInvokedEventArgs` from `GameAsset` to
  `ContentBrowserAssetItem`, or add a clearly named parallel event during the
  transition. Do not leave the invocation API as a hidden `GameAsset`
  dependency after ED-M06 browser rows migrate.
- Add filter UI for:
  - kind;
  - generated;
  - source;
  - descriptor;
  - cooked;
  - stale;
  - missing;
  - broken.
- Search should match display name, asset URI, descriptor path, and GUID where
  available.
- Add compact row badges for `GEN`, `SRC`, `DESC`, `COOK`, `STALE`, `MISS`,
  and `ERR`.
- Add details panel fields:
  - asset URI with copy action;
  - asset GUID/identity with copy action where available;
  - descriptor path and cooked path as diagnostic affordances only;
  - diagnostics for missing/broken rows.
- Add navigation/refresh operation results for failures, but keep normal
  search/filter changes quiet.

Exit:

- A user can see what asset identity they are selecting, whether the descriptor
  is cooked/stale/broken/missing, and copy the stable identity.

### ED-M06.7 - Material Picker Migration

Goal: make the ED-M05 material picker consume the shared ED-M06 browser model.

Tasks:

- Implement `AssetBrowserFilter` projection from `MaterialPickerFilter`:
  - `Kinds = { Material }`;
  - `IncludeSource` maps to `IncludeSource || IncludeDescriptor`;
  - `IncludeCooked` maps to `IncludeCooked || IncludeStale`;
  - existing `IncludeMissing` maps to both `IncludeMissing` and
    `IncludeBroken` in `AssetBrowserFilter`;
  - `MaterialPickerFilter` does not gain a separate `IncludeBroken` field in
    ED-M06 unless the LLD is reopened; missing and broken stay grouped for the
    material picker facade.
- When the picker opens from a missing or broken current assignment, append the
  unresolved current-assignment row after filtering even when missing/broken
  filters are off.
- Migrate `MaterialPickerResult`:
  - `PrimaryState`;
  - `DerivedState`;
  - `RuntimeAvailability`;
  - `DescriptorPath`;
  - `CookedPath`;
  - `BaseColorPreview`.
- Do not add `DisplayPath` or `ThumbnailKey` to `MaterialPickerResult` in
  ED-M06. Reconsider those projection fields in ED-M07 if a concrete consumer
  needs them.
- Keep Geometry material slot persistence unchanged:
  - scene stores only the selected asset URI;
  - no browser state or cooked path is serialized.
- Add picker tests:
  - descriptor material row returns `PrimaryState = Descriptor`;
  - descriptor+cooked row includes `DerivedState = Cooked`;
  - stale row shows stale badge;
  - broken existing assignment remains visible;
  - filter projection maps ED-M05 material filters correctly.

Exit:

- Geometry material assignment still works, but picker rows use shared ED-M06
  state instead of material-only state reduction.

### ED-M06.8 - Validation, Cleanup, And Ledger Update

Goal: close the milestone only after code evidence and manual validation are
both honest.

Tasks:

- Remove or narrow temporary ED-M05 picker-only reducer logic that has been
  replaced by the shared provider.
- Audit persistence:
  - scene material slots store only asset URI;
  - material descriptors do not store browser state;
  - project state does not persist diagnostics.
- Run targeted tests through the approved MSBuild workflow only when build
  validation is requested/allowed.
- Prepare the manual validation script in this plan for user execution.
- Update `IMPLEMENTATION_STATUS.md` with landed facts first, then mark ED-M06
  validated only after the user completes manual validation.

Exit:

- Unit/integration tests for non-UI seams exist and pass when run.
- Manual validation script is ready for user validation.
- Ledger accurately distinguishes landed from validated.

## 7. Project/File Touch Points

Likely source touch points:

- `projects/Oxygen.Editor.ContentBrowser/src/Models/GameAsset.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Panes/Assets/AssetsViewModel.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Panes/Assets/Layouts/AssetsLayoutViewModel.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Panes/Assets/Layouts/ListLayoutViewModel.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Panes/Assets/Layouts/TilesLayoutViewModel.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/ProjectLayoutViewModel.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Infrastructure/Assets/IProjectAssetCatalog.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Infrastructure/Assets/ProjectAssetCatalog.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Materials/IMaterialPickerService.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Materials/MaterialPickerService.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Materials/MaterialPickerFilter.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Materials/MaterialPickerResult.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/Panes/Assets/AssetsViewItemInvokedEventArgs.cs`
- `projects/Oxygen.Editor.ContentBrowser/src/State/ContentBrowserState.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/Geometry/GeometryViewModel.cs`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector/Geometry/AssetPickerItem.cs`
- `projects/Oxygen.Editor.Projects/src/IProjectCookScopeProvider.cs`
- `projects/Oxygen.Editor.Projects/src/ProjectCookScope.cs`
- `projects/Oxygen.Editor.Projects/src/ProjectCookScopeProvider.cs`
- `projects/Oxygen.Core/src/Diagnostics/DiagnosticCodes.cs`
- `projects/Oxygen.Core/src/Diagnostics/MaterialDiagnosticCodes.cs`

Likely test touch points:

- `projects/Oxygen.Editor.ContentBrowser/tests/Oxygen.Editor.ContentBrowser.Tests.csproj`
- `projects/Oxygen.Editor.ContentBrowser/tests/MaterialPickerServiceTests.cs`
- `projects/Oxygen.Editor.ContentBrowser/tests/MaterialFolderWorkflowTests.cs`
- `projects/Oxygen.Editor.ContentBrowser/tests/GameAssetTests.cs`
- new Content Browser tests for reducer/provider/filter behavior.
- `projects/Oxygen.Editor.World/tests/Oxygen.Editor.World.Tests.csproj` for
  `SceneSerializer` material-slot JSON round-trip coverage.
- `projects/Oxygen.Editor.WorldEditor/tests/Oxygen.Editor.WorldEditor.SceneExplorer.Tests.csproj`
  only if the Geometry inspector consumer migration needs a focused test.
- `projects/Oxygen.Editor.Projects/tests/Oxygen.Editor.Projects.Tests.csproj`
- `projects/Oxygen.Core/tests/Oxygen.Core.Tests.csproj`

`Oxygen.Assets` may be read for primitive catalog contracts. ED-M06 must not
add browser UI state to `Oxygen.Assets`.

## 8. Dependency And Migration Risks

| Risk | Mitigation |
| --- | --- |
| `AssetState.Source` changes meaning for material descriptors. | Migrate MaterialPicker, Geometry material slot row, and browser row rendering in the same slice; descriptors become `Descriptor`, raw non-descriptor files remain `Source`. |
| Existing `GameAsset` drives invocation routing. | Use an adapter only as a temporary bridge; `ContentBrowserAssetItem` is the ED-M06 row contract. |
| Broken-state validation becomes slow or UI-thread bound. | Validate descriptors off UI thread and cache by URI + last-write timestamp until `AssetChange`. |
| Cooked-root paths drift from project policy. | Read `ProjectCookScope.CookedOutputRoot` through `IProjectCookScopeProvider` for browser display, diagnostics, and reducer paths; catalog composition may retain LLD-permitted `.cooked/<MountName>` index discovery. |
| Material picker keeps a second state reducer. | Project `MaterialPickerFilter` into `AssetBrowserFilter` and use the shared provider/reducer. |
| Browser rows accidentally become persisted authoring data. | Add serializer round-trip tests and keep browser-only fields out of scene/material/project writers. |
| Users confuse cooked outputs with authoring targets. | UI shows cooked state as derived; create/open workflows remain content-mount oriented. |
| Search/filter churn floods operation results. | Only navigation, refresh, query, resolve, copy, and pick failures publish user-visible operation results. |

## 9. Manual Validation Script

The user validates these scenarios in the running editor:

1. Open the Vortex project and navigate to Content Browser.
2. Open `/Content/Materials`.
3. Confirm material descriptors show as material rows with `DESC` badges.
4. Cook an existing material through the ED-M05 material workflow (Material
   Editor `Cook`, or the existing material context action if present), then
   refresh Content Browser. Confirm the row shows cooked-derived state (`COOK`)
   without changing the authored asset URI.
5. Edit a material descriptor after it has cooked, save it, and refresh.
   Confirm the row indicates stale cooked output.
6. Temporarily break a material descriptor JSON and refresh. Confirm the row is
   visible as broken with diagnostic details and the editor does not crash.
7. Assign a material to a Geometry component through the picker. Confirm the
   picker shows descriptor/cooked/stale/broken state using the same identity
   vocabulary as the browser.
8. Delete or move the assigned descriptor outside the editor, refresh, and
   confirm the Geometry slot preserves the authored URI and shows a missing or
   broken state instead of silently clearing it.
9. Use search and state filters for `Descriptor`, `Cooked`, `Stale`, `Missing`,
   and `Broken`. Confirm normal filter changes do not create noisy operation
   results.
10. Copy the asset URI and GUID/identity from the Content Browser details panel
    and confirm the copied values match the selected row.

Manual validation is the user's app run. The milestone is not validated until
the user confirms these scenarios or records explicit deviations.

## 10. Code/Test Validation Gates

Required non-UI gates:

1. Reducer tests cover generated, source, descriptor, cooked, stale, missing,
   broken, and relocated facts.
2. Broken descriptor validation test proves invalid `*.omat.json` becomes
   `Broken` with `OXE.ASSETID.Descriptor.Broken`.
3. Provider tests prove `IContentBrowserAssetProvider` wraps
   `IProjectAssetCatalog` and refreshes snapshots on `AssetChange`.
4. `AssetChangeKind.Relocated` test proves `PreviousUri` invalidates the old
   row.
5. Material picker projection tests prove `MaterialPickerFilter` maps into
   `AssetBrowserFilter` and returns `PrimaryState`/`DerivedState`.
6. `SceneSerializer` round-trip test in `Oxygen.Editor.World.Tests`
   materializes a Geometry slot with an assigned material and asserts the
   produced scene JSON contains only the asset URI, never `DescriptorPath`,
   `CookedPath`, `PrimaryState`, `DerivedState`, `RuntimeAvailability`,
   `ThumbnailKey`, GUID copy value, or diagnostic fields.
7. Content-root policy tests prove `.cooked`, `.imported`, `.build`, `Scenes`,
   and synthetic project roots are not accepted as material creation targets.
8. Diagnostics vocabulary tests cover any new operation-kind/domain/code
   constants.

Build/test execution must use the approved MSBuild workflow only. If the user
takes ownership of build validation, record that honestly and do not imply the
assistant ran it.

## 11. Status Ledger Hook

Before implementation starts:

- `IMPLEMENTATION_STATUS.md` must show the ED-M06 detailed plan exists.
- ED-M06 LLD acceptance must be recorded only after review feedback is
  remediated and accepted.
- ED-M05 may remain formally pending in the validation ledger while ED-M06
  planning starts, but ED-M06 picker/browser migration must not mask ED-M05
  regressions in material create/open/edit/assign/save/cook behavior.

After implementation lands:

- Mark ED-M06 status `landed`, not `validated`, until manual validation
  completes.
- Check only implementation rows backed by source changes and tests.
- Leave the ED-M06 validation ledger row pending until the user completes
  manual validation.

After user validation:

- Mark ED-M06 status `validated`.
- Record the validation ledger row with the user-confirmed browser, picker,
  missing/broken reference, and persistence behavior.

Closure evidence must mention:

- asset browser row states;
- typed picker result behavior;
- missing/broken reference handling;
- persistence impact;
- build/test boundary;
- manual validation outcome.
