# ED-M07A - Authoring Integrity And Runtime Convergence

Status: `planned; no implementation or validation completion claimed`

## 1. Purpose

Close identified omissions in the delivered inspector/property workflow and the
save/sync guarantees required for V0.1. Earlier milestone delivery and evidence
remain recorded as they were. This is new execution work, not an M04 reopening,
replacement, or another baseline audit.

## 2. PRD Traceability

`REQ-005` through `REQ-009`, `REQ-011`, `REQ-012`, `REQ-014`, `REQ-022`,
`REQ-024`, `REQ-026`, `REQ-037`, `REQ-038`; `SUCCESS-002`, `SUCCESS-003`,
`SUCCESS-007`.

## 3. Required LLDs

[property-pipeline.md](../lld/property-pipeline.md),
[property-inspector.md](../lld/property-inspector.md),
[environment-authoring.md](../lld/environment-authoring.md),
[settings-architecture.md](../lld/settings-architecture.md),
[documents-and-commands.md](../lld/documents-and-commands.md),
[material-editor.md](../lld/material-editor.md),
[live-engine-sync.md](../lld/live-engine-sync.md), and
[runtime-integration.md](../lld/runtime-integration.md).

## 4. Identified Delivery Gaps And Evidence

Source inspection is against the committed `72c42e1cd` worktree. Concurrent
save-revision fixes are a separate workstream and are not assumed landed here.

| Gap | Concrete source evidence | Owning task |
| --- | --- | --- |
| Background falsely shares successful environment sync status | `SceneEngineSync.UpdateEnvironmentAsync` sets BackgroundColor to the overall environment outcome; `SyncEnvironmentSystemsAsync` calls `OxygenWorld.SetEnvironment` without any background RGB argument. | 07A.1 |
| Camera/light/environment gestures are submitted as separate one-shot edits | `PerspectiveCameraViewModel.ApplyCameraEditAsync`, `DirectionalLightViewModel` edit methods, and `EnvironmentViewModel.ApplyEnvironmentEditAsync` pass `EditSessionToken.OneShot`. | 07A.2 |
| Rejected camera edit lacks field-level rejection/restore handling | `PerspectiveCameraViewModel.ApplyCameraEditAsync` returns on `!result.Succeeded`; the camera XAML has no field diagnostic binding. The attempted VM value is not refreshed on this path. | 07A.3 |
| Pending property replay has no saved/current revision or document-lifetime boundary | `PendingPropertySyncQueue` stores scene ID, node ID and entries; `ReplayPendingPropertySyncs` drains before attempting replay and catches failures without retaining failed work. | 07A.4 |
| Existing evidence does not prove the complete UI/engine chain | `SceneEngineSyncTests.Coalescer_ThrottlesPreviewAndAllowsOneTerminalSyncThroughSyncService` proves the helper's call count, not a camera/light/environment control gesture, field rejection, or presented runtime value. The M04 ledger explicitly records only partial visual evidence. | 07A.6 |

Already present, therefore not assigned for reimplementation: EnvironmentView
has sky/sun/exposure/tone/bloom/color-grading/background sections;
SceneNodeEditorViewModel constructs the environment editor for scene selection;
material slot sync calls SetMaterialOverride with descriptor-to-cooked URI mapping;
scene/property command validation, undo paths, schema catalogs, and the sync
coalescer have implementations. Exercise these paths when closing the named gaps;
do not replace them merely because the old ledger lacked complete evidence.

## 5. Non-Scope

No generic validation dashboard, new property architecture, full inspector
rewrite, project-settings panel, autosave, texture authoring, or M04 closure
sweep. Content descriptor/publication gaps belong to 07B; standalone parity to
M08; viewport tools to M09. Existing record adapters may remain when they obey
the canonical property contract. Purely cosmetic schema-native VM rewrites are
not release requirements.

## 6. Implementation Sequence

### 07A.1 - Background Application And Truthful Per-Field Results

Add a supported native scene-background capability through the engine API and
Runtime/Interop boundary, wire BackgroundColor through scene synchronization,
and apply it when atmosphere is disabled. Return accepted status only for fields
actually dispatched; an absent capability must identify BackgroundColor as
unsupported/failed rather than borrow another field's success.

Pass: set non-black background with atmosphere off, observe the exact native
value and visible change, undo/redo and save/reopen it; inject native rejection
and verify field-specific diagnostics with authoring retained. Cooked background
mapping is 07B.3 and final visual parity is M08.

### 07A.2 - Real Gesture Sessions For Existing Inspectors

Connect numeric drag, text commit, color gesture, and wheel begin/preview/commit/
cancel to the existing CommitGroupController/property command path for camera,
light, and environment fields. Resolve target identity at begin; end/cancel the
session before selection or document changes. Preserve the existing controls.

Pass: 100 value samples from each affected control make one undo entry, at most
one preview per 16 ms, and one terminal application; Escape restores before-values
without adding history. Wheel commits after 250 ms idle. Two targets with different
initial values undo to their respective originals. Late callbacks cannot change
a different selection or reopened document.

