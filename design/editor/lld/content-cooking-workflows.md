# Content Cooking Workflows LLD

Status: `ED-M07B implementation contract; D1 accepted 2026-09-11; Cooking panel decisions accepted 2026-09-12`

## 1. Purpose And Ownership

Make supported content easy to discover, prepare, assign, and update without
requiring users to understand generated descriptors, cooked paths, or mounting.
This is the user-workflow contract for ED-M07B. It traces to PRD `REQ-013` through
`REQ-024`, `REQ-026`, `REQ-036` through `REQ-041`, and `SUCCESS-006/007`.

ContentPipeline owns dependency freshness, requests, incremental execution, and
publication. ContentBrowser owns shared asset presentation and typed pickers.
MaterialEditor and WorldEditor own document saves and undoable assignments;
they submit requests through ContentPipeline. Runtime owns readiness and asset
load acceptance. Projects owns roots and retained source locations. Existing
operation results carry finalized outcomes and recovery. Cook-specific coordinator
state supplies live progress to the [Cooking panel](cooking-panel.md); there is
no second job system.

Related contracts: [content-pipeline.md](content-pipeline.md) sections 16-19,
[content-browser-asset-identity.md](content-browser-asset-identity.md),
[material-editor.md](material-editor.md),
[documents-and-commands.md](documents-and-commands.md),
[diagnostics-operation-results.md](diagnostics-operation-results.md),
[cooking-panel.md](cooking-panel.md), and
[runtime-integration.md](runtime-integration.md). These follow
[ARCHITECTURE.md](../ARCHITECTURE.md) and [DESIGN.md](../DESIGN.md).

## 2. Source Review And Scope

The current implementation provides useful browser rows, material/geometry
pickers, explicit Cook actions, and result messages. ED-M07B must close these
specific gaps:

| Source | Gap to close |
| --- | --- |
| `ContentBrowser/src/AssetIdentity/AssetIdentityReducer.cs` | Timestamp comparison cannot prove dependency freshness; file existence cannot prove publication or runtime readiness. An uncooked descriptor can acquire a missing-cooked diagnostic. |
| `ContentBrowser/src/AssetIdentity/ContentBrowserAssetItem.cs` | Abbreviations such as SRC/DESC/COOK/MISS do not explain the user's next action. |
| `ContentBrowser/src/Panes/Assets/AssetsViewModel.cs`, `AssetsView.xaml` | Cook results describe output counts/paths; the interaction needs scoped progress, cancellation, useful empty states, and clear recovery. Import copies external files with overwrite enabled and has early failures visible only in debug output. |
| `ContentBrowser/src/AssetIdentity/ContentBrowserAssetProvider.cs`; WorldEditor geometry/material pickers | Browser and picker projections need a common publication/freshness authority and consistent pre-cook availability. |
| `MaterialEditor/src/MaterialDocumentService.cs`, `MaterialEditorViewModel.cs` | Dirty Cook rejection must lead to an ordinary save/retry workflow; source save, approximate swatch, and actual scene material need distinct feedback. |
| PRD section 8; material/pipeline LLDs | The implemented baseline requires explicit cooking. M07B implements the accepted D1 trigger policy below. |

The [2026-09-11 running-editor review](../validation/ED-M07B-ux-review.md)
adds observed defects: list/tile navigation can display assets from the wrong
folder; a material marked cooked in the browser opens as NotCooked in its editor;
Filter has no effect; Cook is clipped in the material document; source/cooked
details are absent on selection; generated companions appear as separate picker
choices. These are required 07B.5 fixes, not optional visual polish.

