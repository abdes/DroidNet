# ED-M08.F1 — Descriptor-local reference format

Status: **in progress — native and managed loose-index cutovers implemented; qualification and rollout remain**

**Summary:** replace descriptor-specific packaging rewrites with native reference
tables. Follow [scope](#scope), [delivery](#delivery) and [acceptance](#acceptance).
Execute after [M08.1.9](ED-M08-runtime-parity-and-standalone-validation.md#m081-remaining-increments)
and before M08.2. Existing M08.2–M08.8 identifiers remain unchanged.

| Outcome                                                                                                                                              | Remaining                                                                                                             | Evidence                                                                                                                                                                                                                        |
| ---------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Native reference tables, scene-local scripting, generic packaging, managed loose-index consumers and shared current-version definitions implemented. | Complete editor scene-reference authoring, warning cleanup, load-cost qualification and editor project qualification. | Release install and 28 native test programs pass; 16 maintained scenes and four retained models recooked; RenderScene retained-library check passes. Managed changes are pending user-owned build, tests and editor validation. |

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

### Editor reference authoring gap

Native scene descriptor v10 accepts scene-level `scripts`, `input_actions`,
`input_mapping_contexts`, `physics_sidecars`, and `extra_assets` references.
The managed editor currently emits only `materials`; its `SceneData` model has no
serialized collections or authoring workflow for the other reference kinds.
`ProjectAssetKeyIndex` now discovers project descriptors for scripts, input
actions, mapping contexts, and physics scenes, but this does not make them
authored or emitted by a scene.

Complete this editor portion of F1.4 in a follow-up: define backward-compatible
serialized scene reference data; add assignment, validation, and save/reload
behavior in the editor; emit all supported native v10 reference arrays; resolve
project/library ownership and expected asset types; and add descriptor,
round-trip, and cook-closure coverage. Until then, scene-local scripting and
these reference kinds are native-format capabilities, not editor capabilities.

Wire details belong in the existing Data packing and Cooker PAK designs before
implementation; do not duplicate their field layouts in this plan.

## Delivery

| Slice                  | State       | Work                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| ---------------------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| F1.1 Contract          | validated   | Owner contracts cover current asset types, scripts, dependency kinds, absent/fallback/error texture semantics, bounds and versions. Preserve successful model import with diagnostics for missing textures.                                                                                                                                                                                                                                                                                                                                                                                            |
| F1.2 Producers/readers | in_progress | Native emitters/readers and Inspector migrated; shared version catalog added. PakGen and its dependencies are removed; external tests use native owning boundaries. Exact reference-inventory checks and per-descriptor publication gates are implemented; frozen physics bindings and shared scene-load scopes are implemented. Complete qualification and pre-commit checks. Binding resolution stays on loading paths.                                                                                                                                                                              |
| F1.3 Packaging         | in_progress | Generic binding relocation implemented; descriptor rewrites and global script tables removed. Ordered composition, embedded catalogs, tombstones, exact adjacent baselines and patch-of-patch input are implemented. Complete integration qualification and pre-commit checks.                                                                                                                                                                                                                                                                                                                         |
| F1.4 Cutover           | in_progress | Native SDKs, maintained scenes, shared PAK and four retained model imports refreshed. RenderScene retained-library loading passes. Managed loose-index v3 readers retain reference-block metadata opaquely; editor dependency reports preserve typed native references, validate expected target types, and use the v2 wire/cache contract while only Asset targets enter dependency closure. Project key discovery covers current reference-target descriptor types. Editor scene-level authoring/emission for non-material references, project recook/Interop checks and final qualification remain. |

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

F1.2 must close origin-first dependency lookup and cache reuse across changed
views. F1.3 must preserve declared source order, compose base catalogs with
replacement/deletion semantics and reject incompatible types. These changes are
in progress; approval is not validation.

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
