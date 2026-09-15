# Material Editor LLD

Status: `Canonical V0.1 contract; implementation and qualification tracked by ED-M08`

## 1. Purpose

Provide a focused scalar PBR material editor with asset identity, commands,
Undo/Redo, atomic persistence, cooking, per-instance assignment and useful
preview. The material swatch follows current edits immediately. Scene rendering
uses the last successfully published material until a saved revision is cooked
and published.

Successful explicit Save/import and missing/stale dependency demand schedule
incremental work through the shared project coordinator. Session pause and
explicit Cook remain available. Browsing and transient edits do not cook;
cooking never saves documents implicitly. See
[content-cooking-workflows.md](content-cooking-workflows.md).

## Save Revision Contract

The material document owns authoring and saved revisions. Validate and commit
edits under its authoring lock. Serialize saves per document and writes per
destination; a queued save captures its coherent snapshot after acquiring the
save gate. Disk I/O does not hold the UI edit gate.

After successful atomic replacement, acknowledge the captured saved revision.
Do not replace newer authoring state or clear its dirty flag. Report “Saved;
newer changes remain unsaved” when applicable. A failed write retains the prior
saved revision and current edits. Close/dispose drains pending persistence;
unsaved revisions require the normal explicit Save/discard flow.

## 2. PRD Traceability

| IDs | Required result |
| --- | --- |
| GOAL-004/005/006 | Material authoring, published state and failures are usable and visible. |
| REQ-010/011/012/013/014 | Create/open/edit/save/cook/assign supported material values by asset identity. |
| REQ-021/022/037 | Shared catalog state, actionable failures and source round trips. |
| SUCCESS-002/004/007 | Saved material integrity, independent native verification and complete editor workflows. |

## 3. Architecture Links

- [property-pipeline.md](property-pipeline.md): property identity, validation and gestures.
- [documents-and-commands.md](documents-and-commands.md): document lifetime, history and saving.
- [property-inspector.md](property-inspector.md): geometry slots and scene commands.
- [content-pipeline.md](content-pipeline.md): native cooking, slot identity and publication.
- [live-engine-sync.md](live-engine-sync.md): current runtime intent and async completion ownership.
- [content-browser-asset-identity.md](content-browser-asset-identity.md): browser/picker identity.

## 4. Integration Baseline

`MaterialDocumentService`, immutable `MaterialSource`, the property-descriptor
catalog, source reader/writer, CPU swatch and material picker are existing
integration points. `MaterialCookService` delegates to `IContentPipelineService`;
it is not an independent managed binary writer. The native cooker owns emitted
material descriptors and their format.

The implementation must extend these paths for emission and identity-based
assignment to all existing geometry slots. Current source models lack emission,
and the native packed material currently stores emissive RGB as binary16. These
are implementation deltas, not supported substitutes for the contract below.

## 5. Target Design

```text
Create/open material -> edit immutable source through commands -> explicit Save
  -> shared incremental cook -> validated publication -> refresh scene instances
Assign/clear material -> scene command for a stable geometry slot -> live sync
```

V0.1 supports base colour/opacity, metallic, roughness, Opaque/Mask/Blend,
double-sided rendering and scalar emission. Existing canonical texture references
remain read-only. Normal scale and occlusion strength are editable only when the
corresponding input exists and its active shader path has an effect.

Texture authoring, material graphs, custom shaders, additional BRDF lobes and
emissive global illumination are outside this material surface. Emission adds
self-illumination to ordinary PBR; it does not select a new Unlit mode or promise
light on nearby geometry.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| MaterialEditor | Material documents, property commands, history and swatch UI. |
| Managed.Assets | Typed immutable material data and canonical source reader/writer. |
| ContentBrowser | Create/open entry points, asset identity and material picker. |
| ContentPipeline | Saved-input capture, native cook, provenance and publication. |
| WorldEditor | Per-instance geometry-slot assignments and explicit repair commands. |
| Native Data/Cooker/Content/Vortex | Slot inventory, binary formats, loaded materials and actual rendering. |
| Runtime / Interop | Supported capability transport and lifetime-safe native application. |

No editor feature writes native material/geometry structs or owns another cook
path. Development qualification depends on these production capabilities; the
production modules do not depend on qualification code or schemas.

## 7. Data Contracts

### 7.1 Canonical schema and migration

Use the engine-owned material descriptor schema for canonical `*.omat.json`
authoring. The typed `MaterialSource` is an immutable managed projection of that
schema, not a second wire format. Remove the former glTF-shaped
`oxygen.material.v1` authoring route after one-time migration of useful content.
The reader must not silently accept a retired format.