The follow-up reports add Cylinder project-cook rejection and node-inspector
space/filtering defects. Pipeline section 19 owns full engine generator/alias
coverage; [property-inspector section 9.1](property-inspector.md#91-ed-m07b-compact-layout-and-component-filtering)
owns compact component selection and filtered editors in 07B.5g.

Use the existing WinUI browser, inspector pickers, material document, scene
toolbar, and output/results surfaces, with a dockable Cooking panel as the common
cook progress and recovery surface. Required work includes their empty states,
action labels, disabled reasons, progress, and keyboard/accessibility behavior.
Full material thumbnails, a new asset management application, a generic settings
panel, raw generated-file editing, scheduling beyond the existing cook queue,
rename/move/reference repair, and a new drag/drop placement system are outside
this slice. Qualified
formats/components remain those in PRD section 8 and pipeline section 17.

## 3. One Asset Before And After Cooking

The normal browser shows one row per logical authorable asset. Cooking updates
that row; it does not require finding another copy under Cooked or reassigning a
scene reference. Preserve folder, search, selected identity, scroll, and keyboard
focus across refreshes. Source, import settings, generated descriptor, and cooked
artifact are related facts available in details, not competing choices.

Folder tree, breadcrumb, row contents, and command scope must agree after every
navigation, list/tile switch, search, Back/Forward, and publication refresh.
Discard results from superseded queries. Disable actions on stale selections
during a scope transition; never cook an old row under a new folder breadcrumb.

One imported model may produce several geometry/material assets. Show the source
and its named outputs with their relationship, stable subasset identities, and
types. Before discovery, show the source with Import as its action; do not invent
assignable geometry identities from its filename. After import, typed pickers
show actual supported outputs. Shared source/cooked companions do not inflate
the PRD's logical asset count.

Expose engine-provided assets in an explicitly labeled Built-in group/filter
in the browser as well as the pickers. Use the 07B.7 provenance/identity mapping
to attach generated cooked companions to their originating built-in. Do not
offer cooker-internal names such as Engine_Generated_BasicShapes_Cube or
OxygenEditor_Default as duplicate ordinary choices when they are proven derived
companions. Do not deduplicate by filename or assume two defaults are equivalent.
An independently authored asset remains distinct; existing explicit references
to a derived identity remain resolvable and visible, without silent URI rewriting.

Default browsing includes valid uncooked and stale assets. Offer clear Type and
Status filters, including Needs cooking, Out of date, and Problems. An empty
project offers New Material / Import; an empty Cooked view explains that content
has not been cooked and links to authored content. Filtered emptiness offers
Clear filters. Built-ins are identified as Built-in and remain immediately usable.

Keep the existing Cooked (derived) tree as a read-only view of published content.
It uses logical asset names and links back to source where available. Runtime
tables and companion files belong under technical details/Inspect. Double-click
opens the supported source editor; a cooked-only item opens read-only details.
Cooked-only geometry/materials can be assigned when their identity and native
compatibility are valid; Edit/Reimport explains that source is unavailable.
No action overwrites a cooked binary or fabricates editable source from it.

### Visible State And Next Action

Saved/unsaved authoring, cooked freshness, operation phase, and runtime readiness
are separate facts. Present the most useful short status on rows, and all relevant
facts in details. Error and stale overlays must not erase the last good result.

When the SDK cannot provide its built-in catalog, discovery uses the last valid
engine-provided snapshot and marks those choices as last-known with a "Preview
unavailable" notice. Without a valid snapshot, show the catalog problem and no
engine choices. A successful SDK refresh replaces the derived cache and removes
the notice. This authoring fallback does not supply cook recipes or qualify
native operations.

| Situation | What users see | What users can do |
| --- | --- | --- |
| Recognized source not imported | Not imported; source type/name | Import, inspect source details. Raw model source is not yet a geometry choice. |
| Valid saved descriptor, no published output | Needs cooking; no error merely for being new | Open/edit; assign a known typed identity; Cook. Explain that viewport content is pending. |
| Unsaved edits | Unsaved changes; last cooked state separately | Save or continue editing. Swatch can update; runtime material remains the last published version. |
| Queued/cooking | Queued or Cooking, asset/scope, progress | Continue browsing/authoring; inspect progress; cancel. Existing published content stays usable. |
| Validated output being installed | Updating preview | Continue authoring. Keep the last frame with a visible pause indication until preview resumes. |
| Current published output, runtime accepts it | Ready | Assign/use immediately; edit source; inspect/validate output. |
| Current output with no runtime | Cooked; preview unavailable | Browse/edit/assign/cook offline. Explain native availability separately; do not recook just to mount. |
| Saved inputs/dependencies changed | Out of date; preview uses previous cook | Cook/update; inspect which asset changed. An older usable output is distinct from missing output. |
| Cook/update fails with prior output | Update failed; using previous cook | View problem, fix source/settings, Retry. Prior published state remains visible. |
| First cook fails | Could not cook; unavailable in preview | View problem and Retry after correction; keep the authorable asset and identity. |
| Source missing/invalid, or published data damaged | Source missing / Invalid source / Cooked content invalid | Inspect the named problem, restore valid source externally, or choose a replacement through existing pickers. Explain whether prior runtime content remains usable. |

Ready requires current dependency provenance, valid publication, and runtime
availability; a later per-asset load failure overrides it. Runtime not initialized,
tool mismatch, output invalidity, and missing source must not collapse into one
generic missing badge. A folder/project shows counts for its scope, not Ready
because the last single-asset cook succeeded. A successful historical cook can
coexist with Out of date relative to newer authoring.

Details lead with name, type, location, status, next action, and a short reason.
Show dependencies and source/output relationships read-only, with navigation to
the blocking asset. Put hashes, operation IDs, schema/build fingerprints, and
copyable source/generated/cooked paths under technical details. Paths for cleaned
temporary artifacts must be labeled no longer retained. A thumbnail or material
swatch is labeled approximate and never implies the current material is rendered.

## 4. Trigger Policy D1 - Hybrid Default

Accepted on 2026-09-11 when the user directed implementation of the revised
M07B plan. The table below replaces the explicit-only cook policy and includes
the explicit Save listed and Cook action in section 6. Source saving remains an
independent, explicit user operation.

Rationale: Import and Save express intent to produce usable content. Assignment
and scene opening express demand for preview. Browsing expresses discovery only.
Automatic work should follow those intents and rebuild only affected products.
Saving a shared material updates all its scene uses after publication;
Save completes independently of subsequent cook success.

| User interaction | Cooking behavior |
| --- | --- |
| Import / explicit Reimport | After supported inputs/settings are accepted and retained, cook changed outputs and required dependencies automatically, then publish. One visible workflow with separate source-import and publication outcomes. |
| Successful Save of a material, scene, or supported authored import settings | Queue incremental cooking for that saved scope. Save failure/conflict queues nothing. New document creation that successfully saves uses the same trigger once. Saving unchanged content is a no-op for cooking. |
| Choose a known uncooked/out-of-date asset in an inspector picker | Record the undoable identity assignment immediately and request its saved dependency closure. Show pending/previous content while it cooks. Do not require saving the consuming scene for an asset-only cook. |
| Open/activate a scene whose preview needs uncooked/out-of-date assets | Make the document usable immediately and request only the saved assets required by that active scene. Do not cook the scene itself merely to show its live authoring state. |
| Select a built-in geometry, None, or Default | Use the existing immediate engine capability. No cook is needed to preview a built-in; scene publication includes its required generated products. |
| Open a project, browse/select a row, search/filter, hover, refresh catalog, open a material source editor | Update metadata and display state; no cooking. Restored scene activation follows the scene-demand rule, not a whole-project cook. |
| Drag/type a property, Undo/Redo, or cancel an edit | Preserve existing live authoring behavior and mark cooked freshness as needed; never cook transient values. A subsequent explicit Save can trigger cooking. |
| External file changes or version-control updates | Reconcile catalog/dependencies and mark affected content out of date. Offer Reimport/Update; no unsolicited source reimport or generated-source replacement. Unacknowledged external changes block automatic requests until reviewed. |
| Explicit Cook Selected Asset / Folder / Current Scene / Project | Incrementally bring that scope and its required dependencies up to date. Show scope and affected dependencies; skip current products. Project cooking remains an explicit action. |
| Inspect / Validate Cooked Output | Read the published generation under a lease; do not start cooking or silently replace the generation being inspected. |

No autosave, automatic broad project scan-and-cook, or automatic external-source
reimport is introduced. Scene live transforms/lights/environment keep their
existing immediate sync; they do not wait for scene cooking. M08 continues to
require an explicitly identified published generation for standalone validation.

For batch work, add Pause automatic cooking / Resume to the existing Cook menu
and show a persistent paused indicator. This is session state owned by the
coordinator, reset on project close; no new durable preference/settings panel.
Pause prevents automatic jobs starting, does not interrupt an active transaction,
and coalesces saved/demand changes for Resume. Explicit Cook remains available.

### One Completion Path For Every Trigger

The familiar workflow is edit, save/apply, and see existing scene uses update.
Unreal's [material Apply action](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-editor-ui)
updates the material and its uses in the world. Unity's
[asset refresh](https://docs.unity3d.com/6000.0/Documentation/Manual/AssetDatabaseRefreshing.html)
tracks dependencies, imports changed data and hot-reloads assets. Unreal's
[platform cooking](https://dev.epicgames.com/documentation/en-us/unreal-engine/cooking-content-in-unreal-engine)
is a separate concern from applying material edits in the editor.

Oxygen's embedded runtime consumes cooked assets. Under D1, successful Save is
the material's automatic cook trigger; it does not require another Apply button.
Existing direct scene-property previews remain immediate. Transient gestures and
Undo/Redo never launch cooks. External reimport stays explicit under the accepted
policy. The supported asset formats remain the PRD's V0.1 set.

All triggers converge on one asynchronous project-owned workflow:

1. Successful Save, accepted Import/Reimport, preview demand, or explicit Cook
   submits the logical source scope. Save success is independent of cook success.
2. The coordinator coalesces requests, captures saved dependencies, and reuses
   verified products. Browsing and catalog refresh cannot submit work.
3. Changed products cook and validate in private staging while the previous
   published content remains usable.
4. The publication transaction pauses conflicting runtime reads, installs the
   complete root set, refreshes native sources and current asset bindings, and
   waits for the runtime's acknowledgment. It preserves the active document,
   selection, camera, and newer authoring changes. A material refresh does not
   require recreating the scene. Geometry refresh preserves surviving overrides.
5. A committed content-generation change updates the catalog, pickers, previews
   and Cooking history. It carries project/lifetime and generation identities,
   changed/removed assets, root set and outcome. Consumers reject obsolete work.

UI buttons only submit requests and present outcomes. Material-editor callbacks,
browser messages and filesystem watchers must not independently remount roots or
reload bindings. Runtime/cache invalidation is part of publication, for every
trigger and supported asset kind. Use the existing scene asset-request authority
to preserve current reference intent and reject late completions.

An unchanged cook causes no native work, root replacement, catalog-refresh loop
or preview pause. Failures keep the last working published content and expose
recovery in Cooking. Automatic work stays quiet and does not steal focus; its
errors remain discoverable. A saved asset may have failed cooking without becoming
unsaved again. No extra qualification command or separately built probe belongs
to this workflow.

## 5. Incremental Execution Contract

All automatic and explicit requests use the same snapshot and publication
transaction. Automatic triggers submit work; they never write output or remount
directly from a save, watcher, picker, or catalog callback.

- Compute product freshness from saved content hashes, dependency fingerprints,
  retained importer settings, generator versions, and qualified tool/schema
  identity. Timestamps are discovery hints only. Verify published outputs and
  indexes before reuse; a missing/corrupt output invalidates its product.
- Resolve the requested dependency closure and cook missing/changed products.
  Mark dependent products stale; rebuild dependent products inside the requested
  scope where their emitted data is affected. A material-only request does not
  rebuild every unrelated scene. Its dirty consuming scene is not an input.
  Preserve unrelated entries and validate the complete resulting roots.
- If nothing changed, report Already up to date without spawning native workers,
  rewriting roots, or pausing preview. If only mount/readiness needs repair,
  handle that through runtime recovery without inventing source staleness.
- Persist provenance so a reopened project makes the same reuse decision.
  Track added/removed dependencies and source-owned subassets, not just updates.
  A failure to establish a complete dependency set blocks that request visibly.
- Coalesce duplicate pending requests for the same project, target, and saved
  revision. A broader pending request may absorb contained work, preserving
  caller correlation and cancellation. Running snapshots never change in place.
  Capture queued work only after acquiring the project gate.
- Repeated saves during a cook retain at most the latest pending revision for
  that target. The completed revision is labeled stale when appropriate, and
  the next valid saved revision follows. Publishing never clears newer edits.
  Changes confined to unsaved data do not create another job.
- User-demand and explicit requests run ahead of pending background-save work;
  do not preempt a running publication. Results expose queued reason and scope.
  Burst coalescing must not introduce an arbitrary long idle wait.
- Pause/cancel is respected: reopening a picker or receiving catalog events
  cannot immediately recreate cancelled work for the same revision. Retry, a new
  explicit request, or a new successful Save can request work again. Failure
  does not create an automatic retry loop. Resume releases paused work, not
  previously cancelled/failed work without a new request.
- Cancelling one observer of shared work detaches its request. Stop the worker
  only when no remaining request owns it, or when the user cancels the displayed
  operation. Project close drains all project-owned work under section 18.
- Undoing an assignment, changing selection, closing a document, or switching
  scenes invalidates its demand observer. Completion may populate the catalog
  but cannot restore an undone assignment or update a different document.

Incremental planning/reuse is testable independently of triggers: explicit Cook scopes
must also skip verified current products. Full-root validation and recoverable
publication remain mandatory even when only one product is rebuilt.

## 6. Interaction Details And Failure Recovery

### Import And Reimport

Show supported formats and the destination authoring mount/folder before starting.
Retain sources/dependencies in the project according to pipeline section 17;
selecting a derived root does not place source there. A filename collision offers
Choose another name/location, explicit Replace/Reimport of the identified source,
or Cancel. Never silently overwrite source, import settings, or authored material
edits. Dirty affected documents and changed output identities block replacement
with an actionable explanation. Unsupported content lists the feature and source.

Import progress ends at usable output or a clear partial result such as Imported;
cooking failed. Retained source remains discoverable and Retry does not require
choosing the file again. After success, offer Show imported assets in the browser;
do not steal selection/focus if the user has moved elsewhere. A source producing
several meshes lets the user choose the intended named geometry in the picker.

### Save And Cook

Finish/cancel an active gesture through its normal control contract. If the
selected cook closure contains dirty documents, list them inline with Open and
Save listed and Cook, plus Cancel. The latter explicitly authorizes ordinary saves
of exactly the named documents, waits for success, then rechecks the closure.
Do not save unrelated documents or silently expand the list after consent.
Any save conflict uses the existing conflict flow and keeps cooking blocked.
Save and Close keeps its accepted inline conflict actions; a cook failure cannot
turn a successfully saved document back into an unsaved document or reopen it.

Background requests wait with Needs save and an action, not a surprise modal.
They never bypass the pipeline's dirty-input rejection. Saves acknowledged after
a newer edit leave that document dirty, so capture waits for a coherent saved
closure. Cooking after Save never holds up the source-save success notification.

### Picking And Preview

Material and geometry pickers share browser identity/state, include valid saved
uncooked choices, and show Built-in, Ready, Needs cooking, and Out of date in
plain language. Pin an existing missing/broken assignment with its explanation;
prevent new invalid assignments without hiding their reason. Separate browsing
selection from committing an assignment. No cook starts merely opening a picker.

Commit one history entry for the assignment. Cooking/publication creates no
scene/material history entries. Pending or failed preparation retains the chosen
identity and explains what the viewport shows: the previous material/geometry
where available, or the engine's existing fallback/unresolved state. Do not
silently replace the authored URI with None/Default or erase a failed assignment.
Successful publication refreshes all current uses of the affected identity with
existing generation/lifetime guards, without reassigning or restarting.

### Progress, Results, And Recovery

The [Cooking panel contract](cooking-panel.md) owns detailed progress, session
history, asset-grouped issues, run-scoped progress/technical output, cancellation,
Retry, and Save listed & Cook for
all scopes. Explicit Cook opens/selects its run; automatic work never opens the
panel or moves focus. The accepted compact split layout and filtering keep the
default view focused on active work and actionable outcomes.
The selected run's Output section shows its messages live and after completion
inside Cooking; users do not need to search or navigate the global Logs panel.

Use a persistent compact status near the cook action and affected row/slot:
Queued, Checking dependencies, Cooking, Validating, Updating preview, Ready.
Show asset/scope, completed/total items when known, and Cancel. Use indeterminate
progress instead of fabricated percentages when total work is unknown. Start
feedback within the PRD's 100 ms gate. Details expose changed/reused counts,
trigger (Save/Import/Use/Cook), blockers, and links to affected assets/results.

During the short publication boundary, show Updating preview; cancellation reads
Finishing update when it must wait for commit/rollback. A cancellation report
follows actual worker drain or transaction completion. Multiple saves produce a
coalesced status, not stacked notifications. Routine automatic success does not
open a modal or steal focus. Failure remains actionable after transient messages
are dismissed and is visible from the asset and Cooking panel. For asset failures
inside a folder/project cook, continue independent work, skip dependents, and
collect issues; a failed cook does not replace published output.

Publication failure says whether previous content was restored; failed recovery
keeps preview visibly unavailable and offers Retry recovery after the cause is
resolved. Tool mismatch explains which capability is unavailable while browsing,
authoring, and safe saves continue. A stopped runtime yields Cooked / preview
unavailable, not cook failure; activation mounts the validated generation.

Inspect shows the actual scope and a browsable read-only asset/dependency report,
not only counts and a filesystem path. Validate distinguishes output integrity
from freshness and runtime availability. Reuse the existing details/results
surfaces; neither command changes the selected asset or initiates a cook.

Every required action is keyboard reachable, has a meaningful accessible name,
and displays its disabled reason outside hover-only tooltips. Use text/icons in
addition to color, announce phase/result changes without reading every progress
tick, and preserve focus across row replacement, popups, and publication.
Reuse DroidNet controls and existing spacing/theme resources.

At supported dock widths, reflow or overflow commands without clipping Save,
Cook, Cancel, status, or picker choices. Long paths stay in expandable details;
long asset names remain distinguishable. Use appropriate existing asset icons
and material swatches; the current geometry translation glyph and material
missing-glyph boxes are not acceptable type identification. Test list and tiles
at 100%, 150%, and 200% display scaling, keyboard-only navigation, and narrowed
docks. Enforce a documented minimum pane width or provide a usable overflow.

## 7. Qualification Journeys

ED-M07B.5 records packaged UI/integration evidence and user-visible walkthroughs
for these cases, including the accepted D1 triggers.

| Case | Required observation |
| --- | --- |
| Empty project -> create material -> save -> assign | Asset is discoverable before cooking; approximate swatch, saved state, queued work, and actual ready material are distinguishable. No path entry or manual mount. |
| Import supported glTF/FBX -> choose one of several outputs -> assign | Destination and source relationship are clear; stable named outputs become usable in the geometry/material pickers. |
| Import collision, unsupported source, or dirty reimport target | No silent overwrite/drop; clear named blocker and recovery; prior source and publication survive. |
| Browse/search/filter/open picker with 1,000 logical entries | No cook launches; focus/selection remain stable during catalog updates; PRD browser timing gates hold. |
| Navigate Materials/Geometry/Scenes, switch list/tiles, Back/Forward, then act | Breadcrumb, rows, selection, and operation scope agree; old asynchronous results cannot repopulate the wrong folder. Filters actually change the result set. |
| Browse built-ins and their cooked companions | Proven generated outputs stay attached to their originating identity; legitimate authored assets and existing references remain distinct and usable. |
| Select Cylinder, save, Cook Current Scene / Cook Project; repeat the full engine generator catalog and aliases | Every supported choice cooks and loads with its shared recipe. Diagnostics identify the captured scene/node/asset when another reference fails. |
| Single/multi-node component filter and compact inspector | Geometry/Transform selection restricts editors; deselection/All restores applicable sections. The header/list consumes only needed space; filtering creates no authored changes or cooks. |
| Uncooked vs missing vs stale vs cooked-only assets | Distinct actions and state; typed identity survives Save/reopen; cooked-only editing stays read-only. |
| Save shared material used on several nodes | Exactly the affected products rebuild; all current uses update after publication, with no additional history entry. |
| Assign uncooked material into a dirty scene | Asset-only cook can run when its own inputs are saved; consuming scene need not be saved. |
| Repeated saves, later unsaved edit, pause/resume, cancel/retry | Work coalesces; newer edits stay dirty; cancelled/failed revisions do not restart themselves; no false Ready. |
| Undo assignment or switch/close scene during cooking | Completion never revives old assignment or applies to a different lifetime. |
| Asset/folder/scene/project Cook twice, then change one dependency | Second unchanged request performs no worker/output/pause work; next request rebuilds affected products and preserves unrelated entries. |
| Dirty dependency -> Save listed and Cook -> save conflict | Only named documents are offered; ordinary conflict recovery applies; no cook before successful saved capture. |
| Worker/validation/publication/mount failure and cancellation | Visible outcomes distinguish first failure from usable prior output; Retry recovery needs no generated-file repair. |
| Project reopen, external changes, and deleted derived output | Provenance survives; external reimport stays explicit; required missing products regenerate without changing logical identities. |
| Offline native state or mismatched build | Authoring and saves work; cooking/readiness report the specific capability; activation converges without an unnecessary recook. |
| Keyboard-only, screen reader, theme and scale checks | Core import/cook/browse/pick/recovery actions are usable with readable state and stable focus. |

Repeat the recorded running-editor journeys after implementation, alongside
new before/during/after cooking cases, to verify discoverability, legibility, and
interaction behavior. The planning review records current defects; milestone
validation requires the corrected workflows. M08 owns rendered standalone parity.

## 8. Decision Record

D1 adopts the hybrid trigger table and session pause control in section 4,
including explicit Save listed and Cook. A successful saved revision schedules
cooking and can refresh every shared use. Source saving stays explicit and
independent. PRD `REQ-014/026`, section 8, and the material/pipeline contracts use
this policy. ED-M05/06/07/07A evidence remains historical; the new behavior is
qualified in M07B through the same saved-input, rollback, and lifetime contracts.

Dependency-aware incremental processing is established in
[Unity's asset database workflow](https://docs.unity3d.com/6000.0/Documentation/Manual/AssetDatabaseRefreshing.html),
which tracks dependency changes to determine reimport. This informs reuse and
invalidation here; the Oxygen trigger policy is a product decision,
not a claim that another editor's refresh and Oxygen cooking are equivalent.
