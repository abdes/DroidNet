# Live Engine Sync LLD

Status: `Canonical V0.1 production contract; implementation and qualification tracked by ED-M08`

## 1. Purpose

Project committed authoring changes into the embedded engine through one
`SceneEngineSync` owner. This contract covers current-state convergence, typed
runtime transport, asset intent, cancellation and diagnostics. Engine lifecycle,
view ownership and mounting belong to [runtime-integration.md](runtime-integration.md).

Normal scene synchronization is production behavior. Saved-revision capture
isolation, parity checkpoints and qualification orchestration belong only to
opt-in development adapters; they are not production services or dependencies.

## 2. PRD Traceability

| IDs | Required result |
| --- | --- |
| GOAL-003, REQ-008/026 | Embedded preview reflects committed supported authoring. |
| GOAL-006, REQ-022/024 | Failures retain precise operation/target diagnostics. |
| SUCCESS-003 | The current scene converges after edits, publication and restart. |

## 3. Architecture Links

- [property-pipeline.md](property-pipeline.md): edit revisions and gesture ownership.
- [property-inspector.md](property-inspector.md): node, geometry, camera and light fields.
- [environment-authoring.md](environment-authoring.md): canonical environment/role semantics.
- [material-editor.md](material-editor.md): source versus published material state.
- [content-pipeline.md](content-pipeline.md): slot identity, publication and native readers.
- [standalone-runtime-validation.md](standalone-runtime-validation.md): development-only capture ownership.

## 4. Existing Integration Points

Reuse `ISceneEngineSync`, `SyncOutcome`, document metadata registrations,
`SceneSyncRevision`, the property replay/coalescing path and Runtime-owned world
commands. Native `SceneAssetRequests` owns scene sessions, request generations,
completion inboxes and scene-mutation acceptance. Extend these mechanisms for
stable slot IDs and canonical camera/flag/light payloads; do not create competing
counters in an inspector or a second synchronization orchestrator.

Current index-based slot transport and old sun/camera/boolean flag payloads are
migration targets. The canonical runtime consumes only the replacement contract
once useful content is migrated. Existing evidence is listed in section 16;
source/API presence alone is not proof of the newly required rendered effects.

## 5. Delivery Model

```text
Command validates -> commits authored values/revision/history
  -> SceneEngineSync captures immutable target and intent
  -> Runtime world capability validates target and queues native mutation
  -> native completion is accepted only for the current target/generation
  -> current readiness/diagnostics update; newer authoring remains authoritative
```

Invariants:

1. Sync follows the committed authoring revision and never rolls it back.
2. No running engine produces a classified skipped/pending state; the adapter
   does not start it merely to deliver an edit.
3. Acceptance means a command was accepted/queued, not loaded assets or a
   presented frame. Completion/readiness and rendering evidence remain distinct.
4. A failed required capability is visible and fails qualification; `Unsupported`
   is a diagnostic state, not permission to ship an inert editable field.
5. Full projection and later mutations share one document/runtime lifetime and
   revision order. A stale completion cannot overwrite the latest intent.
6. Source saving/cooking/mounting and production view navigation remain with
   their normal owners; sync does not perform those operations itself.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| Scene document commands | Validate/mutate source, history and immutable revision-stamped delivery. |
| SceneEngineSync | Current-state projection, ordering, coalescing and classified results. |
| Runtime world capabilities | Engine-run/scene-target validation and managed DTO transport. |
| Interop | Narrow native dispatch; no authoring policy or independent asset state. |
| Native scene/content/Vortex | Asset readiness, mutation-phase application and actual rendering. |

Feature code uses Runtime-owned capabilities rather than obtaining an
`OxygenWorld` facade. Production sync has no import/cook/mount or development
qualification dependency.

## 7. Data Contracts

### 7.1 Sync outcome

`SyncOutcome` carries status, operation kind, affected project/scene/node/asset
scope, code/message and optional technical exception. Existing statuses are
Accepted, SkippedNotRunning, Unsupported, Rejected, Failed and Cancelled.

Expected contention, missing resources and unsupported capabilities are classified
results rather than repeated first-chance exception loops. Authoring success and
preview success are separate; a preview failure cannot undo a valid scene edit.

### 7.2 Environment results

`EnvironmentSyncResult` retains each field's outcome and an aggregate outcome.
Background has its own scoped delivery/result. Accepting some fields cannot
turn a missing or rejected field into Accepted. Native observations identify
their actual scene/run/lifetime, and do not themselves prove presentation.

### 7.3 Immutable intent and target

Every payload contains its `RuntimeSceneTarget` (project, run, scene, document
lifetime and activation), authoring revision/preview sequence, component identity
and immutable changed values. APIs use the same classified result path for full
and granular delivery. Remove retired overload/adapter routes after callers are
migrated; do not retain a second path for backward compatibility.

