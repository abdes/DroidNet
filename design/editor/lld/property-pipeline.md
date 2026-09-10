# Property Pipeline LLD

Status: `V0.1 design contract; implementation and validation incomplete`

## 1. Purpose

Define the authored-property path for scene and material editors: schema
identity, validation, mixed values, edit sessions, commands, undo/redo,
revision tracking, persistence, and runtime projection. Inspector, material,
and live-sync LLDs consume this canonical contract.

## 2. PRD Traceability

`REQ-005` through `REQ-009`, `REQ-011`, `REQ-012`, `REQ-014`, `REQ-022`,
`REQ-024`, `REQ-026`, `REQ-037`, `REQ-038`; `SUCCESS-002`, `SUCCESS-003`,
`SUCCESS-004`, `SUCCESS-007`.

## 3. Architecture Links

- [ARCHITECTURE.md](../ARCHITECTURE.md): module and native boundaries.
- [documents-and-commands.md](./documents-and-commands.md): document lifecycle,
  history, save ownership, and selection.
- [property-inspector.md](./property-inspector.md): supported scene fields/UI.
- [material-editor.md](./material-editor.md): material fields and persistence.
- [live-engine-sync.md](./live-engine-sync.md): runtime adapter and completion.
- [settings-architecture.md](./settings-architecture.md): durable scope.

## 4. Source Baseline

The repository contains `Oxygen.Editor.Schemas`, `PropertyId`, `PropertyEdit`,
`PropertyDescriptor<T>`, `PropertySnapshot`, `PropertyOp`, `PropertyApply`,
`PropertyBinding<T>`, and `CommitGroupController`. Scene/material services expose
schema-property entry points. Native `SetProperties` covers transform,
perspective-camera, and directional-light scalar fields; geometry/material
references and scene environment use specialized adapters. These source facts
do not prove all gates below pass. Existing record adapters may implement the
same contract; a second mutation/history authority is forbidden.

## 5. Target Design

```text
field binding / viewport intent
  -> owning document command or material service
  -> schema and domain validation of the complete proposed change
  -> synchronous authoring commit + revision + undo record
  -> scoped notifications and runtime sync request
```

Engine schemas own runtime descriptor structure, types, ranges, and units.
Editor overlays carry `x-editor-*` UI annotations and schema references; they
cannot weaken validation. Scene authoring DTOs may contain editor-owned
structure under the scene-model LLD, with an explicit runtime mapping.
Cross-field and domain rules, including near/far ordering, cardinality, valid
references, and exclusive sun binding, remain enforced by the owning service
in addition to schema validation.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| Engine schemas/cooker | Runtime descriptor definitions, native property dispatch, cooked encodings. |
| `Oxygen.Editor.Schemas` | Schema/overlay loading, descriptors, typed edits, snapshots, mixed values, reusable apply/session mechanics; no WinUI or native runtime dependency. |
| WorldEditor commands | Scene targets, domain rules, authoring transaction, revision/history, scene sync intent. |
| MaterialEditor document service | Material transactions, revisions/history, persistence, explicit cook requests. |
| Editor.UI and feature UI | Reusable field controls and composition, without independent mutation authority. |
| Runtime/interop | Stable engine property keys and specialized capabilities on the proper frame phase. |

Descriptors identify engine command keys. They do not store PAK byte offsets
or implement cooked binary readers/writers. Round-trip tests use supported
engine/cooker interfaces; the engine remains the binary-format authority.

## 7. Data Contracts

- `PropertyId`: component/schema kind plus JSON pointer; `PropertyId<T>` adds
  value type. Identity is independent of display labels.
- `PropertyDescriptor<T>`: identity, model accessor, value validator, editor
  annotation, and engine command key where applicable.
- `PropertyEdit`: typed changed values. Clone/freeze submitted values so later
  caller mutation cannot change queued work.
- `PropertySnapshot`: values per stable target ID, preserving heterogeneous
  initial values in a multi-selection.
- `PropertyOp`: target identities and before/after snapshots. Undo and redo use
  the same apply mechanism with opposite snapshots.
- Edit session: document lifetime, stable target set, touched properties,
  before snapshot, previews, terminal commit/cancel. Never resolve its target
  from a later current selection.
- Authoring revision: advances with every committed mutation and undo/redo,
  including already-dirty documents, before asynchronous work. Save first ends
  an active gesture through its owner and captures a committed revision.

## 8. Commands And Sessions

1. Bindings submit typed edits through scene-command or material-service
   property entry points. Record adapters converge on the same transaction.
2. Resolve all targets and validate the complete proposed state before changing
   any target. Stale/invalid targets reject the transaction without partial edits.
3. Apply model changes synchronously under the author's gate; publish revision
   and history for the successful commit before awaiting runtime work.