### 07A.3 - Scoped Current Field Diagnostics

Bind rejected command results to the originating PropertyId/target/document
lifetime, restore or refresh displayed committed values, and display the error
at the affected field. Replace current diagnostics for touched scopes on a newer
revision; retain operation history separately. Successful edits clear their
resolved field error. Revalidate near/far together and sun/reference dependents
when their target changes. Do not scan unrelated fields on every edit.

Pass: Near >= Far and a zero scale axis leave model/history unchanged, show inline
errors, and never leave controls pretending the invalid value is committed. A
valid subsequent edit clears only the resolved current error; changing selection
or deleting the target cannot attach stale diagnostics to another inspector.
M09 consumes this same mechanism for transform tools; it adds no validation model.

### 07A.4 - Revision-Aware Offline Convergence

Add document lifetime and revision identity to pending sync. Full current-scene
synchronization supersedes queued values at/before its captured revision. Replay
only later valid work; retain failed work/status and discard invalid lifetimes.
Scene activation and close must invalidate stale queues and completion callbacks.

Pass: edit offline, undo offline, reconnect, fail/retry replay, delete a queued
node, switch scenes, close/reopen the same source, and edit during full resync.
The live scene converges to current committed authoring values; the pending
indicator cannot clear after an unreported failed replay.

### 07A.5 - Scene/Material Save Integrity And Conflict Handling

Integrate the existing save-revision issue fixes when landed, then implement
remaining document-contract gaps: atomic scene replacement, serialized writes,
coherent snapshots/revision acknowledgment, external-change rejection and conflict
choices, single-writer coordination, and explicit-Save-only crash safety. Do not
redo an issue fix that already supplies the required behavior.

Pass: deterministic storage gates hold save A while edit B commits; A succeeds
without clearing B, queued saves cannot overwrite out of order, and failure leaves
the prior valid file and current history intact. Repeat for already-dirty edits,
undo/redo, explorer layout and material source. Exercise interrupted replacement,
external modification, Save Copy, and save-and-close with a newer revision.

### 07A.6 - Complete The Missing Workflow Evidence

Run concrete cases through actual controls/commands: Transform primary axes;
Geometry pick/missing URI and material slot pick/clear/mixed values; camera FOV,
near/far/aspect; directional color/intensity/sun/environment/shadows/affects-world/
angular-size/exposure fields; and every editable SkyAtmosphere/PostProcess/
Background field in the environment LLD. Include empty selection opening the
scene settings, stale sun deletion, component add/remove with Transform removal
denied, defaults/null environment loading, and runtime FPS/logging rejection.
For each field, record changed value, undo/redo result, reopened saved value,
actual native observation or field-specific failure, and UI diagnostic outcome.

Pass: the case table has an observed result for every declared field and the
listed transitions. All required behavior passes. Source-only tests and prior
partial manual notes do not substitute for the control/native cases. Missing
native capability is a failed gate, not a request for a new audit.

## 7. Project/File Touch Points

- `WorldEditor/src/Inspector`: existing camera/light/environment/geometry/host
  views and VMs; field-result bindings and session wiring.
- `WorldEditor/src/Documents/Commands`: property adapters, revision/history and
  document sessions; consume the concurrent save-fix changes.
- `WorldEditor/src/Services`: SceneEngineSync, pending queue and outcome mapping.
- `Schemas/src`: existing session/apply/snapshot mechanics only where the named
  behavioral gaps require a shared change.
- `MaterialEditor/src`, `Projects/src`, `World/src/Serialization`: persistence
  owners and coherent serialized snapshots.
- `Runtime/src/Engine`, `Interop/src/World` and `Interop/src/Commands`: background
  capability and runtime observations. Engine APIs own native scene/render state.
- Existing WorldEditor, MaterialEditor, Schemas and Projects test projects.

## 8. Risks And Containment

Source save fixes are being developed independently: merge their contracts and
proof without claiming them before landing. Preserve domain-specific sun/asset
rules when adding generic sessions. Native background work must use engine-owned
capabilities. Save conflicts must not be resolved by silently discarding edits.
There is no autosave scope expansion.

## 9. Validation Gates

- [ ] 07A.1 truthful background dispatch and live behavior pass.
- [ ] 07A.2 gesture/history/selection/cancel cases pass on the existing controls.
- [ ] 07A.3 scoped inline diagnostics and dependent invalidation cases pass.
- [ ] 07A.4 offline/reconnect/lifetime convergence cases pass.
- [ ] 07A.5 scene/material persistence and conflict cases pass.
- [ ] 07A.6 field/workflow evidence is complete and the user has validated the
  visible behavior; native acceptance alone is not presented-state proof.

## 10. Status Ledger Hook

Record one ED-M07A validation row after its gates pass. Preserve earlier M03,
M04, M05 and other milestone records and their original evidence. M04 has no new
sweep or closure action; the missing work identified above executes here.