Material intent additionally identifies GeometryUri, MaterialSlotId and expected
current layout revision. Runtime indices are resolved against accepted geometry
inside the native capability; they are never persisted or supplied as authored
identity. See section 8.2.

## 8. Adapter Mapping

### 8.1 Canonical payloads

| Operation | Required runtime behavior |
| --- | --- |
| Transform / reparent | Apply immutable local TRS and explicit reparent policy; resolve affected descendants and invalidate render/light products. Reject unrepresentable requested transforms before authoring mutation. |
| Scene Visibility | Preserve Inherit/Shown/Hidden source mode. Geometry and light eligibility use the resolved native flag; a locally Shown child can override a hidden parent. |
| Geometry Cast / Receive Shadows | Preserve each independent Inherit/On/Off flag. Cast controls opaque/masked occluder submission; Receive controls actual direct-light shadow attenuation, not AO or Unlit shading. |
| Geometry attach/change/remove | Dispatch canonical geometry identity and retire obsolete request/slot intent; obtain the accepted inventory before applying material overrides. |
| Material slot assign/clear | Apply the named SlotId's current native bindings; clear restores that slot's mesh default on every declared LOD/submesh binding. |
| Perspective camera | Carry Auto/Fixed mode, stored Fixed ratio, vertical FOV, valid clipping and parented pose. Auto derives ratio per target; Fixed fits the complete image with bars. Resizing never edits source/history. |
| Directional light | Carry runtime affects-world, light shadowing and supported values separately from AtmosphereLightSlot None/Primary/Secondary. Both atmospheric roles have full independent consumers; None still permits ordinary directional illumination. |
| Atmosphere role edit | Validate unique stored slot occupancy across hidden/off sources; reject conflicts. No promotion, brightest/first-light selection or coupled Sun pointer/boolean representation. |
| Scene environment | Carry exactly the canonical atmosphere, captured-sky lighting, exposure and appearance fields from the owning field tables; invalidate products affected by either atmospheric source. |
| Background | Preserve its defined display-colour semantics and translucent foreground composition. |
| Editor Hide | Editing-view representation mask only, retaining lighting and caster eligibility. It never writes source flags or triggers a cook. |

Physical-camera inputs, general simulation activation and authored hidden-shadow
modes are not introduced by these mappings. An explicitly selected camera remains
usable when its node/representation is hidden. Runtime presence is derived;
`IsActive` is not saved authored activation.

### 8.2 Material-slot V0.1 behavior

