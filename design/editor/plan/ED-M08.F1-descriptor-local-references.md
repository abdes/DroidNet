# ED-M08.F1 — Descriptor-local reference format

Status: **planned**

**Summary:** replace descriptor-specific packaging rewrites with native reference
tables. Follow [scope](#scope), [delivery](#delivery) and [acceptance](#acceptance).
Execute after [M08.1.9](ED-M08-runtime-parity-and-standalone-validation.md#m081-remaining-increments)
and before M08.2. Existing M08.2–M08.8 identifiers remain unchanged.

| Outcome                                                                    | Remaining                                              | Evidence                              |
| -------------------------------------------------------------------------- | ------------------------------------------------------ | ------------------------------------- |
| Packaging relocates generic bindings while descriptor bytes remain stable. | Wire design, implementation, recook and qualification. | None; implementation has not started. |

## Scope

Data owns descriptor-local resource-reference indices and validated binding
metadata. Cooker owns emitted bindings, direct asset dependency metadata and
generic resource placement. Content resolves those bindings through M08.1.5
identities. Typed material/geometry/scene decoding remains in its current owner.

The [Data reference contract](../../../projects/Oxygen.Engine/src/Oxygen/Data/Docs/binary_packing_discipline.md#planned-descriptor-local-references-m08f1)
owns local references and scripting layout; the
[Cooker packaging contract](../../../projects/Oxygen.Engine/src/Oxygen/Cooker/Docs/Pak/paktool_design.md#planned-reference-table-packaging-m08f1)
owns binding relocation.

This removes `RewriteResourceReferences` field/asset-type dispatch,
`RewriteSceneScriptingComponentRanges`, `RewriteSceneScriptBindings`, and the
container-global script binding/parameter coordination. Resource aggregation and
placement remain necessary. No reflection framework, universal graph or general
content-addressed database is included.

Wire details belong in the existing Data packing and Cooker PAK designs before
implementation; do not duplicate their field layouts in this plan.

## Delivery

| Slice                  | State   | Work                                                                                                                                                                                            |
| ---------------------- | ------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| F1.1 Contract          | planned | Specify reference kinds, absent/fallback semantics, directory/table bounds, dependency completeness and current-only versions. Review every reference-bearing asset type and scripting payload. |
| F1.2 Producers/readers | planned | Update Data serializers, native emitters, loaders, Inspector and PakGen together. Resolve bindings once during loading; no per-frame indirection.                                               |
| F1.3 Packaging         | planned | Remap generic binding tables; delete type-specific descriptor rewrites and global scripting tables. Preserve descriptor hashes and physics-sidecar scene identity.                              |
| F1.4 Cutover           | planned | Upgrade maintained sources/projects as needed, recook demos and PAKs, refresh SDK/Interop, reject previous formats and qualify normal engine/editor loading.                                    |

## Acceptance

- Repacking leaves opaque descriptor bytes unchanged while referenced resources
  resolve identically in loose and PAK sources, including multi-root packaging.
- Adding a resource-bearing descriptor field needs no packaging offset walker.
- Missing, wrong-kind, out-of-range, truncated and cyclic hard references reject
  before publication; reference/dependency inventories are complete.
- Scene scripting, metering masks and physics-sidecar target hashes remain valid.
- Existing material, camera, slot and IBL behavior passes matched regression cases.
- Measure added metadata/load cost against removed relocation/inspection work;
  keep shader evaluation and rendering hot paths unchanged.
- Current formats only; no legacy reader or shipping conversion branch.

Retain concise results and reproduction commands. Migration backups and generated
debugging artifacts stay outside Git.
