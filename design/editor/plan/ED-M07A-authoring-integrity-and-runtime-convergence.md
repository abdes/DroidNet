# ED-M07A - Authoring Integrity And Runtime Convergence

Status: `validated` (2026-09-11)

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

Source inspection is against `editor` at `ea395a310` after rebasing. Issues
[#2](https://github.com/abdes/DroidNet/issues/2),
[#3](https://github.com/abdes/DroidNet/issues/3),
[#4](https://github.com/abdes/DroidNet/issues/4) and
[#5](https://github.com/abdes/DroidNet/issues/5) are landed. Their documented
automated evidence is retained; running-editor replay is not inferred.

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

GitHub issue ownership (issue reports were rechecked against the rebased source):

| Issue | Current evidence and remaining scope | Owner |
| --- | --- | --- |
| [#6](https://github.com/abdes/DroidNet/issues/6) | EngineService.State already returns Faulted when the stored loop task completes; surface waits observe loop exit after #3. No ongoing run observer publishes the original fault/exit result or lifetime-scoped state event. | 07A.7 |
| [#7](https://github.com/abdes/DroidNet/issues/7) | Scene snapshot writes still call WriteAllTextAsync directly. Material writes already use a same-directory temporary file plus File.Move; shared guarantees, durability/cleanup and boundary failure tests remain. #4 revision acknowledgment is implemented and must be preserved. | 07A.5 |
| [#9](https://github.com/abdes/DroidNet/issues/9) | Material source edits commit and mark dirty/stale but do not register document history. Schema validation and the #4 authoring lock do not supply undo/redo. | 07A.8 |
| [#10](https://github.com/abdes/DroidNet/issues/10) | IEngineService exposes concrete OxygenWorld/OxygenInput and feature input/sync constructs or consumes interop payloads. | 07A.0 |

Landed foundations to retain: #2 close/discard guards; #3 serialized lifecycle,
cleanup and loop-ended surface waits; #4 coherent snapshots, per-destination
serialization and revision acknowledgment (232 recorded automated tests); #5
native geometry/material completion generations, scene-session invalidation and
mutation-phase application (34 recorded native tests, 19 issue-specific).
Do not reimplement these or claim their automated evidence proves the new UI/
rendered gates. #5 does not add revision/lifetime identity to the separate
managed PendingPropertySyncQueue, so 07A.4 remains required.

## 5. Non-Scope

No generic validation dashboard, new property architecture, full inspector
rewrite, project-settings panel, autosave, texture authoring, or M04 closure
sweep. Content descriptor/publication gaps belong to 07B; standalone parity to
M08; viewport tools to M09. Existing record adapters may remain when they obey
the canonical property contract. Purely cosmetic schema-native VM rewrites are
not release requirements.

## 6. Implementation Sequence

### 07A.0 - Managed Runtime Capabilities (#10)

Define Runtime-owned, UI-independent `IRuntimeWorldCommands` and
`IRuntimeInputCommands`, exposed as typed capabilities by IEngineService. Move
concrete OxygenWorld/OxygenInput ownership and DTO conversion into internal
Runtime adapters. Port SceneEngineSync and viewport input to these capabilities
in one migration; remove the concrete facade properties from the feature-facing
service rather than keep compatibility forwarding accessors.

Managed DTOs carry runtime session, scene/view lifetime, authored node identity,
property values and input semantics using managed primitives. Pointer positions
are physical viewport pixels; WinUI event interpretation stays in the UI bridge,
and native enum/layout conversion stays in the Runtime adapter. No second scene
model or authoring policy enters Runtime. Preserve #5 native request generation
and queued/accepted completion semantics under the new adapter.

Forward current native asset-load failures through managed result/event reporting
using #5 request identity; do not recreate its loader generation mechanism or
report superseded failures against current authoring.

Pass: managed substitutes drive every world/input success, rejection, cancellation
and fault path; conversion tests cover identities, units, key/button/modifier
values and numeric payloads. Feature code contains no OxygenWorld/OxygenInput
access or native input DTO construction. Existing navigation, scene mutation,
#2-5 lifecycle/save/load regressions and sync outcome behavior remain passing.
Run this contract migration before adding new background/supervision behavior.

Implementation and automated gate (2026-09-10): `IEngineService` no longer
exposes concrete world/input facades. Scene sync and viewport input use immutable
managed requests with run, document, scene activation and view-generation identity.
Runtime owns native DTO conversion; the UI bridge supplies physical pixel input.

Current native asset failures now carry their existing #5 generation and original
managed request through the native command, Runtime event and scene operation-result
surface. Native mutation-phase validation discards obsolete completions; Runtime
and UI delivery recheck operation/target identity. Detach, scene replacement and
shutdown invalidate old notifications. No second native generation authority or
log-text parsing was introduced.

MSBuild passed the changed Runtime/WorldEditor projects and native/managed test
projects. VSTest passed Runtime 48/48 (including a live native-to-managed asset-failure
case), WorldEditor SceneExplorer 95/95 and Interop NativeTests 35/35. Collected compiler
SARIF reports have no unsuppressed analyzer or IDE diagnostics in this slice's managed
files. Native test discovery uses the engine install `Debug/bin` on process `PATH`.
Existing unrelated editor diagnostics remain outside this change. Issue #10's
implementation and automated acceptance criteria are satisfied; complete visible
control/navigation evidence remains part of the milestone's 07A.6 gate.

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

Implementation: the native `Background` environment system supplies linear SDR
color for composition after foreground exposure and tone mapping. Scene color
alpha records opaque/masked coverage and accumulated translucent coverage;
compositing preserves foreground shading and bloom. Atmosphere takes precedence,
reflection captures exclude the presentation background, and HDR sky settings
remain independent. The picker converts between display sRGB and linear source
values. Per-field dispatch results retain background-specific failures.

Validation: Interop native tests pass 36/36 and packaged inspector tests pass
22/22. They cover native RGB, atmosphere precedence, reflection exclusion, HDR
sky preservation, invalid input, real picker gestures, Undo/Redo and atomic
Save/reopen. Lighting and post-process tests pass 4/4 and 6/6. Editor builds pass,
and changed C# files have no analyzer or IDE diagnostics. The user verified that
the rebuilt editor matches the picker, retains that color across exposure and
tone-mapper changes, and restores it through Undo/Redo and Save/reopen.

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

Current implementation and automated evidence (2026-09-10): camera, directional
light and environment controls now feed captured gesture sessions through the
shared property snapshot/controller path. Commit creates one history entry;
cancellation restores each original target. Wheel input commits after 250 ms
idle. Per-target color edits preserve untouched channels. Rebinding, hidden
inspectors and component replacement terminate old sessions; duplicate terminal
callbacks cannot apply another value. Transform requests capture their phase
before asynchronous dispatch and recheck cancelled wheel callbacks on the UI
thread. Save/close integration remains in 07A.5; actual control coverage remains
in 07A.6.

Pointer regression and qualification (2026-09-11): the user found that releasing
a picker drag restored its original color. Our capture-loss handler cancelled
the gesture before the normal release reached the picker. WinUI's ColorSpectrum
releases its child capture within its release handler; the editor now commits
when that pointer is no longer in contact, and cancels capture lost while it is
still pressed or explicitly cancelled. This applies to environment, light and
material pickers. Nine packaged tests inject real Windows clicks, drags and
interrupted capture into the actual flyout controls, checking selected color,
one-entry Undo/Redo and cancellation without history. The prior programmatic
color-sample test did not exercise pointer-release ordering.

The full packaged inspector suite passes 20/20, including native RGB/save/reopen
coverage. MSBuild rebuilt the affected views, UI test host and Debug editor;
changed picker/test files have no unsuppressed analyzer or IDE diagnostics,
including SARIF notes. The user also confirmed that the picker works after the
fix. This closes the reported picker reset, not the separate exposure failure
or all remaining 07A.2/07A.6 gates.

Packaged numeric qualification (2026-09-11): camera, light and environment
controls each pass 100-sample history, 16 ms preview spacing, terminal delivery,
cancellation and wheel-idle cases through the real inspector host. Mixed camera
and light selections restore each original value on Undo. Selection cancellation
now publishes its new scope before old-model notifications can reenter binding,
preserving rejection of late callbacks. The packaged suite passes 37/37.

Escape qualification: NumberBox now focuses captured drags and cancels them on
Escape. Real Windows mouse/key tests pass for camera, light and environment;
release after cancellation creates no history. Inspector tests pass 40/40 and
shared controls pass 50/50. The Debug editor builds, with no analyzer or IDE
diagnostics in the changed control and test files. The 07A.2 gate passes.

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

Current field feedback is bound by property, request ordering and document
lifetime. Numeric inspectors refresh rejected values and show inline errors;
camera near/far feedback is invalidated together. MSBuild compiler SARIF reports
have no unsuppressed analyzer/IDE diagnostics in the new gesture/diagnostic
implementation and tests. WorldEditor tests pass 110/110, including 100-sample
camera/environment gestures, mixed-target undo/redo, wheel idle, cancellation,
replacement components, repeated terminal delivery and stale diagnostic tickets.
The generated XAML connections match their control types in all four modified
inspector views. The user confirmed scene selection opens the inspector after
the TransformView connection-ID regression was corrected. This is one observed
UI case; the complete field/dependency and control/native table is still open.

Additional control and dependency qualification (2026-09-11): environment model
notifications now refresh the bound inspector after undo/redo. Sun candidates
and stale-reference feedback follow node/component removal, re-addition and
scene switches. Diagnostic objects remain stable when XAML binds before the
scene arrives. Ground albedo, sky luminance and background VectorBoxes now wire
their child-control gesture sessions and inline feedback. Unrelated environment
edits and their history no longer overwrite independently authored sun flags;
the sun side effects run only for an explicit sun-reference edit.

WorldEditor tests pass 140/140. Eleven packaged editor UI tests pass, including
all five modified inspector/material views loading, actual camera near/far and
zero-scale text validation, valid correction, three 100-sample environment vector
gestures, and the native background workflow above. Two realized NumberBox tests
also prove save-time valid commit, invalid cancellation and duplicate-terminal
protection. Synthetic lifecycle samples exercise actual controls and bindings;
the generic control cases use a managed sync substitute, while the background
workflow runs the real native engine. These bounded cases do not replace the
remaining complete per-field/runtime table or final user-visible qualification.

Numeric text qualification (2026-09-11): NumberBox keeps draft text local until
its explicit commit. Removing the template's duplicate two-way conversion
preserves exact float values and prevents a refresh from clearing an unrelated
field error. Packaged tests verify near/far dependent correction, retained aspect
feedback and clearing on selection change; shared text-control tests pass 4/4.

Sun dependency qualification: packaged tests select the sun through the actual
ComboBox, remove and restore either its node or light component, and verify the
inline warning, restored selection and unchanged authored reference/history.
The Clear button changes the reference explicitly. The inspector suite passes
42/42; current ordering/lifetime, numeric and sun feedback satisfy 07A.3.

### 07A.4 - Revision-Aware Offline Convergence

Add document lifetime and revision identity to pending sync. Full current-scene
synchronization supersedes queued values at/before its captured revision. Replay
only later valid work; retain failed work/status and discard invalid lifetimes.
Scene activation and close must invalidate stale queues and completion callbacks.

Pass: edit offline, undo offline, reconnect, fail/retry replay, delete a queued
node, switch scenes, close/reopen the same source, and edit during full resync.
The live scene converges to current committed authoring values; the pending
indicator cannot clear after an unreported failed replay.

Implementation and automated gate (2026-09-11): open-document metadata instances
own distinct projection lifetimes, including close-before-load and same-source
reopen protection. Commands capture payload/revision before asynchronous metadata
notification; notification no longer delays native publication. Full sync uses an
owned scene snapshot captured on the UI dispatcher. Only an accepted snapshot can
supersede earlier work. Later scalar, environment, hierarchy and component requests
replay in revision/preview order; failed requests retain their status and pending
count. Node creation precedes its subsequent property updates, and deleted or
replaced targets cannot resurrect stale fields. Topology calls carry their source
scene explicitly. Runtime scene invalidation ends pending acknowledgments.

The requested scene resynchronizes when Runtime announces a new running lifetime.
Authoring-load notification is separate from native readiness, so offline data can
populate the inspector and save path. Current readiness is published only after
replay; disposal retires document work before releasing the asynchronous sync gate.

WorldEditor tests pass 131/131, including real command edit/undo/reconnect/redo,
delayed metadata notification, snapshot/replay ordering, background failure and
recovery, topology/component changes during projection, inactive-scene isolation,
close/reopen, automatic recovery and disposal. Runtime tests pass 61/61, including
pending scene/node cancellation and old-activation isolation. Required visible
control/native field evidence remains in 07A.6.

Open-document activation follow-up (2026-09-11): Scene Explorer now reuses the
registered authoring model when switching back to an open scene. It no longer
rereads disk and replaces unsaved state/history during tab activation. A direct
view-model transition test switches between two scenes and back, verifies the
same dirty source and adapters, and asserts that no disk reload occurred.
WorldEditor tests pass 141/141 with this case and the later control feedback work.

### 07A.5 - Shared Atomic Save And Conflict Safety (#7)

Preserve the landed #4 snapshot/revision and writer serialization. Factor the
material temporary-file/rename path into a shared storage-level atomic-write
primitive used by both scene and material persistence. Write a unique temporary
file in the destination directory, close/flush it to the specified process-crash
contract, and atomically replace the destination. Define existing-file/first-save,
replacement failure, cancellation, cleanup, and target-collision behavior; do not
claim universal hardware power-loss durability. Keep document revision/history
policy in the authoring owner, not the storage primitive.

Add the document contract's external-change rejection, conflict choices and
single-writer coordination. Retain the explicit-Save-only recovery choice; no
autosave or unsaved crash recovery is introduced.

Pass: inject write/flush/replace failure and cancellation for existing and first
saves. Previous saved content remains complete; temporary files cannot be loaded
as assets or clobber a concurrent save. Repeat for scene/material owners, retain
#4 edit/save and save/save tests, and exercise external modification, Save Copy,
interrupted replacement and save-and-close with newer unsaved revisions.

Storage and authoring-owner implementation (2026-09-11): scene and material
snapshots now use the shared `IAtomicFileStore`. The native implementation owns
an exclusive destination write lease, compares complete-content baselines,
flushes and closes a unique same-directory temporary file, and publishes through
rename. Changed/inaccessible destinations and collisions report Document.Conflict.
Normal failures clean only the attempt's temporary file; process interruption
can leave an orphan `.tmp`, which is not an authored asset. Recovery remains
limited to the last successful Save; no hardware power-loss guarantee is claimed.

Save conflicts keep the close dialog open with inline Reload and Save Copy.
Both scene and material documents use these actions, including after ordinary
Save. Reload requires explicit discard confirmation; Save Copy creates a distinct
asset and retains the original document's dirty state and history. Recovery
requests and close preparation wait for pending work, and overlapping ordinary
Save requests share one conflict dialog.

Scene reload suspends new mutations, drains admitted commands and tree updates,
and checks revision, history and document lifetime before accepting the disk
snapshot. Acceptance replaces the model and baseline, retires old callbacks and
history, and rebinds the active tree and inspector. Inactive reloads do not change
the active inspector. Malformed source, identity/name mismatches, cancellation,
newer revisions and closed lifetimes retain the original model and baseline.

Validation: Storage 193/193 includes write/flush/replace failures, cancellation,
competing writers, real replacement denial and eight abrupt child-process exits.
Projects 54/54, Documents 66/66, MaterialEditor 41/41 and WorldEditor 156/156 cover
owner state, conflicts, source reload, copy identity, history and revision races.
Packaged UI 23/23 covers inline discard confirmation, pending copy behavior,
dialog deduplication and inspector regressions. DynamicTree 145/145 and
TimeMachine 76/76 verify the supporting mutation and history contracts. Editor
builds pass; changed managed files have no analyzer or IDE diagnostics.

### 07A.6 - Complete The Missing Workflow Evidence

The [field/workflow table](../validation/ED-M07A-field-workflows.md) records
completed cases. All 40 atmosphere/post-process and 38 component fields pass
source/native comparison, Undo/Redo, Save/reopen and clear inline feedback.
Geometry/material, sun consistency, component transitions, default loading and
runtime-setting rejection cases also pass. Packaged inspector tests pass 137/137
and Runtime tests 71/71. On 2026-09-11, the user confirmed composed XYZ rotations,
Cube/Sphere changes with a newly cooked material, None and Default, and coupled
sun controls in the restarted Debug editor through Undo/Redo and Save/reopen.
The 07A.6 gate passes.

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

### 07A.7 - Observe Runtime Loop Lifetime (#6)

Observe the loop task immediately after start with a unique run identity. Under
the lifecycle gate distinguish requested stop from unexpected normal exit/fault,
publish the original result through Runtime diagnostics and a managed immutable
state-change notification, and transition out of Running. A previous observer
cannot modify a newer run. Retain #3 cleanup ownership and loop-ended surface
wait handling; terminate other pending requests predictably without synchronous
UI waits. Ordinary shutdown produces no spurious runtime-fault notification.

Pass: direct managed-service tests use a controlled runtime session to fault,
exit unexpectedly, stop normally, race stop/exit and restart while an old observer
finishes. Assert state notification, original exception/exit cause, finite pending
request completion, and unchanged newer-run identity. Getter-based detection
alone does not satisfy active diagnostic publication.

Implementation and automated gate (2026-09-10): the loop observer shares the
command dispatcher's run identity and captures the original completion before
acquiring the lifecycle gate. Shutdown consumes that same completion before
clearing ownership, preserving failures when it wins the observer race. Ordered
immutable StateChanged notifications and Runtime.Loop operation diagnostics are
published outside the lifecycle gate. Requested cancellation is ordinary stop;
unexpected normal exit and non-cancellation exceptions are reported once. The
observer invalidates command delivery without destroying native resources.

Runtime tests pass 59/59, including nine new direct-service cases for fault and
unexpected exit with pending work, normal/cancelled stop, exit before shutdown,
fault during shutdown, immediate completion, failing subscribers and restart
inside an old notification. Existing native cleanup, asset-failure and background
tests remain passing. MSBuild passed Managed.Core, Runtime and the editor app;
changed implementation files have no unsuppressed compiler analyzer/IDE diagnostics.
This satisfies issue #6's runtime supervision gate without a presented-frame claim.

### 07A.8 - Material Document Undo/Redo (#9)

Add document-owned TimeMachine history using per-target before/after source
snapshots and the shared property/session mechanism. Route scalar and multi-
channel edits and undo/redo through the same validated apply/commit path under
the material authoring lock. Wire active-material-document undo/redo commands;
never use scene or global selected-document history implicitly. Preserve #4
revision acknowledgment and writer serialization.

Pass: scalar roughness/alpha and four-channel color edits undo/redo to exact
values; dirty and cooked-stale states stay coherent through save/undo/redo.
Drag/wheel/color sessions produce one intended entry; no-op/rejected/cancelled
edits produce none. Document switching and close/reopen isolate history and
queued gestures. Tests exercise the material service and UI command routing,
not just generic PropertyOp inversion.

Implementation and automated gate (2026-09-11): each open material document owns
its TimeMachine history, immutable source snapshots and shared commit-group
controller. Scalar and four-channel edits, previews, cancellation and history
replay use the validated authoring path under the same lock. History validation
happens before popping the stack. Saved-content identity determines dirty state;
undo/redo advance authoring revision and invalidate cooked state while preserving
the captured saved revision. Close/reload retire old history and gesture tokens.

Material controls now begin/end document-bound numeric and color sessions; wheel
samples commit after 250 ms idle. Undo/redo buttons and scoped keyboard commands
route through the material view model. Deactivation terminates pending gestures
and blocks late hidden-control edits. Save/close flush focused numeric text and
finish pending sessions before snapshot capture. Rejected values refresh from
the authoring model.

MaterialEditor tests pass 38/38, including 100-sample sessions, exact scalar and
four-channel history, no-op/rejected/cancelled edits, wheel idle, document
isolation, saved-content dirty state, save during newer edits, focused input at
close, and actual view-model undo/redo routing. MSBuild passes the material editor,
its tests and the editor app with no unsuppressed analyzer/IDE diagnostics in
changed files. This satisfies #9's implementation and automated acceptance gate;
the milestone's separate visible field/workflow qualification remains in 07A.6.

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

The rebased #2-5 fixes are landed: preserve their contracts and documented
automated evidence without treating them as proof of unrelated gaps. Preserve domain-specific sun/asset
rules when adding generic sessions. Native background work must use engine-owned
capabilities. Save conflicts must not be resolved by silently discarding edits.
There is no autosave scope expansion.

## 9. Validation Gates

- [x] 07A.0 managed capability migration, native failure forwarding and boundary tests pass (#10).
- [x] 07A.1 truthful background dispatch and live behavior pass.
- [x] 07A.2 gesture/history/selection/cancel cases pass on the existing controls.
- [x] 07A.3 scoped inline diagnostics and dependent invalidation cases pass.
- [x] 07A.4 offline/reconnect/lifetime convergence cases pass.
- [x] 07A.5 shared atomic-save and conflict cases pass (#7), preserving #4.
- [x] 07A.7 active loop observation/state diagnostics and restart tests pass (#6).
- [x] 07A.8 document-owned material history and session tests pass (#9).
- [x] 07A.6 field/workflow evidence is complete and the user has validated the
  visible behavior; native acceptance alone is not presented-state proof.

## 10. Status Ledger Hook

Record one ED-M07A validation row after its gates pass. Preserve earlier M03,
M04, M05 and other milestone records and their original evidence. M04 has no new
sweep or closure action; the missing work identified above executes here.