The canonical slot and reimport contract is
[ContentPipeline section 20](content-pipeline.md#20-canonical-material-slot-identity-and-reimport).
Every existing slot, including nonzero slots and its declared LOD bindings, uses
one identity-based transport. Remove the old first-slot/LOD-0-only route.

Normalize a material source URI to the corresponding native `.omat` virtual
path before dispatch. A null assignment clears the instance override; it does
not substitute another material. Retired empty-URI sentinels do not form another
shipping execution path.

Native requests are keyed by scene session, node, accepted geometry identity and
SlotId. Each assignment/clear advances intent generation before any procedural,
cache or asynchronous lookup. Geometry replacement has its own generation. A
callback is accepted only when node, geometry generation, SlotId, current layout
and material intent still match. Fresh inventories can carry an established
SlotId into a changed layout; a numeric position or matching label cannot.

Callbacks enqueue immutable results through a weak inbox. They do not retain
scene nodes, editor contexts or modules and do not mutate the scene directly.
During SceneMutation, queued authoring commands execute before completion
acceptance. Deleted descendants/dead handles and obsolete failures are discarded.
Detach retires the renderable's pending geometry and all slot intents. Scene
replacement/shutdown retires its entire inbox.

Material results wait for transient geometry readiness. A missing geometry/slot
while replacement is still loading is not a permanent structural rejection.
Once geometry is accepted, resolve SlotId using its verified inventory. A missing
identity produces repair-required status and retains authored intent; it is not
retried endlessly or applied to another surface. Resolution/load errors retain
node/asset/slot context and recover through existing content-demand/publication
paths.

Clear records explicit removal intent and immediately removes an applicable
visible override even while newer geometry loads. Older pending loads cannot
restore it. Undo/Redo sends the same generation-checked commands. On publication,
only identity-compatible current intents are reapplied; incompatible layouts are
blocked by the publication contract before they can break saved scene consumers.

### 8.3 Component removal

After source removal commits, issue the matching detach command. Transform is
fundamental and cannot be removed. Removing a node retires descendants and all
associated geometry/material/light/camera requests. Unsupported component types
do not enter the canonical V0.1 source through silent best-effort projection.

### 8.4 Gesture coalescing

The shared edit-session controller owns one history entry per committed gesture.
While editing, committed sub-values may be delivered at a 16 ms minimum preview
interval. Commit cancels any remaining preview timer and sends one terminal
current value; cancellation restores the pre-gesture value. Wheel bursts use the
shared 250 ms idle commit. Do not maintain a parallel history in sync.

### 8.5 Runtime-state classification

NoEngine, Initializing, Ready, Starting, ShuttingDown and Faulted produce a scoped
SkippedNotRunning/pending result. Running permits dispatch after target checks.
A newer run/activation rejects old payloads and performs one coherent projection
of the current open scene. Authoring remains available when preview is offline.

### 8.6 Cancellation

Chain delivery cancellation to the originating document/scene lifetime. Observe
actual native completion/drain where resources are in flight; a cancelled managed
wait does not prove those resources are free. Do not retry obsolete work or cancel
an unrelated scene/process. Current authoring remains available for later
convergence.

## 9. UI Surfaces

Sync has no standalone panel. Consumers show current inline authoring errors,
scoped pending/preview failures and technical operation details. Missing material
or slot identity is visible on the corresponding slot row. Success does not add
banner noise, and stale failures cannot replace current field state.

## 10. Persistence

Sync does not save authoring or persistent demo/view state. Commands and document
services own source. Native target generations, runtime indices and loaded
objects are session state; editor Hide is separate workspace state owned by the
view/workspace service.

## 11. Sync / Cook / Runtime Behavior

The adapter translates identities but does not cook, mount, or invent physical
cooked paths. Existing asset-demand services submit saved dependencies to the
shared coordinator. Published material values remain on screen while their
source has unsaved/newer changes. Successful validated publication refreshes the
current scene through the same projection and generation authority.

## 12. Operation Results And Diagnostics

| Sync result | Authoring/result consequence |
| --- | --- |
| Accepted | No routine success result; queue acceptance only. |
| SkippedNotRunning | Authoring succeeds; preview unavailable/pending warning. |
| Unsupported | Capability failure; authoring retained, required-field qualification fails. |
| Rejected / Failed | Authoring retained; scoped partial preview failure with actual reason. |
| Cancelled | That delivery stops; current intent remains authoritative where the document is live. |

Diagnostics carry scene/document lifetime, node/component identity, asset URI,
SlotId and expected/actual geometry layout where relevant. Preserve native codes
and context. Unresolved slot repair, missing asset, stale completion, incompatible
schema and device/runtime failure are different conditions.

## 13. Dependency Rules

Commands call SceneEngineSync; SceneEngineSync uses Runtime-owned world
capabilities; only Runtime/Interop dispatch native calls. Inspector VMs do not
call native facades or independently drive sync. No mutation of authored values,
source persistence, cooking or mounting occurs inside the sync adapter.

## 14. Validation Gates

- Exercise accepted, offline, rejected, failed and cancelled delivery without
  outward NotImplementedException or contention exception floods.
- Verify Local/Inherit propagation, local overrides beneath hidden parents,
  reparenting, cached-light invalidation and actual caster/receiver effects.
- Verify camera Auto/Fixed fitting and exact selected/parented camera behavior.
- Verify None/Primary/Secondary, secondary-only and two-source illumination,
  role conflicts, hidden/off sources and dependent sky-product invalidation.
- Verify every material slot, per-LOD bindings, clear/Undo/Redo, delayed cache/
  procedural/async completion, geometry replacement and repair-required layouts.
- Verify newer edits and document close/replacement defeat stale callbacks;
  restart and publication converge on current authoring without history changes.
- Native-engine effects are validated before real editor workflows. CPU state,
  successful enqueue and native asset loading do not substitute for rendered proof.

## 15. Development Capture Integration Boundary

The opt-in development host may adapt existing production entry points to hold
later deliveries while rendering a pinned saved revision. Its admission hooks,
qualification state and capture scheduling compile only into development targets.
Normal SceneEngineSync builds do not contain a qualification hold service,
protocol dependency, hidden command or always-shipping validation state.

The development adapter must cover scalar edits, topology, asset completion and
publication delivery, release after capture/readback ownership drains, and restore
one coherent latest scene rather than replaying an obsolete snapshot. Source
history and dirty state remain unchanged. See
[standalone validation section 5](standalone-runtime-validation.md#5-ownership-build-isolation-and-dependency-direction)
and [section 8.4](standalone-runtime-validation.md#84-saved-revision-embedded-capture-session).

## 16. Historical Evidence

Earlier request-generation and replay validation remains at its recorded scope
in [issue 5](../plan/issue-005-asset-request-generations.md),
[ED-M07A field workflows](../validation/ED-M07A-field-workflows.md),
[workspace publication](../validation/ED-M07B-workspace-publication.md) and
[material recovery](../validation/material-sidedness-and-recovery.md).
The milestone ledger owns completion status for the canonical changes above.
