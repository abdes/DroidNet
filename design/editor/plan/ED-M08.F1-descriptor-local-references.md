# ED-M08.F1 — Descriptor-local reference format

Status: **in progress — F1.1 validated; native and managed format cutovers and editor scene-reference authoring implemented in the worktree; F1 qualification open**

**Summary:** replace descriptor-specific packaging rewrites with native reference
tables. Follow [scope](#scope), [delivery](#delivery) and [acceptance](#acceptance).
Execute after [M08.1.9](ED-M08-runtime-parity-and-standalone-validation.md#m081-remaining-increments)
and before M08.2. Existing M08.2–M08.8 identifiers remain unchanged.

| Outcome                                                                                                                                                                                | Remaining                                                                                                                                                                                                                                | Evidence                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Native reference tables, scene-local scripting, generic packaging, managed loose-index consumers, shared current-version definitions and editor scene-reference authoring implemented. | Maintained-project cook/package qualification, origin-first lookup/cache-reuse evidence, load-cost measurement and the final F1 result record. Native-pointer UI interaction qualification is blocked by loss of test-window foreground. | Focused verification: ContentPipeline descriptor tests 40/40; typed-reference discovery and typed-reference dependency-closure cook 2/2; material-texture integration tests 2/2; ContentBrowser tests 151/151; WorldEditor Unit tests 405/405; World tests 77/77; extra-asset UI interaction 1/1; InspectorBindingTests 37/37. Seven native-pointer caption rows could not execute because another process took foreground; no maintained-project or load-cost qualification was run. |

## Remaining work

M08.F1 closes when every item below is done and recorded. Items 1–2 gate the
editor side of the cutover; items 3 and 5 close the format plan's own acceptance.

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
   passes 1/1. The native-pointer numeric-caption rows remain unqualified because
   their test window lost foreground to another process.
   Reference snapshots are now read-only for newly created scenes and scenes
   loaded without a references field, as well as populated references. Regression
   cases for both empty paths were added; they are not executed under the user's
   no-test-rerun instruction. The recorded test counts predate this correction.
   User mounting qualification exposed dropped default-scene metadata in mount
   edits. Browser candidates and publication baselines now retain that setting,
   preserving the full configuration conflict check. Regression coverage was
   extended; those cases have not been executed under the same instruction.
2. **Managed cook/editor qualification.** ContentPipeline Unit and Integration
   test projects build. Focused descriptor tests pass 40/40; typed-reference
   discovery and the editor-to-native dependency-closure cook pass 1/1 each;
   material-texture integration tests pass 2/2; ContentBrowser tests pass 151/151;
   WorldEditor Unit tests pass 405/405; World tests pass 77/77; and
   InspectorBindingTests pass 37/37. The extra-asset UI interaction passes 1/1.
   Seven native-pointer caption rows were blocked because the test window lost
   foreground to another process; do not treat those as verified interaction
   results. Native `RejectsInvalidPhysicsScenePairs` and
   `PatchIncludesBothMembersOfPhysicsScenePair` tests cover pair rejection and
   patch completeness; `RejectsInvalidAssetReferenceGraphs` covers native
   missing/type-mismatched asset dependencies. A focused native-backed scene
   cook now verifies the editor-to-native typed-reference boundary. A broader
   maintained-project cook/package qualification and recorded origin-first
   dependency lookup/cache reuse across changed views remain outstanding.
3. **Load cost — decision/evidence required.** No workload or numeric budget is
   specified, so do not invent a pass/fail threshold. For the handoff, choose:
   (a) measure descriptor metadata bytes and scene-load/cook timings on maintained
   projects and report results without a threshold; (b) set an explicit budget
   and qualify against it; or (c) defer this gate and keep F1 in progress. Any
   measurement must compare added metadata/load work with removed relocation and
   inspection work, with shader evaluation and rendering hot paths unchanged.
4. **Ordered-layer acceptance evidence — covered by existing native tests.**
   [`DeterministicPlanningPreservesDeclaredLayerPriority`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Test/Pak/PakPlanBuilder_test.cpp)
   verifies that reversing conflicting inputs changes the winner.
   [`IncrementalPatchRevertsReplacementAndPreservesDeletion`](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Test/Pak/PakPlanBuilder_test.cpp)
   verifies that flattening the ordered layers preserves effective content,
   including a replacement and deletion.
5. **Record the M08.F1 result.** Add the `ED-M08.F1` validation row with the exact
   native build/test counts, the recook set, the SDK and Interop state, and the
   managed suite results. Update it only after the outstanding qualification
   gates above have evidence; current managed test results are recorded here.

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
rather than source-cook jobs. The remaining editor-side work is qualification:
focused inspector tests and a native-backed script-reference cook pass; the
broader inspector suite has two legacy environment-test failures, and interactive
editor validation remains unperformed.

Wire details belong in the existing Data packing and Cooker PAK designs before
implementation; do not duplicate their field layouts in this plan.

## Delivery

| Slice                  | State       | Work                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              |
| ---------------------- | ----------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F1.1 Contract          | validated   | Owner contracts cover current asset types, scripts, dependency kinds, absent/fallback/error texture semantics, bounds and versions. Preserve successful model import with diagnostics for missing textures.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                       |
| F1.2 Producers/readers | in_progress | Native emitters/readers and Inspector migrated; shared version catalog added. PakGen and its dependencies are removed; external tests use native owning boundaries. Exact reference-inventory checks and per-descriptor publication gates are implemented; frozen physics bindings and shared scene-load scopes are implemented. Binding resolution stays on loading paths. Remaining: record closure evidence for origin-first dependency lookup and cache reuse across changed views, which no current test or record names, and run the pre-commit checks on the changed files.                                                                                                                                                                                                |
| F1.3 Packaging         | in_progress | Generic binding relocation implemented; descriptor rewrites and global script tables removed. Ordered composition, embedded catalogs, tombstones, exact adjacent baselines and patch-of-patch input are implemented with native coverage (`PakPlanBuilderTest.DeterministicPlanningPreservesDeclaredLayerPriority`, `PakPlanBuilderTest.IncrementalPatchRevertsReplacementAndPreservesDeletion`, `PatchResolutionPolicyTest.*`). The ordered-layer acceptance tests cited in [remaining work](#remaining-work) item 4 cover the required cases; final F1 qualification and changed-file checks remain.                                                                                                                                                                            |
| F1.4 Cutover           | in_progress | Native SDKs, maintained scenes, shared PAK and four retained model imports refreshed. RenderScene retained-library loading passes. Managed loose-index v3 readers retain reference-block metadata opaquely; editor dependency reports preserve typed native references, validate expected target types, and use the v2 wire/cache contract while only Asset targets enter dependency closure. Project key discovery and editor scene-reference authoring/emission now cover the current reference-target descriptor types. Focused managed tests pass as recorded in [remaining work](#remaining-work); UI test compilation, two integration failures, end-to-end cook/editor qualification, interactive validation, load-cost measurement and the final F1 result record remain. |

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

F1.2 still needs recorded evidence for origin-first dependency lookup and cache
reuse across changed views. F1.3 must preserve declared source order, compose
base catalogs with replacement/deletion semantics and reject incompatible types;
the cited native tests cover the ordered-priority, replacement, and deletion
acceptance cases. These changes remain in progress pending F1 qualification.

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
- Existing material, camera, slot and IBL behavior passes matched regression cases.
- Measure added metadata/load cost against removed relocation/inspection work;
  keep shader evaluation and rendering hot paths unchanged.
- Current formats only; no legacy reader or shipping conversion branch.

Retain concise results and reproduction commands. Migration backups and generated
debugging artifacts stay outside Git.
