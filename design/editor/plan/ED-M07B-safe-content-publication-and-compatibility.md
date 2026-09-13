# ED-M07B - Safe Content Publication And Compatibility

Status: `in_progress; workflow review and D1 complete; implementation underway`

## 1. Purpose

Make supported content discoverable and usable before and after cooking, with
clear progress, freshness, and recovery. Close descriptor, cook/publication,
import and matched-build gaps before standalone parity. Preserve ED-M07's
completed delivery record; this milestone owns the additional work and evidence.

Users should be able to import or create content, find it in one consistent
browser, assign it through typed pickers, understand what the viewport is using,
and update it without managing generated files or mounts. The
[UI review](../validation/ED-M07B-ux-review.md) and
[workflow LLD](../lld/content-cooking-workflows.md) define the concrete changes.

## 2. PRD Traceability

`REQ-013` through `REQ-024`, `REQ-026`, `REQ-036` through `REQ-042`;
`SUCCESS-004`, `SUCCESS-006`, `SUCCESS-007`.

## 3. Required LLDs

[content-pipeline.md](../lld/content-pipeline.md) sections 16-19,
[content-cooking-workflows.md](../lld/content-cooking-workflows.md),
[cooking-panel.md](../lld/cooking-panel.md),
[content-browser-asset-identity.md](../lld/content-browser-asset-identity.md),
[material-editor.md](../lld/material-editor.md),
[property-inspector.md](../lld/property-inspector.md) section 9.1,
[runtime-integration.md](../lld/runtime-integration.md),
[environment-authoring.md](../lld/environment-authoring.md),
[asset-primitives.md](../lld/asset-primitives.md),
[project-services.md](../lld/project-services.md),
[project-layout-and-templates.md](../lld/project-layout-and-templates.md),
[diagnostics-operation-results.md](../lld/diagnostics-operation-results.md).

## 4. Identified Gaps

