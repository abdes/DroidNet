# ED-M08.F1 — Descriptor-local reference format

Status: **validated — closed on 2026-10-06; format cutover, editor reference authoring, PAK delivery and origin/cache evidence complete within the approved closure scope**

**Summary:** replace descriptor-specific packaging rewrites with native reference
tables. Follow [scope](#scope), [delivery](#delivery) and [acceptance](#acceptance).
Execute after [M08.1.9](ED-M08-runtime-parity-and-standalone-validation.md#m081-remaining-increments)
and before M08.2. Existing M08.2–M08.8 identifiers remain unchanged.

| Outcome                                                                                                                                                                       | Remaining                                  | Evidence                                                                                                                                                                                                                                                                                                                                         |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Native reference tables, scene-local scripting, generic packaging, managed consumers and scene-reference authoring delivered. User editor checks and Main PAK rendering pass. | None within the approved F1 closure scope. | Main and Examples packaged: 158 assets, 65 resources, zero warnings/errors; Main loaded from the PAK alone in RenderScene and user-confirmed. Existing focused origin/binding/cache tests pass 5/5. Prior managed/native evidence is retained below. Final managed/UI qualification and load-cost measurement are non-blocking by user decision. |

## Remaining work

By user decision on 2026-10-06, final managed/UI qualification and load-cost
measurement do not block F1 closure. Existing failures and unrun checks remain
recorded; removing a gate does not turn them into passes.
The remaining cook/package and origin/cache gates now pass, as recorded in
[closure verification](#closure-verification). There is no remaining F1 blocker.
This document and the implementation ledger are the final closure record.

1. **Editor scene-reference authoring (the editor portion of F1.4) — implemented
   and focused-tested.** Scene files now have backward-compatible serialized
   reference collections, and descriptor generation emits the native v10
   `materials`, `scripts`, `input_actions`, `input_mapping_contexts`,
   `physics_sidecars` and `extra_assets` arrays. Typed references are authored as
   already-cooked project/library outputs; F1 does not infer or schedule source
   recipes. Physics-scene/sidecar pairing remains native package-planner
   validation. Focused descriptor, legacy-compatibility, extension-validation,
   cooked-reference discovery, catalog, serialization, inspector scope/binding,
   and an editor-to-native cooked-library dependency-closure test pass. The
   InspectorBindingTests passes 37/37, and the extra-asset add/remove interaction
   passes 1/1. The native-pointer numeric-caption rows remain unqualified: five rows lost
   test-window foreground and two Environment rows failed caption hit-testing.
   Reference snapshots are now read-only for newly created scenes and scenes
   loaded without a references field, as well as populated references. Regression
   cases for both empty paths were added; they are not executed under the user's
   no-test-rerun instruction. The recorded test counts predate this correction.
   User mounting qualification exposed dropped default-scene metadata in mount
   edits. Browser candidates and publication baselines now retain that setting,
   preserving the full configuration conflict check. Regression coverage was
   extended; the browser preservation cases now pass 2/2. The WorldEditor mount
   transaction cases have not been executed.
   The user confirms successful mounting, correctly typed reference addition,
   dirty state, removal, Undo/Redo, Extra Assets, unavailable assets after losing
   a mount and resolution after remounting. Save/reopen of authored references
   is not inferred from these checks. Cook/package qualification is separately
   recorded below.
   Live mount-tree presentation exposed a disposed singleton explorer being
   reused after outlet replacement. Explorer views and models are now transient
   and outlet-owned; shell Refresh targets the displayed explorer. Focused browser
   regressions pass 7/7, including rendered local-folder mounting before/after
   outlet replacement and refresh discovering a new folder without restart.
   The user confirmed the correction in the editor on 2026-10-06; the live
   mount-tree item is validated and closed.
2. **Cook/package and dependency/cache qualification.** Final managed/UI
   qualification is non-blocking by user decision. ContentPipeline Unit and Integration
   test projects build. Focused descriptor tests pass 40/40; typed-reference
   discovery and the editor-to-native dependency-closure cook pass 1/1 each;
   material-texture integration tests pass 2/2; ContentBrowser tests pass 151/151;
   WorldEditor Unit tests pass 405/405; World tests pass 77/77; and
   InspectorBindingTests pass 37/37. The extra-asset UI interaction passes 1/1.
   Seven native-pointer caption rows remain unqualified: five foreground failures
   and two Environment caption hit-testing failures; do not treat those as
   verified interaction results. Native `RejectsInvalidPhysicsScenePairs` and
   `PatchIncludesBothMembersOfPhysicsScenePair` tests cover pair rejection and
   patch completeness; `RejectsInvalidAssetReferenceGraphs` covers native
   missing/type-mismatched asset dependencies. A focused native-backed scene
   cook verifies the editor-to-native typed-reference boundary. The user's saved
   Main scene with Lantern geometry and Examples script/input references was
   packaged and loaded from the PAK in RenderScene. Existing procedural-cube,
   scalar-material, texture-binding and identity tests establish the origin/cache
   gate without a new Lantern import or new test implementation.
3. **Load cost — non-blocking.** Removed from F1 closure criteria by user decision
   on 2026-10-06. No measurement or numeric budget is claimed. Shader evaluation
   and rendering hot paths must still retain their intended behavior.
4. **Ordered-layer acceptance evidence — covered by existing native tests.**
   [`DeterministicPlanningPreservesDeclaredLayerPriority`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Test/Pak/PakPlanBuilder_test.cpp)
   verifies that reversing conflicting inputs changes the winner.
   [`IncrementalPatchRevertsReplacementAndPreservesDeletion`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Test/Pak/PakPlanBuilder_test.cpp)
   verifies that flattening the ordered layers preserves effective content,
   including a replacement and deletion.
5. **Closure record — complete.** The approved scope decisions, prior test counts,
   cutover state and final qualification evidence are recorded here and in
   [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).

## Closure verification

### Saved Main scene to PAK to RenderScene

On 2026-10-06, packaged the user's existing `F1 Validation` project's saved Main
scene; no authoring data was changed and no import or recook was repeated.
The active publication was `3103a36b-936d-465e-be89-7e57fa84c5ed`, selecting project
generation `01a110fc92667a08bc3429af1b8d0821` and the mounted Examples main library.
Main's authored JSON SHA-256 was
`8c433d5bfca6dfe98b9a5a6b0524deced33ec6fc7861c28f22b65ff1df7077f3`.

The existing installed Release SDK PakTool packaged both roots, with external
script sealing against `Examples/Content`. Result:
158 assets, 65 resources, zero warnings/errors. This deliberately packages the
whole Examples library, not a minimal stripped distribution. PakDump reads Main
as scene v10, key `0c81ea40-a71f-3b14-0c57-8882d9be88c9`, with three Lantern
renderables and the authored environment.

RenderScene mounted only `f1-main.pak`, verified its CRC, loaded that exact Main
key, staged the scene successfully and published it at frame start. No loose
project or Examples root was mounted. The user confirmed seeing the Lantern
scene. This is manual rendered acceptance, not an automated UI-suite pass.
Two earlier UI-filter attempts selected zero tests and are not passing evidence.
PAK SHA-256:
`940322e2480a5783a6226785e7264f5501304b76903b1fa5cc0c143f556e8751`.

Reproduction from `projects/Oxygen.Engine` in PowerShell:

```powershell
$project = Join-Path $env:LOCALAPPDATA 'DroidNet\Oxygen Editor\Oxygen Projects\F1 Validation'
$generation = Join-Path $project '.cooked\generations\01a110fc92667a08bc3429af1b8d0821'
$out = Join-Path (Get-Location) 'out\m08-f1-user-main'
& .\out\install\Release\bin\Oxygen.Cooker.PakTool.exe build `
  --loose-source .\Examples\Content\.cooked\main --loose-source $generation `
  --script-source-root .\Examples\Content `
  --out "$out\f1-main.pak" --catalog-out "$out\f1-main.catalog.json" `
  --manifest-out "$out\f1-main.manifest.json" --diagnostics-file "$out\f1-main.report.json" `
  --content-version 1 --source-key 01a110fc-9266-7a08-bc34-29af1b8d0821 --embed-browse-index
& .\out\install\Release\bin\Oxygen.Examples.RenderScene.exe `
  --fps 60 --resolution 1280x800 --verify-hashes=true --hot-reload=false `
  --environment-profile scene --cvars-archive "$out\interactive-cvars.json" --debug-layer=true
```

For the RenderScene launch, its SDK-local `share/oxygen/RenderScene/demo_settings.json`
records only this PAK under `content.library.mounted_paks`, empty mounted indices
and import records, and this Main key/source under `content.active_scene`.
Do not use `--scene`: it also mounts the shared loose library.
Local PAK, reports and logs are retained under `out/m08-f1-user-main`; the runtime
settings remain SDK-local. None are committed. Main's script/input references are
packaged dependencies; it has no attached script component, so this does not
claim script execution.

### Procedural origin/binding/cache evidence

Ran five existing Release cases; all pass. No new tests were written.

| Existing native case                                                       | Verified behavior                                                                                                                                                                                            |
| -------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `AssetLoaderBasicTest.ExternalDependencyBindingsSurviveRootReplacement`    | A procedural-cube scene with a scalar material retains its old bound material after root replacement. A new scene uses the replacement; loading through the original scope reuses the original scene object. |
| `AssetLoaderBasicTest.NewestMountWinsWithoutChangingQualifiedPublications` | Red/blue scalar materials obey mount order; explicitly source-qualified loads select the correct version and retained data does not change.                                                                  |
| `AssetLoaderBasicTest.RetiredGenerationsRetainTheirOwnAssetsAndPayloads`   | Old/new materials and script payloads remain distinct; unrelated cached material is reused, retained resources reload correctly after eviction, and reference-free scene identity is reused.                 |
| `AssetLoaderBasicTest.SharedTextureBindingIsPinnedAndEvictedExactlyOnce`   | Repeated material loads share the decoded object; shared base-color/normal texture identity remains pinned and is evicted exactly once.                                                                      |
| `ContentIdentityRegistry.BindingViewsSeparateGraphsAndRetainPhysicalKeys`  | Different mounted-content binding views have distinct asset identities while the same unchanged physical resource keeps its identity.                                                                        |

Reproduction with existing binaries:

```powershell
& .\out\build-ninja\bin\Release\Oxygen.Content.AssetLoader.Tests.exe `
  '--gtest_filter=AssetLoaderBasicTest.ExternalDependencyBindingsSurviveRootReplacement:AssetLoaderBasicTest.NewestMountWinsWithoutChangingQualifiedPublications:AssetLoaderBasicTest.RetiredGenerationsRetainTheirOwnAssetsAndPayloads:AssetLoaderBasicTest.SharedTextureBindingIsPinnedAndEvictedExactlyOnce'
& .\out\build-ninja\bin\Release\Oxygen.Content.ContentIdentity.Tests.exe `
  '--gtest_filter=ContentIdentityRegistry.BindingViewsSeparateGraphsAndRetainPhysicalKeys'
```

The existing Release SDK and native binaries were used without rebuilding the
engine. Prior cutover evidence remains: Release install and 28 native test
programs pass; 16 maintained scenes and four retained models were recooked;
managed/Interop owning projects compiled against the installed SDK. The prior
managed counts above retain their original scope and are not a claim of a new
full-suite run. Final managed/UI qualification and load-cost measurement remain
non-blocking under the user's 2026-10-06 decision.

## Scope

Data owns descriptor-local resource-reference indices and validated binding
metadata. Cooker owns emitted bindings, direct asset dependency metadata and
generic resource placement. Content resolves those bindings through M08.1.5
identities. Typed material/geometry/scene decoding remains in its current owner.

The [Data reference contract](../../../projects/Oxygen.Engine/src/Oxygen/Data/Docs/binary_packing_discipline.md#descriptor-local-references-m08f1)
owns local references and scripting layout; the
[Cooker packaging contract](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Docs/Pak/paktool_design.md#reference-table-packaging-m08f1)
owns binding relocation.

This removes `RewriteResourceReferences` field/asset-type dispatch,
`RewriteSceneScriptingComponentRanges`, `RewriteSceneScriptBindings`, and the
container-global script binding/parameter coordination. Resource aggregation and
placement remain necessary. No reflection framework, universal graph or general
content-addressed database is included.

### Editor reference authoring

Scene documents now serialize typed script, input-action, input-mapping-context,
physics-sidecar, and extra-asset references. The inspector exposes typed catalog
pickers, removal and extra-path controls; edits are undoable and validated before
the scene is marked dirty. Descriptor generation emits the native v10 reference
arrays, and cook dependency discovery retains those references as cooked outputs
rather than source-cook jobs. Focused inspector tests and a native-backed
script-reference cook pass. User reference-authoring and live mount-tree
validation are recorded above. Final managed/UI qualification is non-blocking;
the remaining cook/package and origin/cache checks retain their own evidence.

Wire details belong in the existing Data packing and Cooker PAK designs before
implementation; do not duplicate their field layouts in this plan.

## Delivery

| Slice                  | State     | Work                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            |
| ---------------------- | --------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F1.1 Contract          | validated | Owner contracts cover current asset types, scripts, dependency kinds, absent/fallback/error texture semantics, bounds and versions. Preserve successful model import with diagnostics for missing textures.                                                                                                                                                                                                                                                                                     |
| F1.2 Producers/readers | validated | Native emitters/readers and Inspector migrated; shared version catalog added. PakGen and dependencies removed. Reference inventories, per-descriptor publication gates, frozen physics bindings and shared scene-load scopes implemented. Binding resolution stays on loading paths. Existing focused origin/binding/cache cases pass 5/5; see [closure verification](#closure-verification).                                                                                                   |
| F1.3 Packaging         | validated | Generic relocation replaces descriptor rewrites/global script tables. Ordered composition, embedded catalogs, tombstones, exact baselines and patch-of-patch input retain native coverage. Ordered-layer tests cover priority/replacement/deletion; the user's Main scene and both content roots package successfully and render from the PAK alone.                                                                                                                                            |
| F1.4 Cutover           | validated | Native SDKs, maintained scenes, shared PAK and four retained model imports refreshed. Managed loose-index v3 readers preserve reference metadata; editor dependency reports retain typed native references and the v2 wire/cache contract. Asset targets alone enter closure. Editor scene-reference authoring/emission and user workflow checks pass. Main PAK delivery is user-confirmed; final managed/UI qualification and load-cost measurement are non-blocking under the approved scope. |

Implementation boundaries:

- **F1.2:** Data packed records/Serio codecs and immutable reference metadata;
  Cooker asset emitters, scene/script producers and loose-index registry;
  Content source readers, typed loaders and existing runtime binding owners;
  Inspector and focused native tests. Content tests remain independent of Cooker
  and external processes; native Cooker tests own package-build integration. Keep
  local reference indices separate from container indices. Use Base named types and module APIs.
- **F1.3:** Cooker PAK planning/writing/inspection and full/patch packaging tests.
  Remove superseded walkers and global script-table machinery in the same slice.
- **F1.4:** Examples/Content recipes, retained RenderScene imports, native SDK and
  Interop consumers, editor ContentPipeline integration and maintained projects.
  Validate native loose/PAK loading before editor workflows. Run focused changed
  tests; full managed-suite execution remains user-owned.

Keep commits buildable and scoped: owner designs accompany their code; engine and
editor changes remain separate. Finish each slice with a simplification review,
format/lint checks and focused validation before requesting commit approval.

## Ordered layers — approved

**Option A:** ordinary composition and patches share explicit ordered layers.
Later layers replace compatible definitions of the same AssetKey; deletions mask
lower layers. Filesystem ordering never chooses winners. Physical indices remain
local to the selected descriptor's container. Existing objects retain their exact
bindings; new or reloaded graphs use one coherent current layer view.

The [Content lifetime contract](../../../projects/Oxygen.Engine/src/Oxygen/Content/Docs/loose_cooked_content.md#published-generations)
owns runtime binding and cache behavior. The
[Cooker layer contract](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Docs/Pak/paktool_design.md#ordered-layers-and-patch-baselines)
owns composition and incremental/cumulative patch baselines.

F1.2 origin/binding/cache evidence is recorded in
[closure verification](#procedural-originbindingcache-evidence).
F1.3 preserves declared source order, composes base catalogs with
replacement/deletion semantics and rejects incompatible types; the cited native
tests cover ordered priority, replacement and deletion.

## Acceptance

- Base scene + patched material: new/reloaded scene uses the patch; the held old
  scene retains its original material. Incremental P2 can revert P1 to base bytes,
  preserve P1 deletions or explicitly recreate a deleted asset. Flattening the same
  ordered layers preserves effective content. Reversed conflicting inputs change
  the winner; repeated identical ordered inputs remain deterministic.
- Repacking leaves opaque descriptor bytes unchanged while referenced resources
  resolve identically in loose and PAK sources, including multi-root packaging.
- Managed editor catalog and cook readers accept only loose-index v3 and retain
  reference-block locators/counts without interpreting their payloads.
- Editor dependency closure includes only native key references classified as
  `Asset`; `PhysicsResource`, `Logical` and resource bindings are not asset edges.
- The editor's authored scene reference lists round-trip and emit the native v10
  material, script, input-action, input-mapping-context, physics-sidecar, and
  extra-asset categories with project/library resolution and expected-type checks.
- Adding a resource-bearing descriptor field needs no packaging offset walker.
- Missing, wrong-kind, out-of-range, truncated and cyclic hard references reject
  before publication; reference/dependency inventories are complete.
- Missing source textures preserve usable model imports, existing error-texture
  rendering and diagnostics in loose and PAK content. Successful repair/reimport
  restores real textures. Explicit fallback and ordinary unassigned slots retain
  their separate semantics.
- Scene scripting, metering masks and physics-sidecar target hashes remain valid.
- Preserve existing material, camera, slot and IBL behavior. Final managed/UI
  qualification is not a closure gate by user decision on 2026-10-06.
- Load-cost measurement is not a closure gate by the same decision; keep shader
  evaluation and rendering hot paths unchanged.
- Current formats only; no legacy reader or shipping conversion branch.

Retain concise results and reproduction commands. Migration backups and generated
debugging artifacts stay outside Git.