4. One gesture creates one undo entry. Previews add no history; commit captures
   final values; Escape/cancel restores before-values with no new history.
   A net no-op adds no entry.
5. Selection change commits valid text edits or cancels an unfinished drag
   before changing selection. Target deletion/close cancels the session before
   invalidating its lifetime. Reopened documents cannot inherit queued sessions.
6. Wheel edits commit after 250 ms idle. Scene preview requests may coalesce to
   the existing 16 ms cadence. Commit/cancel emits its final state once, after
   pending previews. Sync cadence never controls undo granularity.
7. Invalid values are rejected. Where an accepted field normalizes values,
   return/display the applied value according to the field contract. The UI
   must not display a value different from the committed authoring state.

## 9. UI Surfaces

Mixed values have an explicit indeterminate state, never an editable placeholder.
Editing one coordinate/channel preserves all other channels on each target.
Controls derive hints from descriptors; commands remain authoritative.
Invalid edits show field feedback without changing authoring state. Color
controls use the field's declared linear/sRGB conversion.

Pending/failed sync is visible near the scene/viewport and in operation results.
Material swatches are approximations; runtime material values update after
explicit Save/Cook publication per the material LLD.

## 10. Persistence And Round Trip

The document owner acknowledges the saved revision and preserves newer edits.
Engine handles, sessions, history, and diagnostics are not authored data.
Schema/domain validation applies on edit and load. Incompatible versions are
rejected without rewriting the file. Every editable V0.1 field is covered by
save/reopen/cook/load tests through its accepted mapping, including units and
enum ordinals. The PRD capability matrix determines scope; discovering another
engine schema leaf does not automatically add an editor feature.

## 11. Runtime Projection

Scalar edits use `SetProperties` on `OnSceneMutation`. Geometry/material identity
uses asset-reference adapters; environment uses scene-system publication.
Surface/view/mount changes use the frame-start runtime boundary. The property
layer does not force non-scalar payloads through a scalar wire or start engines.

When runtime is unavailable, authoring remains valid and sync is visibly pending.
Retain document lifetime, scene identity, and revision on queued work. On
activation/reconnect, synchronize a coherent current scene snapshot; it
supersedes queued edits at or before its revision. Replay only newer matching-
lifetime edits, in revision order, before declaring current state. Failed replay
remains pending/failed. Close or project replacement invalidates old work.
Only one scene is live at a time; inactive documents cannot dispatch into the
active scene's engine context.

Accepted/queued native requests do not prove presented pixels, asset resolution,
or cooked parity. Required capability failures block release gates even when
the authoring command properly succeeds with a warning.

## 12. Operation Results

Cancellation before authoring commit yields `Cancelled` with unchanged state.
After commit, downstream cancellation/failure is a child sync result; the parent
reports the successful authoring mutation with warnings/partial success as
appropriate. It must not imply that authoring was cancelled. Include operation,
document/scene lifetime, affected IDs, and revision where known. Invalid edits
use authoring diagnostics; native unavailability uses sync/runtime diagnostics.
No log parsing determines operation success.

## 13. Dependencies And Schema Delivery

Schemas depends on JSON-schema validation and shared non-UI primitives. Feature
owners supply target adapters; Schemas cannot depend on feature implementations.
Engine schemas and sibling overlays ship with the matched editor build.
`*.editor.schema.json` is excluded from native embedding. Overlay-only UI edits
do not require engine code changes. Runtime/cooker/schema compatibility follows
the PRD qualification manifest, not arbitrary files from another installation.

## 14. Validation Gates

ED-M07A closes these cross-cutting gaps for scene and material properties.
Earlier ED-M03/04/05 evidence remains scoped to its originally recorded work:

- [ ] All editable V0.1 fields have identity, validation, mutation, persistence,
  and their required runtime/cook mapping.
- [ ] Overlay lint, references, coverage of the declared V0.1 field set,
  validator agreement, and native-embedding isolation pass.
- [ ] Mixed values, partial coordinates, and invalid multi-target edits preserve
  untouched values and never leave partial authoring changes.
- [ ] Apply/undo/redo, commit/cancel/no-op, selection changes, deletion, and
  reopening preserve the documented per-target values and history.
- [ ] Revision precedes async work; save A followed by edit B acknowledges A
  while B stays dirty, including undo/redo and explorer-layout changes.
- [ ] Offline edits, reconnect, superseded queues, failed replay, activation,
  and document lifetimes cannot apply stale values to a live scene.
- [ ] Supported cook/load APIs prove field round-trip without editor-owned PAK
  offsets. ED-M08 separately qualifies visual parity.

## 15. Scope Decisions

Schema properties, specialized adapters, revision-aware resynchronization, and
explicit material Save/Cook preview are the V0.1 contract. No alternative wire
or property architecture remains undecided. Unchecked gates are implementation/
validation work; progress lives in [IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md).