| Evidence in the committed source | Missing behavior | Task |
| --- | --- | --- |
| `SceneDescriptorGenerator.CreateEnvironment` produces only NativeSkyAtmosphereEnvironment; earlier code emits warnings for non-default exposure/tone/background. | Required PostProcess/Background values survive cooking and native loading, rather than being omitted. | 07B.3 |
| `ContentImportManifestBuilder` writes directly to GetCookedMountRoot; ContentCookScope/Result have no input revision/hash or publication transaction. | Coherent saved input, private staging, safe fixed-root replacement and provenance. | 07B.1/2 |
| Runtime mount contract is UnmountProjectCookedRoot followed by MountProjectCookedRoot. | Pause/drain, all-root rollback and interrupted-publication recovery. | 07B.2 |
| ProjectCookScopeProvider derives project/root/output facts; existing Content Browser exposes Cook Asset/Folder/Project. | Define automatic versus explicit triggers, incremental reuse, and visible scope through existing surfaces. | 07B.0/1/5 |
| AssetIdentityReducer uses file timestamps; opening M_ShinyRed shows NotCooked while its browser row shows COOK. | One dependency/publication status authority across browser, document, picker, and viewport. | 07B.1/5a |
| Running-editor review: Materials breadcrumb can retain geometry tiles; Filter has no effect; selected rows expose no details. | Correct navigation/query lifetime, working filters, actionable state and source/output details. | 07B.5b |
| Running-editor review: built-ins and Engine_Generated_BasicShapes outputs appear as separate picker choices; material Cook clips at the current dock width. | Provenance-aware grouping, consistent typed picking, and usable command layouts. | 07B.5c/e/7 |
| Import UI permits all file types and source copying uses overwrite; Inspect reports only counts/path. | Safe and understandable import decisions, useful result inspection and recovery. | 07B.4/5d/e |
| User report and source: the node inspector reserves a 2*:3* header/property split; component selection only controls deletion, and multi-node mode hides the list. | Content-sized compact header/list, functional component filtering and All reset, efficient single/multi-node layouts. | 07B.5g |
| Native discovery locates installed tooling; no qualified artifact-set fingerprint is established by the existing design. | Detect mismatched editor/native/cooker/schema artifacts before unsafe calls. | 07B.4 |
| [#8](https://github.com/abdes/DroidNet/issues/8): ContentPipelineProcessRunner cancels stream reads/WaitForExitAsync without terminating the child; ImportToolContentPipelineApi cleans the manifest in finally. | Owned worker termination, descendant handling, reader drain and cleanup ordering. | 07B.6 |
| [#11](https://github.com/abdes/DroidNet/issues/11): SetGeometryCommand constructs built-ins/default material and pak geometry fields, while ProceduralGeometryDescriptorService separately defines generator parameters/bounds/defaults and supports only Cube/Sphere/Plane. The UI exposes eight built-ins. | One engine/content authority for all exposed procedural geometry and live/cooked semantics. | 07B.7 |

## 5. Scope And Non-Scope

Implement the content-pipeline LLD's saved-input and journaled publication
transaction, dependency-aware incremental cooking, required descriptor mapping,
qualified static/scalar import, reproduction, matched-build preflight, and the
complete browser/picker/material/cook workflows in the workflow LLD. Automatic
triggers and a session pause control follow the accepted D1 policy.
No generic project-settings panel, renderer-preset selector, dedicated recook-
stale scheduler, descriptor/manifest editor, autosave, multi-viewport support,
new drag/drop placement system, or standalone parity claim belongs here. Request
coalescing belongs to the existing project coordinator. Published paths stay fixed.

## 6. Implementation Sequence

### 07B.0 - Review And Settle The User Workflow

Review the running browser, authored/engine-provided/cooked content, both typed
pickers, material editing, and cook/import entry points. Record screenshots and
source-backed gaps. Workflow LLD decision D1 was accepted on 2026-09-11 with the
instruction to proceed with revised M07B; its PRD/LLD clauses are reconciled.
The policy cooks incrementally after Import/Save and when content is
needed for assignment/active-scene preview, with explicit broad scope actions and
session pause. Browsing and transient edits never cook; external reimport stays
explicit. Source saves remain explicit, independent operations.

Pass: before/during/after states, trigger table, dirty-input handling, recovery,
and narrow-pane/keyboard behavior are reviewable and the product decision is
recorded. This review/contract gate is complete; implementation gates follow.

### 07B.1 - Saved Dependency Snapshot And Single Cook Writer

Saved scene/scalar-material/static-geometry discovery and private input capture
are implemented for explicit scopes, including geometry buffers and optional
import settings. Material helpers use the same writer and saved snapshot.
Incremental product provenance now persists source/dependency fingerprints and
validated descriptor/index/resource hashes. Current repeat requests start no
native workers and leave cooked files unchanged. A changed scalar material
rebuilds its own product; current scene/geometry descriptors are reused. A
same-size/timestamp descriptor corruption is detected and repaired, failed cooks
do not advance provenance, and warnings remain attached to reused products.
All engine built-ins retain source identities through repeated scene/project
cooks. Saved-source scheduling now coalesces revisions, resumes scopes blocked by
saved documents, and supports session pause without blocking explicit cooks.
Consumer completion is awaited before the shared writer is released.
ContentPipeline 166/166 passes. Successful changed material/scene saves now
notify the shared scheduler; unchanged saves can resume blocked scopes without
queuing another cook. Explicit and preview-demand requests now precede waiting
background saves without preempting an active writer. Pause applies to queued
automatic work and resets with the project lifetime. Equivalent pending scopes
share one run, with independent caller cancellation and promotion when an
explicit request joins. Running captures stay immutable; later requests queue
separately. ContentPipeline 287/287 passes, including shared native publication.
Assignment and scene activation now submit saved geometry/material demand through
the shared transaction. Reference, document and selection changes retire obsolete
observers; those notifications never launch cooks. Imported-source closure,
Import/creation triggers and complete before/after preview journeys remain open.

Route every cook entry point, including material helpers, through one project
coordinator. Reject dirty participating documents, capture/hash saved inputs and
import settings under coordinated reads, and pass snapshot paths to native jobs.
Serialize overlapping requests and scope callbacks to project lifetime.

Implement the workflow LLD section 5 incremental planner: persisted dependency
fingerprints and validated-output reuse, missing/corrupt product invalidation,
changed-subasset reconciliation, request coalescing and caller cancellation.
Distinguish an asset's inputs from dirty consuming scenes. Carry trigger, scope,
changed/reused counts, and captured/current state through the shared operation
contract. Wire only the triggers selected in 07B.0; automatic requests cannot
bypass snapshot validation or publish directly.

Pass: a later edit does not change captured bytes or clear dirty state; a queued
cook captures only after its gate; source mutation during discovery retries or
fails visibly; cancelled/closed-project work cannot publish. Repeating a current
cook performs no native worker, output rewrite, or preview pause. One changed
dependency rebuilds only affected products in the requested closure. Queued saves
coalesce to the latest saved revision; cancelled/failed work does not self-retry.

### 07B.2 - Staging, Preview Pause, Publication And Recovery

Private-root seeding and cross-process output leases are implemented. Tests cover
preserved unrelated bytes/timestamps, failed seeding, competing readers/writers,
runtime-reader handoff, and external reader exit/termination. The combined
publication/staging/lease/worker suite passes 69/69. The transaction core restores
multi-root failures and persisted interruption states, including metadata writes
and failed rollback mounts. The shared pipeline now stages native output and
publishes receipt/provenance transactionally. Recovery distinguishes live and
abandoned operations, and mounting holds a reader through generation verification.
ContentPipeline 221/221 passes, including native cook plus failed preview,
startup handoff, and corrupt-receipt repair. Fifteen actual publisher-termination
cases verify recovery at prepared, root-move, metadata-write and committed
boundaries. The combined process/worker/lease suite passes 39/39. Runtime awaits
the public asset-loader drain, retains output readers through native refresh and
teardown, and replaces existing material bindings without reloading the scene.
Runtime 90/90 and focused packaged UI 14/14 pass. The user confirmed recook/tab
switching and leak-free shutdown under native debugging. Workspace publication
now uses this awaited boundary. Project-lifetime, recovery and complete user workflows
still require their remaining integration checks.

Implement content-pipeline section 16 exactly: same-volume private output,
whole-root validation, preserved unrelated entries for partial cooks, durable
journal/backups, preview pause/read drain, all-affected-root replacement, remount,
metadata/catalog commit, and rollback/recovery. Runtime/output leases prevent
standalone readers or late requests from observing a partially replaced set.

Apply the workflow LLD's common completion path to Save-triggered, import,
demand, explicit, retry and Save listed & Cook requests. Replace UI-owned
fire-and-forget mount refresh with an awaited publication/runtime boundary.
Refreshing a source must refresh its existing live bindings; clearing native
caches alone leaves the scene's old material/geometry objects in use. Preserve
unaffected roots, current reference intent, scene identity, selection and camera.
The material-editor-only catalog callback is not a publication mechanism.

Pass: inject failure/interruption at every journaled boundary, first publication,
partial asset recook, cancellation, native mount and rollback mount. Prior output
is intact/restored or recoverably retained with preview visibly unavailable.
No failed generation is reported Mounted/current. Authoring remains responsive.
Save and recook a shared material and observe its new values on all current uses
without scene reload. Repeat through every trigger, with a pending load, newer
assignment, removed node, closed project, failed cook and no-op cook.

### 07B.3 - Complete Native Descriptor/Load Mappings

Extend the engine-owned scene descriptor and native import/load contracts for
every editable PostProcess field and LDR BackgroundColor. Carry authored values
and enum ordinals through SceneDescriptorGenerator and native scene-system
initialization. Existing sky-atmosphere and sun/material mappings remain intact.
Do not write guessed fields or editor-owned binary records. Missing required
mapping is an error before publication, never an unsupported-field warning pass.

Use scene descriptor version 4 for the complete post-process record and the
display-background record. The PAK container remains version 7. Native readers
reject older scene descriptors with a recook error under the engine's latest-only
policy; generators, schemas and scene fixtures advance together.

Pass: non-default values for every environment/post-process field survive saved
JSON -> generated descriptor -> cook -> native load observation with units and
ordinals preserved. Include all tone/exposure modes, background with atmosphere
off, and camera/light/material references. Visual equivalence is M08's gate.

### 07B.4 - Matched Artifact Preflight And Reproducible Import

Decision D2, revised by the user on 2026-09-12: remove qualification manifests,
promotion commands and the separately built startup probe. Normal development
builds changed projects and runs the editor. Existing test projects own regression
coverage; tests are never a startup prerequisite.

The ordinary Interop build embeds its native SDK binary hashes and tracks SDK
headers/import libraries as compile inputs. Startup reads the receipt as managed
metadata, checks the installed SDK, and retains native files through session
cleanup. Managed/UI changes do not invalidate this receipt. An SDK change requires
only the normal Interop rebuild. Cooking independently checks its tool/schema
inputs and retains their current producer hashes through worker termination and
reader drain. Project Browser and safe saves remain available on mismatch.

Implement the supported glTF/FBX static/scalar validation and retained import
settings from pipeline section 17. Clean-copy reproduction remains open.

Route Import/Reimport through the same snapshot/publication coordinator. Retain
source media and settings before derived processing; expose destination and
supported formats, handle collisions explicitly, and protect dirty authored
assets. Report retained-source success separately from failed cooking so Retry
does not require reselecting/copying the source.

Pass: wrong/missing artifact or schema fails safely; small unit/axis/handedness
fixtures import consistently; unsupported animated/skinned/texture-bearing
qualified imports fail before publishing without destroying sources. Delete
derived data from a copied project and regenerate identical logical asset
identities and semantically equivalent descriptors/loaded values.

### 07B.5 - Content Discovery, Use, Cooking And Recovery

Implement [content-cooking-workflows.md](../lld/content-cooking-workflows.md)
through the following independently reviewable tasks. Establish the shared
request/state contracts alongside 07B.1, then complete the visible workflows
against the real coordinator and runtime; UI integration is not a final badge pass.

#### 07B.5a - Shared Asset Status

Consume provenance/freshness/publication from ContentPipeline and readiness from
Runtime in browser, material editor, and typed pickers. Separate unsaved source,
Needs cooking, Out of date, queued/active work, prior usable output, and native
unavailability. A new uncooked asset is not a broken asset. Opening a material
must not reset its known cook state. Keep technical identifiers in details.

Pass: the same identity has consistent facts on all surfaces before/after save,
cook, reopen, failure, and native activation; no timestamp-only Ready or false
NotCooked state. A newer edit remains visibly unsaved/out of date.

#### 07B.5b - Browser Navigation And Before/After Presentation

Fix folder/query lifetime and list/tile state so breadcrumb, rows, selection,
and action scope agree. Provide working Type/Status filters and useful empty
states. Keep one logical row per asset with source/output details and contextual
Cook/Open/Inspect actions. Show built-ins clearly; Cooked is a read-only derived
view with source links and technical output inspection.

Pass: repeat the recorded navigation/view-switch defect, Back/Forward, rapid
query changes, and publication refresh. Selection/focus stay scoped; unrelated
or superseded results cannot change the target of a cook. Uncooked, stale,
missing, and cooked-only content have clear, distinct actions.

#### 07B.5c - Typed Picking And Runtime Use

Make geometry/material pickers consume the same identity/status projections.
Include valid saved uncooked content and explain pending/previous runtime output.
Apply the selected D1 demand behavior through the coordinator. Use 07B.7 mapping
to group proven generated companions with their built-in origin; preserve
independent assets and existing authored identities. Retain None/Default semantics.

Pass: assign before first cook, use already cooked content, update a shared
material, and preserve assignment/history through cancellation, Undo/Redo,
Save/reopen, and scene switches. Completion cannot revive an undone assignment.
All current uses refresh without restarting or manual mount commands.

#### 07B.5d - Import, Save And Explicit Cook Entry Points

Complete the visible import/reimport destination, format, collision and retained
source/result flow from 07B.4. All four explicit Cook scopes use incremental
planning and explain the actual affected scope. Handle dirty inputs with the
ordinary save/conflict workflow and the D1 decision on Save listed and Cook.
Route all scopes to the Cooking panel; dirty participating documents and the
explicit Save listed & Cook action remain inline with the selected request.
Wire the approved automatic triggers and session pause/resume without implicit
saves or independent writers. Automatic failures remain actionable and quiet
successes do not steal focus.

Pass: import/create/save/use is a continuous workflow; unchanged cooks are no-ops;
dirty dependencies name the blocking documents; import/cook failure can be retried
from retained source. Inspect/Validate and browse/hover do not trigger cooking.

#### 07B.5e - Progress, Recovery And Accessible Layout

Implement the [Cooking panel](../lld/cooking-panel.md) using the existing docking,
coordinator, document commands, operation results, and logging infrastructure. Its
layout and interactions were approved on 2026-09-12 for all four scopes,
including entire projects. Default docking is at the bottom beside
Content Browser and Logs. Use a compact run list and one selected run's details,
with asset-grouped issues beneath stage/counts, an initially expanded Output
section for that run's live and completed messages, and an expandable asset list.
Keep native/managed progress and technical output inside Cooking. Reuse logging
capture/storage, but do not send users to the global Logs panel to understand a
cook. Preserve the reading position while messages arrive and retain transcripts
with session history, independently of unrelated editor log traffic.
Use a single-line name/type, status pill, and adjacent recovery actions. Keep
routine updated/reused counts and publication confirmations in Assets/Output;
extra status text is reserved for actionable exceptions such as warnings or
preview unavailability.
Hide Retry entirely after success or an already-current result. The expanded
asset list keeps each status icon beside the asset name and type, with accessible
state labels, rather than requiring users to scan a remote status column.
Apply the same status icons and name/type hierarchy to the cook list. Omit
routine Explicit labels and repeated status badges; keep explanations and
applicable actions in the selected run's details.
Use DroidNet's compact toolbar above the side list for Show all. Remove the
panel-wide project-name toolbar and new-cook commands; initial cook requests
remain in the existing editor entry points. Native Expanders label issue groups
by asset and count, with InfoBars keeping messages and recovery actions inline
when space permits. The details body scrolls, and expanded Output/Assets retain
minimum content heights of 160/120 DIP instead of being clipped in short docks.
Both sections expand to fit their full content and have no internal scrollbars;
only the surrounding details body scrolls.

Provide session history, Show all for routine automatic successes, quiet automatic
work, selected-run cancellation, inline unsaved inputs and Save listed & Cook,
latest-saved-scope Retry, and Go to property that keeps Cooking visible and the
failed run selected. Continue independent batch work after asset failures, skip
dependents, collect issues, and preserve published output on failed cooks. These
behaviors use cook-specific observable run state, not a second scheduler.

The first recovery gate is Main's negative Aerial Start: show the actual property
error and captured value, focus the field, apply the native schema's finite
minimum-0 bound through shared editor/preflight validation, then save 100 and
publish successfully. Preserve invalid saved values visibly on load and previous
output on failure; never clear stale status merely because the request ended.

Inspect provides browsable read-only output facts. Layouts must keep
Cook/Save/Cancel/status usable at supported dock widths and scaling, with
meaningful names, text beyond color, proper asset glyphs, and stable focus.

Pass: first-cook failure, failure with prior output, rollback failure, offline
runtime, and mismatch each have the right recovery action. The material toolbar
does not clip. Keyboard and 100%/150%/200% scale walkthroughs pass; controls that
are unsupported have a visible reason or are absent.

#### 07B.5f - End-To-End Workflow Qualification

Execute every journey in workflow LLD section 7 with production services and
the packaged editor controls. Repeat the captured review cases after fixes and
record user validation of before/during/after behavior. Cover supported imports,
the complete engine generator set, authored and cooked-only content, dirty/shared dependencies,
coalesced requests, failures, cancellation, and offline/reopen behavior. Include
the Cooking panel's Main repair journey and every additional gate in its section 6;
a review wireframe is not implementation or editor-validation evidence.

The earlier report that Project cooking required cooking Materials first is no
longer an active defect or completion blocker, as confirmed by the user. Retain
the normal first-project-cook dependency regression test.

Pass: users complete supported workflows without generated-file edits, raw path
assignment, manual mounting, or losing their working context. The PRD 1,000-entry
browser and operation-feedback timing gates apply. Runtime visual parity remains
ED-M08; usability and correct state transitions are required in ED-M07B.

#### 07B.5g - Compact Node Inspector And Component Filtering

Implement property-inspector section 9.1. Replace the proportional header/list
allocation with content-sized, bounded component selection and give properties
the remaining height. Remove the empty list allocation in multi-node mode and
compact the node header actions and absent-control space. Preserve the existing
property-editor layout and spacing; use full-text tooltips for clipped text.
The component selector filters the property sections; no component selected means All components.
Provide a clear, keyboard-accessible reset and consistent behavior across node
selection changes. Multi-node filters respect existing applicability/mixed values.

The compact selector, All icon, type filtering, removal targets and original
property layouts are implemented. Hidden sections retain field errors and pending
validation results; affected component entries show an error icon and full-text
feedback. Recycled section controls detach from their models. Packaged UI passes
196/196 at 175% scaling. Numeric text, drag cancellation, late picker completion,
invalid input and mixed-target source corrections have filter-boundary coverage.
The separate 100%/150%/200% walkthroughs are still open.

Pass: selecting Geometry shows only its editor; selecting Transform switches
sections; clearing selection/All restores all applicable editors. Two components
occupy two compact rows rather than a fraction of the dock. Single/multi-node,
short/narrow docks, DPI, add/remove, no-node selection, active edit cancellation,
and selection lifetime cases pass without extra history, dirty state, or cooks.
Include these cases in 07B.5f; keep M07A property/history semantics intact.

### 07B.6 - Own And Drain Native Worker Processes (#8)

Worker/manifest lifetime implementation is validated: 19 new subprocess/lifetime
cases pass in the 64/64 ContentPipeline suite. Evidence:
`artifacts/m07b-worker-final-tests.log` and
`artifacts/TestResults/m07b-worker-final`. Changed worker/adapter/probe/test files
have no unsuppressed analyzer or IDE diagnostics. Staged publication integration
is exercised with 07B.1/2.

Preserve structured ArgumentList and concurrent stdout/stderr reads. Run native
cook workers in an operation-owned Windows job with descendant ownership and
kill-on-close containment. On cancellation, stop the launched worker/job (the
current CLI has no cooperative cancel protocol), await process-tree termination
and observe both reader tasks before deleting the manifest/staging or releasing
the operation. Treat exit/cancel races by the actual terminal result. A terminate
failure is explicit and retains operation ownership/inputs; successful Cancelled
cannot be returned while writes continue. Never terminate the embedded engine
or unrelated user processes. See content-pipeline section 18.

Pass: a controlled writer child and descendant, large stdout/stderr, immediate
exit, startup failure, cancel/exit races and termination failure are exercised.
No output writes occur after successful cancellation; reader failures are observed,
manifest cleanup follows termination, and published output is unchanged.

### 07B.7 - One Procedural Content Authority (#11)

Add an engine/content-owned procedural definition/resolution capability used by
both immediate preview and cooker descriptor generation. It owns generator
parameters, computed bounds, default material semantics and deterministic identity
mapping. Runtime/Interop transports identity/requests and results; remove its
built-in policy switch, generated-content cache policy and hardcoded pak fields.
Managed descriptor generation consumes the same engine definition rather than
maintaining independent parameters/bounds/material constants.

Cover all named generators in the current engine API/schema: Cube,
SubdividedCube, Sphere, IcoSphere, GeodesicSphere, Plane, Cylinder, Cone, Quad,
Torus and ArrowGizmo. GeodesicSphere is an IcoSphere alias; support both identities
and explain their relationship in discovery. Add the missing capabilities to
the picker through the shared engine catalog. Preserve existing URIs and immediate
preview without requiring an explicit cook first. An authored ArrowGizmo asset is geometry;
editor-only gizmo overlays remain transient. Preserve #5 request-generation and
scene-mutation acceptance for all cached/procedural/async paths.

Decision D3 (2026-09-13): retain the last valid engine-provided catalog as a
derived authoring cache. If the SDK cannot provide its catalog, browser and
picker choices use that snapshot with a clear last-known catalog and
"Preview unavailable" notice. If no valid cached catalog exists, explain the
unavailability and leave engine choices empty. A successful SDK catalog refresh
replaces the snapshot and clears the notice. Cooking continues to require the
current native catalog; cached discovery metadata cannot establish compatibility.

Pass: every listed shape previews, saves/reopens, cooks and loads. Compare identity,
bounds, topology/index counts, vertex semantics, default material and authored
overrides through supported APIs; use the same parameter cases for live and
cooked output. No pak format constants remain in interop procedural construction.
Source/unit tests are separate from the ED-M08 visual/loaded parity evidence.

Required reported regression: select Cylinder, Save, then Cook Current Scene and
Cook Project. Neither may reject this supported generator. Repeat across the
complete set and aliases with default/assigned material and Save/reopen. Audit
factory versus importer defaults (including Cylinder/Cone segment counts) and
make the engine's shared recipe authoritative. Unknown generators still fail
with the exact saved scene/node/geometry scope and an actionable diagnostic.
The report names New Entity 5 although the user describes editing New Entity 6;
check captured node ID/revision rather than assuming either name is wrong.

Expose origin/recipe/version and stable source-to-cooked identity mapping to
07B.5 consumers. This prevents generated cook companions from appearing as
unexplained extra authored shapes/default materials. The UI must not infer that
relationship from a name prefix or silently rewrite existing references.

### Integration And Commit Order

Finish 07B.0 before coding. Establish matched-artifact preflight and owned-worker
lifetime before enabling new native work. Build 07B.1 provenance/incremental
contracts with 07B.5a state projections, then the remaining 07B.3/4/7 native/import
contracts and 07B.2 publication transaction. Integrate 07B.5b-e as those contracts
become available and complete 07B.5g inspector behavior; 07B.5f closes the combined
workflow. Keep each numbered task
or cohesive contract change separately reviewable in commits. Use Fixes #8 and
Fixes #11 only when their complete behavior and validation are delivered.

## 7. Project/File Touch Points

- `ContentPipeline/src`: ContentPipelineService, ContentImportManifestBuilder,
  SceneDescriptorGenerator, tool locator/adapter, input/result contracts, new
  snapshot/journal/publication coordinator in the owning module.
- `Runtime/src/Engine`, `Interop/src/EditorModule`: runtime pause/drain/remount
  capability and typed output leases; no policy in native bridge code.
- `WorldEditor` scene commands, `MaterialEditor` cook requests, ContentBrowser
  provider/reducer, list/tile layouts, navigation, typed pickers, and command/result
  surfaces: consume shared state, preserve interaction scope, and implement the
  approved triggers and recovery. Existing WinUI controls/theme resources apply.
- `WorldEditor/src/Inspector`: SceneNodeEditorView/ViewModel,
  SceneNodeDetailsView/ViewModel, section templates/theme resources, and inspector
  packaged UI tests: compact sizing, component filter ownership and presentation.
- `Projects` project context/paths and `Managed.Assets` supported catalog/index
  adapters; neither executes editor workflow policy.
- Engine scene descriptor schemas/import builders/runtime scene systems for
  PostProcess/Background support, with engine-owned tests and validation.

## 8. Risks And Containment

Multi-root publication must have one recoverable outcome. Native read handles
must drain before fixed paths move. A saved snapshot cannot reference mutable
external inputs. Field names/units/ordinals come from engine-owned contracts.
Navigation and publication callbacks must preserve project/document/query scope.
Automatic cooking must remain bounded to saved/demanded content and cancellable;
its product semantics are settled in 07B.0 before implementation.

## 9. Validation Gates

- [x] 07B.0 UX review and trigger-policy decision are recorded; PRD/LLDs agree.
- [ ] 07B.1 input/revision/concurrency, dependency freshness, incremental reuse,
  coalescing and cancellation cases pass.
- [ ] 07B.2 publication, rollback, interruption, cancellation and lease cases pass.
- [x] 07B.3 every required field survives native cook/load observation.
- [ ] 07B.4 mismatch, import conversion/rejection and clean-copy reproduction pass.
- [x] 07B.6 cancellation owns/drains native workers and descendants (#8).
- [x] 07B.7 all eleven engine generator names, including the sphere alias, use
  one semantic authority; every selectable shape passes scene/project cook (#11).
- [ ] 07B.5a-c shared status, correct browser navigation, source/cooked/built-in
  presentation and typed assignment before/after cooking pass.
- [ ] 07B.5d-e approved triggers, all four Cook scopes, safe import/save entry
  points, useful Inspect/Validate, progress/recovery and accessible layouts pass.
- [ ] 07B.5g compact single/multi-node inspector, functional component selection
  and All reset pass without changing property/history/gesture semantics.
- [ ] 07B.5f all workflow journeys and recorded UI defects pass through the
  visible editor, including resumed preview and user validation evidence.

## 10. Status Ledger Hook

Record one ED-M07B result after these gates pass. Do not reopen ED-M07 or expand
its old validation row into proof of this transaction. ED-M08 starts only after
07A and 07B have their required evidence; no M04 audit/closure sweep is a dependency.