Keep colour and intensity separately in canonical source; store only their
compiled RGB product in the native runtime factor. Do not serialize both an
editable factor and editable colour/intensity with competing authority.

Migration converts useful old descriptors once, including read-only texture
references. A prior factor-only emissive RGB can be decomposed as intensity =
max(R,G,B), colour = RGB/intensity; zero uses colour white and intensity zero.
Materials without emission use that zero-intensity default. Migration preserves
representable appearance, reports invalid/out-of-range input, and recooks affected
content. No old-schema reader, deprecated alias or runtime compatibility branch
ships alongside the canonical contract.

### 7.2 Document model and identity

The document contains its material URI, current immutable `MaterialSource`,
authoring/saved revisions, document-owned history and shared published cook state.
An asset wrapper supplies identity; any attached source snapshot is not the
editable authority. Every edit/copy/history path preserves every canonical field.

The asset name is the source file stem: `Content/Materials/Gold.omat.json` is
shown and saved as `Gold`. URI and derived editor GUID are read-only identity
information. Do not introduce an independently editable descriptor name. Asset
file rename/reference repair remains the Content Browser's responsibility.

### 7.3 Field table and numeric contract

Paths below belong to the canonical engine-owned authoring schema. Internal
managed member names may differ; one tested adapter owns the mapping.

| UI field | Canonical source path | Validation and behavior |
| --- | --- | --- |
| Base colour / opacity | `parameters.base_color[0..3]` | Finite linear RGB and alpha in [0,1]. Colour picker converts display sRGB at its boundary; alpha is linear coverage. |
| Metallic | `parameters.metalness` | Finite [0,1]. |
| Roughness | `parameters.roughness` | Finite [0,1]. |
| Surface mode | `alpha_mode` | Opaque, Mask or Blend; explicit enum mapping. |
| Alpha cutoff | `parameters.alpha_cutoff` | Finite [0,1]; shown only for Mask. |
| Double Sided | `parameters.double_sided` | Boolean; normal PBR and alpha-mode rules remain active. |
| Emission colour | `parameters.emissive_color[0..2]` | Finite linear RGB in [0,1]; retained when intensity is zero. |
| Emission intensity | `parameters.emissive_intensity` | Finite float32 relative multiplier in [0,65504]; default 0. No physical luminance unit is asserted. |
| Normal scale | `parameters.normal_scale` | Finite >=0, only when the preserved normal input exists and is effective. |
| Occlusion strength | `parameters.ambient_occlusion` | Finite [0,1], only when the preserved occlusion input exists and is effective. |
| Texture identities | Canonical texture-reference fields | Read-only; preserve on all unrelated edits, Save and migration. |
| Name, URI, GUID, schema | Identity/schema metadata | Read-only with appropriate copy affordances. |

New materials retain the existing editor creation defaults: white base colour
with opacity 1, metallic 0, roughness 0.5, Opaque, alpha cutoff 0.5, single-sided
and no texture references. Emission starts with white colour and intensity 0;
conditional normal scale and occlusion strength start at 1. The canonical writer
emits these scalar values explicitly. Native descriptor defaults are not a
substitute: its current roughness default is 1, so omission would change newly
authored appearance. Migration preserves existing values instead of applying
creation defaults to older materials.

Reject non-finite and out-of-range entered values without changing source or
history. Sliders may constrain their own interaction range; they do not authorize
silent file/cooker clamps. HDR intensity is not limited to [0,1]. The 65504 limit
provides a finite HDR working range compatible with common binary16 render
intermediates; it does not authorize binary16 storage of the authored factor.

For a display sRGB component s, use s/12.92 when s <= 0.04045, otherwise
((s+0.055)/1.055)^2.4. Persist float32 linear colour. Do not repeatedly convert
unchanged values or apply display gamma during cooking.

Cook `emissive_factor = emissive_color * emissive_intensity` once in float32.
The native `MaterialAssetDesc` stores all three factor channels as float32;
update its engine-owned version and serializers/readers, schema compatibility
checks and producers, then recook all affected material content. The current
binary16 factor is insufficient: 9.7 becomes 9.703125, exceeding the unchanged
1e-4 comparison bound. Do not quantize the expected source to binary16 or loosen
semantic thresholds to conceal that loss.

Save/reopen compares colour and intensity independently. Native material
observations compare the independently computed float32 RGB factor. Multiple
source pairs can produce the same factor, so native observations do not pretend
to reconstruct the source colour/intensity split. Include hand-authored golden
cases for sRGB conversion, zero intensity, HDR products and the 9.7 precision
case. Shader accumulation and output must remain finite at the chosen range;
HDR values do not guarantee unclipped final display pixels.

### 7.4 Material asset identity

Assignments contain material asset URIs, never physical cooked paths. Resolve the
winning project/library identity through the normal catalog/provenance rules.
A missing material remains an authored reference with a visible diagnostic.
A scene instance assignment never changes the shared material or mesh asset.

### 7.5 Existing geometry slots and repair

Every existing mesh material slot is assignable. The native geometry inventory
owns opaque 128-bit `MaterialSlotId` values (canonical UUID text in JSON), layout
revision, labels, mesh defaults and per-LOD/submesh bindings. See
[content-pipeline section 20](content-pipeline.md#20-canonical-material-slot-identity-and-reimport)
for the complete identity, witness and publication contract.

A scene override is keyed by geometry URI and SlotId, with its observed layout
revision for stale-operation checks. Slot labels, list order and runtime indices
are presentation/transport facts, not authored identity. Clearing removes that
slot's override and restores its mesh-assigned default. It does not assign an
unrelated engine material or clear another slot. Explicitly choosing the engine
default remains an ordinary material assignment.

Unproven reimport continuity retains unresolved overrides, their material URIs
and prior slot context. The inspector offers Reassign to an existing slot or
Clear; each is one scene command with Undo/Redo. Do not guess a name/index match.
Unresolved overrides block cooking and qualification of affected scenes, while
unrelated scenes remain usable. Known missing material resources remain distinct
from missing slot identity. Transient geometry loading is not a structural slot
failure and must recover without restarting the editor.

## 8. Commands, Services, Or Adapters

### 8.1 Material commands

Extend the existing `IMaterialDocumentService` and shared property pipeline.
Create/open/edit/save/cook/close use document lifetimes and normal results.
A property edit validates, replaces the immutable branch, advances authoring
revision and records one history entry per committed gesture. Save alone persists
source. Material cooking delegates to the shared pipeline.

### 8.2 Assignment to geometry

The scene command takes explicit targets `(NodeId, GeometryUri, SlotId,
ExpectedLayoutRevision)` plus the selected material URI, or null to clear.
Validate all targets against the current inventories before mutation. A stale
layout or incompatible multi-selection rejects the command atomically and names
the affected target. Never apply a slot merely because it has the same visible
row number on another selected mesh.

The picker supplies material identity; the scene command owns source mutation,
dirty state, history and live sync. All existing slots, including nonzero slots,
use the same native binding and clear route. Unsupported/missing native behavior
is a failed capability/qualification gate, not the supported V0.1 outcome.

### 8.3 Operation kinds

Reuse `Material.Create`, `Material.Open`, `Material.EditScalar`, `Material.Save`,
`Material.Cook`, `Material.AssignToGeometry` and scene slot-edit operations.
Slot repair is an explicit scene edit with target and old/new identity evidence.

## 9. UI Surfaces

Use the existing Material Editor document, shared property cards/number boxes,
Content Browser create/open flow and material picker. Present one compact shared
cook-state chip near the name, current inline field diagnostics and an
approximate swatch. Routine success does not create a banner.

The status feed includes automatic, asset, folder and project cooks. Reopening or
reactivating a material must not replace publication facts with its last explicit
cook result. Closing detaches document subscriptions without cancelling unrelated
shared cooking.

The swatch previews current base/PBR/emission values with a fixed documented
viewing transform and is labelled approximate. It is not native rendering or HDR
parity evidence. Scene instances continue to show published content until Save
and successful publication; pending state explains this relationship.

Create New chooses a name and target using project-layout-and-templates rules,
writes canonical source and opens the document. Assignment is a separate explicit
slot action. Each existing slot row shows its identity/label, default, override
and resolution state; obsolete slot context is shown only for explicit repair.
Slot topology creation/removal is not part of this editor.

## 10. Persistence And Round Trip

- Persist canonical source fields through the existing atomic file store.
- Preserve linear colour/HDR values, alpha semantics and read-only texture refs.
- Persist geometry/SlotId/material identities and unresolved repair context.
- Do not persist runtime slot indices, loaded asset pointers or cooked paths.
- Migrate useful pre-V0.1 data once and reject retired schemas thereafter.

## 11. Live Sync / Cook / Runtime Behavior

### 11.1 Save

Serialize document writes, capture the saved revision under the authoring lock,
then release that lock during I/O. Atomic replacement and revision acknowledgement
are separate guarantees. Newer edits remain dirty; failure leaves the prior file.
Save schedules incremental work only after successful persistence and completes
independently of cooking.

### 11.2 Cook

`MaterialCookService.CookMaterialAsync` routes to
`IContentPipelineService.CookAssetAsync`. Native material-descriptor cooking owns
the binary output. Dirty participating documents require explicit Save; automatic
work shows Needs save. The fixed layout remains:

```text
source: <ProjectRoot>/Content/Materials/Gold.omat.json
cooked: <ProjectRoot>/.cooked/Content/Materials/Gold.omat
index:  <ProjectRoot>/.cooked/Content/container.index.bin
```

Only validated, journaled publication makes a material current. MaterialEditor
never writes/remounts a cooked root itself. Report the actual captured revision,
publication and mount state, including offline NotMounted.

### 11.3 Preview

Assignment queues identity-based runtime intent. Native asset completion and
geometry-layout acceptance remain generation checked. A transient unavailable
geometry/material produces a visible pending/failure state, retains authored
intent and can recover. Structural missing slots require repair, not retry loops.

Successful material publication refreshes every current instance of that material
without restarting. Unsaved material edits affect the swatch and stale-state
presentation only. Asset demand schedules the saved dependency through the shared
coordinator; it never publishes an unsaved material.

## 12. Operation Results And Diagnostics

Use `MaterialAuthoring` for field/create errors, `Document` for persistence,
`ContentPipeline`/`AssetCook` for cooking/publication, `AssetIdentity` for missing
assets and `LiveSync` for native application. Preserve native technical codes.
Diagnostics identify document/project lifetime, material URI and, for assignment,
node, geometry URI, SlotId, expected/actual layout revision and previous label.

Distinguish invalid input, Needs save, missing material, loading geometry,
missing/stale slot layout, rejected native application, cook failure and publish
failure. Never report a queued command as a loaded material or displayed frame.

## 13. Dependency Rules

MaterialEditor uses managed material contracts and public ContentPipeline/picker
services. Geometry UI consumes slot inventory through supported content APIs.
It does not decode native files or implement material asset editing. No editor
feature calls native facades directly or creates a second history/save/cook path.
Production modules have no dependency on development parity schemas or drivers.

## 14. Validation Gates

1. Every canonical scalar and read-only texture identity survives Save/reopen.
2. Emission zero/nonzero/HDR and 9.7 factors satisfy unchanged 1e-4 semantics;
   invalid values fail without source/history mutation.
3. Colour/slider gestures, cancellation, no-op, Undo/Redo and close/reopen preserve
   document history and newer unsaved revisions.
4. Every existing slot supports assign/clear, nonzero-slot editing, multiple
   instances, stable layout reload and missing-material recovery.
5. Slot reimport tests cover exact unchanged layout, parameter/texture-only edits,
   persistent source IDs, unproven reorder/removal, explicit repair and rollback.
6. Native opaque/masked/blended emission works with lighting, exposure and bloom
   before real editor Save/cook/assignment/refresh validation.
7. Pending and failed publication never masquerade as current scene rendering.
8. Normal Debug/Release packages contain no development qualification dependency.

## 15. Qualification And Historical Evidence

Current requirements are above. Historical validation stays at its recorded
scope in [ED-M07A field workflows](../validation/ED-M07A-field-workflows.md),
[ED-M07B closeout](../validation/ED-M07B-closeout-audit.md) and
[material sidedness/recovery](../validation/material-sidedness-and-recovery.md).
Those reports do not establish emission, all-slot identity or new-format parity.
Implementation/evidence completion is recorded in the milestone status ledger.

## 16. Shared Property, Save, And Preview Contract

The property pipeline owns descriptor identity, validation, gestures and history
mechanics; the material document owns its revisions and source. Scene targets
stay outside Schemas. Save, authoring, published content and current runtime
readiness are distinct states with one owner each.

## 17. Document History

Capture before/after snapshots under the material authoring lock and replay them
through the same canonical source-commit path. Advance revisions before async
notifications. One committed scalar/colour gesture creates one history entry;
rejected, cancelled and unchanged edits create none. Document switches/close
cannot redirect history commands to another material lifetime.

## 18. Shared Atomic Save

Scene and material persistence use the same atomic storage primitive: unique
same-directory temporary files, complete writes/flush, replacement, cleanup and
conflict checks. The write-result acknowledgement cannot overwrite a newer source
snapshot. These guarantees cover process interruption and reported I/O failure;
they do not assert universal hardware power-loss durability.
