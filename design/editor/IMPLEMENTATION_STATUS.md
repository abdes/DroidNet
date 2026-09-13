# Oxygen Editor Implementation Status

Status: `authoritative tracker`

This is the resumability document for Oxygen Editor V0.1. It records what is
planned, active, landed, validated, blocked, or deferred. It does not replace
the PRD, architecture, design, LLDs, top-level plan, or detailed implementation
plans; it links them into one execution ledger.

Rules for this file:

1. Every milestone row traces to `GOAL-XXX`, `REQ-XXX`, and `SUCCESS-XXX` IDs
   from [PRD.md](./PRD.md).
2. A checkbox is checked only when the implementation or document artifact
   exists and the stated evidence is recorded.
3. Validation evidence is concise: one ledger row per milestone.
4. Unfinished work is tracked in milestone plan checklists, not in a
   generic gaps list.
5. Blockers and decisions are recorded only when they block a named milestone.

## 1. Status Vocabulary

| Status | Meaning |
| --- | --- |
| `planned` | Scope is accepted but not started. |
| `active` | Work is currently being designed or implemented. |
| `landed` | Implementation or document artifact exists but milestone validation is incomplete. |
| `validated` | Implementation or document artifact exists and validation evidence is recorded below. |
| `blocked` | Progress requires a named decision, dependency, or engine/API change. |
| `deferred` | Explicitly outside V0.1 or moved out by decision. |
| `pending` | Validation ledger placeholder used before a milestone has recorded validation evidence. |

## 2. Current Focus

Current program target: **Oxygen Editor V0.1**, with the closed capability/UI,
explicit-Save recovery, publication-pause and small-project qualification choices
in PRD sections 8-10.

Current execution:

1. Collect the already-pending ED-M02 supported single-viewport evidence.
2. Execute [ED-M07B](plan/ED-M07B-safe-content-publication-and-compatibility.md):
   saved input/staging/publication/recovery, required native descriptor mappings,
   matched-build and portable-import guarantees.
3. Continue through the exact ED-M08 parity, ED-M09 interaction and ED-M10 release
   qualification plans. No later milestone is an entry dependency of an earlier one.

[ED-M07A](plan/ED-M07A-authoring-integrity-and-runtime-convergence.md) is validated
as of 2026-09-11: packaged control/native tests pass 137/137, and the user confirmed
the final viewport workflows through Undo/Redo and Save/reopen.

Previously recorded milestone statuses and evidence are preserved. No new
implementation or closure sweep is assigned to M04. Its identified authoring
omissions and missing evidence were resolved in ED-M07A; downstream descriptor
gaps remain in ED-M07B. Issue fixes #2-5 are included through editor ea395a310. Preserve their recorded
automated evidence and limits; they do not automatically advance a milestone.
PLAN section 9 maps #6-11 to the remaining concrete gap tasks.

## 3. Milestone Tracker

### ED-M00 - Design Package And Execution Baseline

Status: `validated`

Trace: `GOAL-001`, `GOAL-002`, `GOAL-003`, `GOAL-004`, `GOAL-005`,
`GOAL-006`; `REQ-001` through `REQ-037`; `SUCCESS-009`

Outcome: `design/editor` is canonical, reviewed, traceable, and ready to drive
implementation.

- [x] README establishes Editor V0.1 as the first production-quality vertical
      slice.
- [x] RULES separates non-negotiable engineering rules from architecture
      choices.
- [x] PROJECT-LAYOUT defines authoritative ownership and placement rules for
      .NET projects, editor UI, tools, domains, and MSBuild conventions.
- [x] PRD uses traceable `GOAL-XXX`, `REQ-XXX`, and `SUCCESS-XXX` identifiers.
- [x] PRD requirements describe product behavior, not verification technique.
- [x] ARCHITECTURE defines module ownership, runtime/content boundaries,
      dependency rules, and traceability.
- [x] DESIGN routes architecture concerns to LLD owners and cross-LLD workflow
      contracts.
- [x] LLD index covers the V0.1 subsystem set and each LLD has at least a
      scaffold.
- [x] PLAN defines milestone sequence, LLD schedule, and detailed-plan handoff.
- [x] PLAN is reviewed and accepted.

Exit evidence required:

- [x] One `ED-M00` validation ledger row records the completed design-package
      review and remaining approved execution assumptions.

### ED-M01 - Project Browser And Workspace Activation

Status: `validated`

Trace: `GOAL-001`, `GOAL-006`; `REQ-001`, `REQ-002`, `REQ-003`,
`REQ-022`, `REQ-024`; `SUCCESS-001`

Outcome: Project Browser startup, project open/create, invalid project
handling, workspace transition, and restoration failure visibility work as
product behavior.

- [x] Required LLDs are reviewed:
      `project-workspace-shell`, `project-services`,
      `diagnostics-operation-results`.
- [x] Detailed `ED-M01` implementation plan exists.
- [x] `ED-M01.1` diagnostics contracts, host operation-result store, output-log
      adapter, and reducer/exception-adapter tests are implemented.
- [x] `ED-M01.2` removes eager engine startup and `IEngineService` injection
      from Project Browser application launch.
- [x] `ED-M01.3` project validation, V0.1 manifest schema, active project
      context, project creation, recent-project adapter, and cook-scope service
      contracts are implemented and covered by project-service tests.
- [x] `ED-M01.4` shell activation coordinator contract and host
      implementation are implemented; Project Browser open/create view models
      submit activation requests instead of loading projects and navigating to
      `/we` directly.
- [x] `ED-M01.5` Project Browser service/UI migration is implemented:
      Project Browser support services are narrowed, view models route through
      the activation coordinator, open/create failures surface inline operation
      results, and stale recent entries stay visible with an explicit remove
      action.
- [x] `ED-M01.6` workspace activation and restoration implementation is
      landed: workspace activation waits for committed project context, engine
      startup runs before workspace cooked-root refresh, dock/content browser
      restoration is best-effort, and content browser selection is preserved.
- [x] `ED-M01.7` dependency cleanup and targeted tests are landed:
      Project Browser has no direct WorldEditor/runtime/project-manager
      activation dependency, Content Browser uses project context for project
      metadata, and targeted `Oxygen.Managed.Core` / `Oxygen.Editor.Projects` tests
      pass.
- [x] Editor starts at Project Browser.
- [x] Recent project, create project, open project, and invalid project states
      are usable.
- [x] Successful project open transitions into the editor workspace.
- [x] Workspace restoration is best effort and visible when partially
      unsuccessful.
- [x] Project open/create failures produce visible operation results.

Exit evidence required:

- [x] One `ED-M01` validation ledger row records startup path, project
      open/create behavior, invalid project behavior, workspace activation, and
      restoration failure behavior.

### ED-M02 - Live Viewport Stabilization

Status: `landed`

Trace: `GOAL-001`, `GOAL-003`, `GOAL-006`; `REQ-022`, `REQ-023`,
`REQ-024`, `REQ-025`, `REQ-027`, `REQ-028`, `REQ-030`; `SUCCESS-003`,
`SUCCESS-005`

Outcome: embedded Vortex preview, native runtime discovery, runtime settings,
and the supported single live viewport path are landed. Multi-viewport layout
stability is deferred out of ED-M02.

Implementation:

- [x] Embedded engine starts from the editor process.
- [x] Native engine runtime DLLs load from the engine install runtime
      directory instead of being copied into editor output.
- [x] A live Vortex-rendered editor viewport is visible.
- [x] Editor viewport camera uses sane projection/framing defaults.
- [x] Required LLDs are reviewed:
      `runtime-integration`, `viewport-and-tools`,
      `diagnostics-operation-results`.
- [x] Detailed `ED-M02` implementation plan exists and is accepted.
- [x] Runtime, single-surface view, cooked-root, viewport-layout, and
      runtime-settings workflows publish stable operation-kind diagnostics.
- [x] Runtime FPS setter clamps before applying to the engine runner.
- [x] Issue #3 corrects shutdown guards, retains failed native ownership for
      retry, and shuts down through application lifecycle integration after
      approved document close. The engine owner rebuilt/reinstalled the native
      stop/reset fix; 29 runtime, 13 Aura close, and 3 native tests pass. Standard
      Debug editor build and startup/normal-close smoke pass; modified C# files
      have no active compiler/analyzer/IDE diagnostics. Full manual viewport and
      dirty-document replay remains separate from this evidence.
- [x] Cooked-root refresh warnings are non-fatal and leave the workspace usable.
- [x] Targeted MSBuild builds and executable test runs pass for the ED-M02
      touched projects.
- [x] Multi-viewport stability is deferred out of ED-M02 and must be handled by
      later engine multi-surface/multi-view work.

Validation:

- [ ] One-pane viewport layout is validated after clean editor launch.
- [ ] Correct surface presentation is validated for the supported live
      viewport.
- [ ] Engine FPS/runtime settings are validated in the embedded engine.

Exit evidence required:

- [ ] One `ED-M02` validation ledger row records launch path, build config,
      single viewport result, runtime DLL discovery path, runtime settings
      result, and outcome.

### ED-M03 - Authoring Foundation

Status: `validated`

Trace: `GOAL-001`, `GOAL-002`, `GOAL-003`, `GOAL-006`; `REQ-004`,
`REQ-005`, `REQ-006`, `REQ-007`, `REQ-008`, `REQ-009`, `REQ-022`,
`REQ-023`, `REQ-024`, `REQ-026`, `REQ-037`; `SUCCESS-001`, `SUCCESS-002`,
`SUCCESS-003`, `SUCCESS-004`

Outcome: scene documents, commands, dirty state, undo/redo, selection model,
scene explorer, operation results, and save/reopen form a reliable authoring
core.

Issue #2 close protection is implemented: tab closure and scene replacement use
Save/Discard/Cancel, and workspace/window closure uses one selectable unsaved
document list. Document teardown follows successful preparation and all window
vetoes; material disposal no longer discards implicitly. Focused lifecycle,
material persistence, and native window/dialog tests cover the failure and
cancellation boundaries. Full editor scenario validation is still pending.

Issue #4 is implemented: scene/material saves capture coherent snapshots,
serialize competing writes, and acknowledge only persisted revisions. Newer
edits remain dirty and prevent save-and-close. Standard Debug editor build and
232 relevant tests pass, including deterministic storage interleavings and
hierarchy edits awaiting live sync. Modified C# files have no active compiler,
analyzer, or IDE diagnostics. Manual interaction replay was not performed.

Issue #5 is implemented: scene-session request generations reject superseded
geometry/material completions, and accepted results apply during scene mutation
after queued authoring commands. Clear, detach, deletion, scene replacement,
and shutdown invalidate obsolete work. Debug editor build and all 34 native
tests pass, including 19 controlled-completion regression cases. Native analysis
reports zero findings in modified source/test files. No engine source changes
or engine rebuild were required; manual editor interaction was not replayed.


- [x] Required LLDs are reviewed:
      `documents-and-commands`, `scene-authoring-model`, `scene-explorer`,
      `diagnostics-operation-results`.
- [x] Detailed `ED-M03` implementation plan exists.
- [x] Node create/delete/rename/reparent operations use the ED-M03 authoring
      paths; the proper DynamicTree rename commit hook is deferred and tracked.
- [x] Quick-add primitive and directional light creation use command paths.
- [x] Dirty state and undo/redo work for supported mutations.
- [x] Scene save/reopen support is implemented for supported values.
- [x] Live-sync intent is requested after supported mutations.
- [x] Operation result presentation exists for command, save, and sync
      failures.

Exit evidence required:

- [x] One `ED-M03` validation ledger row records command/dirty/undo behavior,
      save/reopen result, live-sync intent result, operation-result behavior,
      and cook readiness.

### ED-M04 - Scene Editing UX And Component Inspectors

Status: `landed`

Trace: `GOAL-002`, `GOAL-003`, `GOAL-006`; `REQ-005`, `REQ-007`,
`REQ-008`, `REQ-009`, `REQ-022`, `REQ-024`, `REQ-026`, `REQ-037`;
`SUCCESS-002`, `SUCCESS-003`, `SUCCESS-004`

Delivery record: the original command/data and inspector work has the recorded
status and partial evidence below. Its implementation omissions are identified
from source in ED-M07A; native descriptor omissions are in ED-M07B. All required
V0.1 behavior is retained. No new work is assigned to an M04 closure sweep.

- [x] Required LLDs are reviewed:
      `property-inspector`, `environment-authoring`, `settings-architecture`,
      `live-engine-sync`, `runtime-integration`.
- [x] Detailed `ED-M04` implementation plan exists.
- [x] `ED-M04.1` baseline audit and LLD lock is complete; direct inspector
      mutation paths, sync throw/log-only paths, and test hosts are recorded in
      the detailed plan.
- [x] `ED-M04.2` command contracts, edit records, operation vocabulary, and
      persisted component identity are implemented and reviewed.
- [x] `ED-M04.3` scene environment domain and serialization are implemented
      and reviewed.
- [x] `ED-M04.4` live-sync result adapter contracts, runtime readiness
      classification, unsupported material/environment outcomes, coalescer
      service contract, and targeted tests are implemented and reviewed.
- [x] `ED-M04.5` component/environment command implementation is landed and
      reviewed: Transform, Geometry, Material slot, PerspectiveCamera,
      DirectionalLight, Environment, AddComponent, and RemoveComponent commands
      validate, mutate authoring state, write undo entries, mark dirty only
      after mutation, request live sync, and publish operation results for
      validation/sync failures.
- [x] Geometry components expose a material assignment/override slot command
      that persists placeholder/sentinel identity; real material asset creation
      and picking still close in `ED-M05`.
- [x] Component edit command layer uses services and `ISceneEngineSync`, not
      direct interop.
- [x] Inspector host view models route ED-M04-supported fields through the
      command service instead of direct mutation.
- [x] Component edits persist through save/reopen from the migrated inspector
      UI.

The source-backed gap table and pass/fail cases are in
[ED-M07A](plan/ED-M07A-authoring-integrity-and-runtime-convergence.md)
and [ED-M07B](plan/ED-M07B-safe-content-publication-and-compatibility.md).
M04's recorded status and original evidence are unchanged; new results are
recorded under those gap-closing milestones, not retrospectively attributed here.

### ED-M05 - Scalar Material Authoring

Status: `validated`

Trace: `GOAL-002`, `GOAL-004`, `GOAL-005`, `GOAL-006`; `REQ-010`,
`REQ-011`, `REQ-012`, `REQ-013`, `REQ-014`, `REQ-021`, `REQ-022`,
`REQ-037`; `SUCCESS-002`, `SUCCESS-004`, `SUCCESS-007`

Outcome: users can create/open/edit scalar material assets and assign them to
geometry through real editor UI.

Current note: the corrective ED-M05 implementation pass is landed. Material
source descriptors are created under the Content authoring mount, cooked output
uses the active project's `.cooked/<mount>` root, geometry material picker
refreshes on open, and the Material Editor UI uses shared editor property
controls plus DroidNet number controls. The Material Editor identity section
shows asset URI and editor-side asset GUID copy affordances. ED-M05 validation
closed after `ED-M06A` confirmed project template layout, material create
targets, and Content Browser/picker refresh behavior under the accepted project
filesystem contract.

- [x] Required LLDs are reviewed:
      `material-editor`, `content-browser-asset-identity`,
      `asset-primitives`, `content-pipeline`.
- [x] Detailed `ED-M05` implementation plan exists.
- [x] Corrective workflow/UI pass is landed after the rejected first
      implementation.
- [x] Users can create/open scalar material assets.
- [x] Users can inspect and edit scalar material values.
- [x] Users can assign material assets to geometry through asset identity.
- [x] Material values save and reopen.
- [x] Minimum scalar material descriptor/cook/preview slice is validated or a
      visible engine/API limitation is recorded.

Exit evidence required:

- [x] One `ED-M05` validation ledger row records material create/edit/assign,
      save/reopen, minimum cook/preview slice, and visible failure behavior.

### ED-M06 - Asset Identity And Content Browser

Status: `validated`

Trace: `GOAL-004`, `GOAL-005`, `GOAL-006`; `REQ-013`, `REQ-020`,
`REQ-021`, `REQ-022`, `REQ-024`, `REQ-036`, `REQ-037`; `SUCCESS-006`,
`SUCCESS-007`

Outcome: source, generated, descriptor, cooked, missing, broken, and
runtime-availability overlay states are visible and selectable by identity.
Authoritative mounted state is deferred to `ED-M07`.

Current note: ED-M06 validation closed after `ED-M06A` corrected project root
layout, folder navigation, material row filtering, and template-created project
structure against `project-layout-and-templates.md`.

- [x] Required LLDs are reviewed:
      `content-browser-asset-identity`, `asset-primitives`,
      `project-services`, `diagnostics-operation-results`.
- [x] ED-M06 LLDs are drafted for review with concrete content-browser row,
      state-reducer, project-root, typed-picker, and diagnostics contracts.
- [x] Detailed `ED-M06` implementation plan exists.
- [x] Detailed `ED-M06` implementation plan is reviewed and accepted.
- [x] Content browser distinguishes source, generated/descriptor, cooked,
      missing, broken, and runtime-availability overlay states. ED-M06 may
      show `Unknown`/`NotMounted`; authoritative `Mounted` is deferred to
      `ED-M07`.
- [x] Asset picker returns typed asset identity.
- [x] Broken references become visible diagnostics.
- [x] Authoring data avoids raw cooked-path text as the user-facing identity.

Exit evidence required:

- [x] One `ED-M06` validation ledger row records asset browsing, picking,
      missing-reference behavior, and persistence impact.

### ED-M06A - Game Project Layout And Template Standardization

Status: `validated`

Trace: `GOAL-001`, `GOAL-004`, `GOAL-005`, `GOAL-006`; `REQ-002`,
`REQ-017`, `REQ-018`, `REQ-019`, `REQ-020`, `REQ-021`, `REQ-022`,
`REQ-024`, `REQ-036`, `REQ-037`; `SUCCESS-001`, `SUCCESS-006`

Outcome: newly created projects, predefined templates, scene/material creation
targets, content-root navigation, source-media placement, local/extra mounts,
and derived-root presentation match the accepted game-project filesystem
contract before content pipeline work starts.

- [x] Game-project filesystem architecture contract is added to
      `ARCHITECTURE.md`.
- [x] `project-layout-and-templates.md` LLD exists and is reviewed together
      with architecture for consistency.
- [x] Detailed `ED-M06A` implementation plan exists.
- [x] Detailed `ED-M06A` implementation plan is reviewed and accepted.
- [x] Built-in template descriptors and payloads satisfy the V0.1 template
      contract.
- [x] Project creation generates a unique project id, writes final
      `Project.oxy`, creates the required folder skeleton, validates the
      result, and opens the declared starter scene.
- [x] Scene create/open/save uses `Content/Scenes/*.oscene.json`.
- [x] Material create/open/save uses `Content/Materials/*.omat.json`.
- [x] Content Browser folder navigation refreshes visible rows without editor
      restart.
- [x] Material Picker shows one logical row per material and no unrelated
      project files.
- [x] Source media, config, packages, and derived roots are presented
      distinctly and never become implicit authored-asset creation targets.
- [x] Extra authoring mounts and explicitly selected local mounts follow the
      target resolver rules.
- [x] Project/template/layout failures produce visible operation results.

Exit evidence required:

- [x] One `ED-M06A` validation ledger row records template validation, project
      creation, unique id behavior, scene/material authored paths, Content
      Browser folder navigation, Material Picker filtering, and target
      resolver behavior.

### ED-M07 - Content Pipeline And Cooking

Status: `validated`

Trace: `GOAL-001`, `GOAL-002`, `GOAL-005`, `GOAL-006`; `REQ-015`,
`REQ-016`, `REQ-017`, `REQ-018`, `REQ-019`, `REQ-022`, `REQ-023`,
`REQ-024`, `REQ-036`, `REQ-037`; `SUCCESS-001`, `SUCCESS-004`,
`SUCCESS-006`

Outcome: descriptor/manifest generation, cook, inspect, cooked validation,
catalog refresh, and mount refresh work as explicit workflows.

- [x] Required LLDs are reviewed:
      `content-pipeline`, `project-services`, `asset-primitives`,
      `runtime-integration`, `diagnostics-operation-results`.
- [x] Detailed `ED-M07` implementation plan exists.
- [x] Detailed `ED-M07` implementation plan is reviewed and accepted.
- [x] `ED-M07.1` native API audit records the selected Interop/ImportTool
      execution path, schema validation path, mount layout, scene descriptor
      name/environment policies, derived procedural geometry strategy, and
      unvalidated mount-refresh publisher cleanup before ED-M07.2+ coding.
- [x] `ED-M07.2` operation-kind/diagnostic vocabulary and content-pipeline
      contract records are implemented with focused contract tests.
- [x] Save/import paths no longer publish direct unvalidated cooked-root
      refresh messages; runtime mount refresh remains reserved for validated
      content-pipeline completion.
- [x] Procedural geometry descriptors are generated for supported procedural
      meshes.
- [x] Scene descriptor generation emits native scene descriptor JSON for V0.1
      scene nodes, generated geometry, material refs, perspective cameras, and
      lights with focused tests.
- [x] Scoped source import produces supported descriptor manifests for asset,
      folder, project, and generated-scene-descriptor inputs.
- [x] Cooking includes current scene, one asset, selected folder, full project,
      and referenced V0.1 asset descriptors with focused service tests.
- [x] Cook output inspection/validation and validation-gated runtime refresh
      have the recorded ED-M07 evidence. Transactional replacement is ED-M07B.
- [x] Content Browser exposes Cook Selected Asset, Cook Folder, Cook Project,
      Inspect Cooked Output, and Validate Cooked Output commands; commands
      publish operation results and refresh catalog state.
- [x] Scene editor exposes Cook Current Scene from the scene toolbar and routes
      it through the same explicit content-pipeline service.
- [x] Runtime cooked-root refresh is requested only from validated cooked-output
      messages after cook/validate success.
- [x] Cook, inspect, and mount failures produce visible operation results across
      all command entry points and manual failure scenarios.

Exit evidence required:

- [x] One `ED-M07` validation ledger row records descriptor/manifest inputs,
      cook output, inspect result, mount result, and failure-result behavior.

### ED-M07A - Authoring Integrity And Runtime Convergence

Status: `validated`

Trace: `REQ-005` through `REQ-009`, `REQ-011`, `REQ-012`, `REQ-014`, `REQ-022`,
`REQ-024`, `REQ-026`, `REQ-037`, `REQ-038`; `SUCCESS-002`, `SUCCESS-003`,
`SUCCESS-007`.

Plan: [ED-M07A](plan/ED-M07A-authoring-integrity-and-runtime-convergence.md).

- [x] 07A.0 managed world/input capabilities, native asset-failure forwarding and
      automated boundary/lifecycle coverage (#10); visible workflow evidence is 07A.6.
- [x] 07A.1 native background application and truthful field results: native/UI
      tests pass; the user verified picker matching, exposure/tone-map independence,
      Undo/Redo and Save/reopen in the rebuilt editor.
- [x] 07A.2 camera/light/environment gesture sessions and cancellation: packaged
      controls pass numeric/picker, history, wheel, selection and real Escape cases;
      inspector tests 40/40 and shared controls 50/50.
- [x] 07A.3 revision/target-scoped inline diagnostics and affected-field invalidation:
      inline numeric, selection and sun removal/restoration cases pass; inspector 42/42.
- [x] 07A.4 offline/reconnect/lifetime convergence with failed work retained;
      WorldEditor 131/131 and Runtime 61/61 regressions pass.
- [x] 07A.5 shared atomic writes and inline scene/material conflict recovery (#7),
      with revision, cancellation, history and reload-lifetime regressions passing.
- [x] 07A.7 active loop supervision/state diagnostics and restart isolation (#6),
      with Runtime direct-service and native regressions passing 59/59.
- [x] 07A.8 material document history and edit sessions (#9); MaterialEditor 38/38,
      including document-scoped UI command routing and focused input at close.
- [x] 07A.6 actual-control/native field and transition evidence: packaged tests
      137/137, Runtime 71/71, SceneExplorer 161/161, World 67/67 and Managed.Assets
      89/89. Results are recorded in the field/workflow table.
- [x] 07A.6 final viewport qualification: the user confirmed composed XYZ rotations,
      Cube/Sphere changes with a newly cooked material, None and Default, and
      coupled sun controls through Undo/Redo and Save/reopen on 2026-09-11.

### ED-M07B - Safe Content Publication And Compatibility

Status: `in_progress`

Trace: `REQ-013` through `REQ-024`, `REQ-026`, `REQ-036` through `REQ-042`;
`SUCCESS-004`, `SUCCESS-006`, `SUCCESS-007`.

Plan: [ED-M07B](plan/ED-M07B-safe-content-publication-and-compatibility.md).

The [2026-09-11 UI/source review](validation/ED-M07B-ux-review.md) is recorded;
the [workflow contract](lld/content-cooking-workflows.md) adds before/after
browsing, picking, incremental execution and recovery. The user directed
implementation of the revised plan on 2026-09-11; D1 is accepted and reconciled.

- [x] 07B.0 settle trigger policy and reconcile PRD/LLDs after the UX review.
- [ ] 07B.1 coherent saved snapshots, dependency freshness, incremental reuse,
      coalescing and serialized project cooks.
      Shared writer, snapshot capture and scene/material save-gate adapters are
      implemented. Current Scene now uses the same saved-file path as other scene
      scopes; scene/material preparation rechecks dirty state and acknowledged
      hashes under save leases. Saved scene/geometry/material dependency discovery
      now includes geometry buffers and import settings, tracks absent settings,
      and collects independent input errors by asset. Source/native references
      resolve to the same authored descriptor. ContentPipeline tests pass 127/127,
      including native first-cook dependencies and discovery/capture races.
      Explicit asset/folder/scene/project cooks now capture the complete saved
      source set before generation, use private native inputs, carry the qualified
      producer fingerprint, and report later source/dirty changes. Geometry media
      references resolve to captured copies. ContentPipeline 137/137 passes.
      Material helpers use the same pipeline and preserve newer edits/saves as
      stale. Persisted product fingerprints and validated output hashes now reuse
      current products across service recreation. Native tests cover unchanged
      cooks with no workers/rewrites, one changed material, corrupt descriptors,
      failed-run provenance, warning retention, and every built-in. ContentPipeline
      166/166 passes. Saved-source scheduling coalesces revisions, resumes blocked
      scopes, and supports session pause. Consumer completion is awaited before
      releasing the writer. Imported-source closure, priorities/observer
      cancellation and the remaining Import/demand triggers remain.
- [ ] 07B.2 staged validation, preview pause, publication, rollback/recovery and leases.
      Native execution now receives explicit input/output/operation paths. A real
      native cook from private inputs into staging preserves published files;
      private-root seeding now preserves unrelated bytes/timestamps and records
      complete root identities. Output leases exclude competing publishers and
      live readers, with atomic runtime-reader handoff and exited-reader cleanup.
      The transaction core journals all affected roots and both publication/cache
      metadata, restores failed replacements, and verifies recovery material before
      changing files. Combined publication/staging/lease/worker regressions pass
      69/69, including persisted interruption states between directory moves and
      journal updates. Native cooks now write to private staging and commit roots,
      receipt and provenance through the transaction. Recovery skips live operation
      leases and restores abandoned journals; mounting holds a read lease through
      verification. ContentPipeline 221/221 passes, including native cook followed
      by failed preview restoration and receipt repair. Fifteen actual publisher
      process-termination cases now cover prepared state, individual root moves,
      metadata writes and completed commit; combined process/worker/lease tests
      pass 39/39. Runtime now awaits native load drain, retains readers through
      refresh and teardown, and refreshes existing scene bindings. Runtime 90/90
      and focused packaged UI 14/14 pass, including same-key material colour
      replacement without scene reload. The user confirmed recook/tab switching
      and leak-free shutdown under native debugging. Workspace publication uses this boundary;
      the remaining project-lifetime and recovery workflows are still open.
- [ ] 07B.3 required PostProcess/Background native descriptor/load mappings.
- [ ] 07B.4 matched artifacts, qualified static/scalar import and clean-copy reproduction.
      The normal Interop build now records its native SDK in assembly metadata.
      Startup checks SDK compatibility independently of managed/UI edits; cooking
      checks its own tools and schemas. Artifact leases follow native ownership
      and worker drain. Qualification manifests, promotion commands and the
      separately built startup probe are removed. Core 82/82, Runtime 83/83 and
      ContentPipeline 145/145 pass, including native mismatch, managed-edit
      independence, startup without Interop and installed cooker/schema preflight.
      Supported import and clean-copy reproduction remain open.
- [ ] 07B.5a-c shared status, correct browser navigation/details, and consistent
      authored/built-in/cooked presentation and typed picking.
- [ ] 07B.5d-e approved triggers, safe import/save/cook flows, dockable Cooking
      progress/recovery, useful Inspect/Validate and accessible command layouts.
      [Cooking panel](lld/cooking-panel.md) run history, scoped output, grouped
      issues, cancellation, explicit save/resume, and Aerial Start navigation and
      bounds are implemented. ContentPipeline 107/107, MaterialEditor 44/44, and
      focused packaged controls 7/7 pass. Actual dock activation now passes eight
      Cooking UI regressions and 192 Docking tests. Changed material/scene saves
      now notify the shared automatic scheduler. Fourteen focused packaged UI tests
      cover dock activation, automatic history, stale-banner removal and native
      material refresh. Remaining Import/demand triggers and workflow gates are open.
- [ ] 07B.5f complete before/during/after workflow and user validation journeys.
- [ ] 07B.5g compact single/multi-node inspector with component filtering and
      deselect/All behavior, preserving existing edit/history contracts.
      Compact type selection, the All icon, original property rows, full-text
      tooltips and consistent icons are implemented. Hidden sections retain field
      errors and pending results, and recycled controls detach from their models.
      Packaged UI 196/196 passes at 175% scaling, including numeric/text/color edit
      boundaries and mixed-target source corrections. Remaining scaling walkthroughs are open.
- [x] 07B.6 owned native worker/descendant termination and I/O drain (#8).
      19 new worker/manifest cases pass; ContentPipeline is 64/64. Source/test
      diagnostic collection and cleanup passed for changed files. Evidence:
      `artifacts/m07b-worker-final-tests.log`; integration with staging is 07B.1/2.
- [ ] 07B.7 single procedural authority for the full engine generator catalog
      and aliases; Cylinder and every picker choice pass scene/project cook (#11).
        Native catalog projections retain all identities, aliases, recipe schemas,
        cooked mappings and LOD/submesh metadata. Projection/descriptor tests 19/19,
        identity tests 11/11 and managed assets 89/89 pass. Browser/picker wiring and
        removal of their existing lists remain open.

Startup correction: workspace commands now wait for native module registration.
Runtime 75/75; the user confirmed project opening without the access violation.

### ED-M08 - Runtime Parity And Standalone Validation

Status: `blocked`

Trace: `GOAL-001`, `GOAL-002`, `GOAL-003`, `GOAL-006`; `REQ-018`,
`REQ-019`, `REQ-022`, `REQ-023`, `REQ-024`, `REQ-026`, `REQ-030`,
`REQ-037`; `SUCCESS-001`, `SUCCESS-003`, `SUCCESS-004`, `SUCCESS-006`

Outcome: the qualified PRD fixture and field suite prove exact saved/published
content in embedded preview and standalone runtime under controlled comparison.

- [ ] ED-M07A and ED-M07B gates pass, and ED-M02 supported-viewport evidence
      is recorded. ED-M09 tools are not a prerequisite.
- [ ] Required LLDs are reviewed:
      `standalone-runtime-validation`, `live-engine-sync`,
      `runtime-integration`, `content-pipeline`, `environment-authoring`.
- [x] Detailed `ED-M08` implementation plan exists; product validation remains pending.
- [ ] Embedded preview renders the qualified PRD fixture and field cases.
- [ ] The exact published project output loads through the ED-M08 request contract.
- [ ] Expected geometry, material, camera, directional light, atmosphere,
      exposure, and tone mapping are present within documented tolerance.
- [ ] Failures classify cooked output, asset resolution, runtime load, sync, or
      parity mismatch.

Exit evidence required:

- [ ] One `ED-M08` validation ledger row records embedded preview, cooked
      output, mount, standalone load, and parity result for the minimum slice.

### ED-M09 - Viewport Authoring Tools And Overlays

Status: `planned`

Trace: `GOAL-003`; `REQ-027`, `REQ-028`, `REQ-029`, `REQ-030`,
`REQ-031`, `REQ-032`, `REQ-033`, `REQ-034`, `REQ-035`; `SUCCESS-003`,
`SUCCESS-005`, `SUCCESS-008`

Outcome: camera navigation, frame selected/all, selection highlight, transform
gizmos, node icons, and overlays are usable in supported viewport layouts.

- [ ] Required LLDs are reviewed:
      `viewport-and-tools`, `documents-and-commands`, `scene-explorer`,
      `runtime-integration`.
- [x] Detailed `ED-M09` implementation plan exists; product validation remains pending.
- [ ] Camera navigation and frame selected/all are usable.
- [ ] Selection highlight is implemented.
- [ ] Transform gizmo UX mutates through commands.
- [ ] Non-geometry node icons exist for cameras/lights.
- [ ] Supported viewport layouts remain stable with overlays enabled.

Exit evidence required:

- [ ] One `ED-M09` validation ledger row records viewport UX coverage across
      supported viewport layouts.

### ED-M10 - V0.1 Acceptance

Status: `planned`

Trace: all V0.1 `GOAL-XXX`, `REQ-XXX`, and `SUCCESS-XXX` IDs

Outcome: the full PRD V0.1 workflow completes end-to-end without manual repair.

- [ ] All V0.1 LLDs are reviewed or explicitly marked as not gating V0.1.
- [ ] Any residual open issue is resolved or moved out of V0.1 by PRD/design
      decision.
- [ ] Start from Project Browser.
- [ ] Open/create a project and scene.
- [ ] Author full V0.1 geometry, material, camera, light, and environment
      content.
- [ ] Preview live in the embedded Vortex viewport.
- [ ] Save and reopen without manual repair.
- [ ] Generate descriptors/manifests.
- [ ] Cook, inspect, refresh, and mount output.
- [ ] Load cooked scene in standalone runtime.
- [ ] Record final `SUCCESS-XXX` validation evidence.

Exit evidence required:

- [ ] One `ED-M10` validation ledger row records the full V0.1 workflow result
      and links to supporting build/run artifacts.

## 4. Detailed Plan Tracker

Detailed plans live under [plan/](./plan/). Each active milestone has one
`ED-Mxx-...md` plan with numbered implementation slices. The status column below
tracks the owning milestone, not whether the plan text has been reorganized.
Cross-milestone contributions and unresolved scope questions are recorded in
the owning plans. Reorganizing those plans does not complete or defer their
requirements. Existing validation evidence remains scoped to the workflows
recorded in section 5.

| Plan | Milestone | Status | Next Action |
| --- | --- | --- | --- |
| [ED-M01-project-browser-workspace-activation.md](plan/ED-M01-project-browser-workspace-activation.md) | `ED-M01` | `validated` | No further action. |
| [ED-M02-live-viewport-stabilization.md](plan/ED-M02-live-viewport-stabilization.md) | `ED-M02` | `landed` | Validate or record the supported single viewport result only; multi-viewport remains deferred and is not an ED-M02 gate. |
| [ED-M03-authoring-foundation.md](plan/ED-M03-authoring-foundation.md) | `ED-M03` | `validated` | No further action for ED-M03; DynamicTree rename commit hook remains deferred. |
| [ED-M04-scene-editing-ux-component-inspectors.md](plan/ED-M04-scene-editing-ux-component-inspectors.md) | `ED-M04` | `landed` | No new execution under M04. Source-identified omissions and missing evidence execute in ED-M07A/07B. |
| [ED-M05-scalar-material-authoring.md](plan/ED-M05-scalar-material-authoring.md) | `ED-M05` | `validated` | No further action for ED-M05. |
| [ED-M06-asset-identity-content-browser.md](plan/ED-M06-asset-identity-content-browser.md) | `ED-M06` | `validated` | No further action for ED-M06. |
| [ED-M06A-game-project-layout-and-template-standardization.md](plan/ED-M06A-game-project-layout-and-template-standardization.md) | `ED-M06A` | `validated` | No further action for ED-M06A. |
| [ED-M07-content-pipeline-and-cooking.md](plan/ED-M07-content-pipeline-and-cooking.md) | `ED-M07` | `validated` | Recorded validation is retained. Section 11 closes UI scope decisions; new publication/mapping guarantees execute in ED-M07B. |
| [ED-M07A-authoring-integrity-and-runtime-convergence.md](plan/ED-M07A-authoring-integrity-and-runtime-convergence.md) | `ED-M07A` | `validated` | All automated and user-confirmed viewport gates pass. |
| [ED-M07B-safe-content-publication-and-compatibility.md](plan/ED-M07B-safe-content-publication-and-compatibility.md) | `ED-M07B` | `in_progress` | UI/source review and D1 complete. Implement content workflows, incremental cooking, safe publication, mappings and qualification. |
| [ED-M08-runtime-parity-and-standalone-validation.md](plan/ED-M08-runtime-parity-and-standalone-validation.md) | `ED-M08` | `blocked` | Requires 07A/07B and ED-M02 evidence; then execute exact-request parity. |
| [ED-M09-viewport-authoring-tools.md](plan/ED-M09-viewport-authoring-tools.md) | `ED-M09` | `planned` | Execute the decided navigation/picking/tool contract after M08. |
| [ED-M10-v01-release-qualification.md](plan/ED-M10-v01-release-qualification.md) | `ED-M10` | `planned` | Qualify the matched build and selected small-project workload. |
| DynamicTree rename commit hook | `post-ED-M03` | `deferred` | Replace ED-M03's loaded-adapter label-change bridge with a first-class DynamicTree rename commit hook/override; this must not block ED-M03 closure. |

## 5. Validation Ledger

One row per milestone. Existing rows retain their original evidence and
qualification limits; their historical wording is not a current execution
instruction. Current gap work and new proof belong to ED-M07A/07B and later
rows. Do not add running notes; update the owning plan instead.

| Milestone | Status | Date | Evidence |
| --- | --- | --- | --- |
| `ED-M00` | `validated` | 2026-04-26 | Design package approved: README, RULES, PROJECT-LAYOUT, PRD, ARCHITECTURE, DESIGN, PLAN, LLD index/scaffolds, plan index, and status ledger are accepted as the V0.1 planning baseline. |
| `ED-M01` | `validated` | 2026-04-26 | User validated Project Browser startup, recent/open/create/invalid project behavior, workspace activation, visible operation results, and best-effort workspace/content-browser restoration after ED-M01 implementation. |
| `ED-M02` | `pending` | - | Not validated; pending scope is the supported single live viewport only. Multi-viewport stability remains deferred. |
| `ED-M03` | `validated` | 2026-04-27 | User manually validated ED-M03 authoring foundation: quick-add, selection, dirty/save, rename undo/redo including in-place edit, save/reopen, and visible diagnostics expectations. Targeted test run passed 112/112 across Oxygen.Managed.Core.Tests, Oxygen.Editor.World.Tests, and Oxygen.Editor.WorldEditor.SceneExplorer.Tests. DynamicTree rename commit hook is deferred and non-blocking. |
| `ED-M04` | `landed` | 2026-04-28 | Reopened after ED-M07 because accepted `property-inspector.md` and `environment-authoring.md` gates were overclaimed. All non-deferred gates from those LLDs must be implemented and validated before ED-M08 runtime parity; the only deferred feature is multi-viewport. Earlier manual validation remains partial evidence for Transform, Geometry asset switching, material slot persistence UI, camera/light/default inspector behavior, Geometry deletion, and save/reopen behavior. |
| `ED-M05` | `validated` | 2026-04-28 | User manually validated scalar material authoring against the corrected ED-M06A project layout: material creation under `Content/Materials`, editor scalar/color editing with shared controls, save/reopen behavior, material picker refresh/filtering, geometry assignment by asset identity, asset URI/GUID identity display and copy affordances, and minimum cook/catalog behavior. |
| `ED-M06` | `validated` | 2026-04-28 | User manually validated asset identity and Content Browser behavior after ED-M06A: folder navigation refreshes rows, material picker shows one project material entry per material instead of arbitrary files, descriptor/cooked state badges remain user-facing identity facts, new material saves refresh browser/picker state without restart, and authored data remains under the accepted `Content` layout. |
| `ED-M06A` | `validated` | 2026-04-28 | User manually validated ED-M06A project layout and template standardization after starter-scene JSON fix: create project from template, starter scene load, new scene/material authored paths under `Content`, Content Browser folder navigation, Material Picker filtering, and authoring target resolution. MSBuild passed for Oxygen.Editor.App and focused ProjectBrowser tests; targeted VSTest run passed 96/96 across Projects, ContentBrowser, and ProjectBrowser assemblies before the final starter-scene regression test, then ProjectBrowser starter-scene regression passed 3/3. |
| `ED-M07` | `validated` | 2026-04-28 | User manually validated ED-M07 content pipeline and cooking: cook project, cook folder, cook selected asset, and cook current scene workflows; inspect cooked output shows visible summary feedback; validate cooked output shows visible feedback and drives validated cooked-root refresh; cooked mount root displays cooked files and persists/remounts from `Project.oxy`; material, scene, and cooked catalog refresh paths update without restart; failures produce visible operation results. Focused automated coverage included ContentPipeline tests 40/40 and ContentBrowser tests 62/62; functional ImportTool dry-run and actual temp Vortex import succeeded during implementation validation. |
| `ED-M07A` | `validated` | 2026-09-11 | Packaged controls/native 137/137; Runtime 71/71; SceneExplorer 161/161; World 67/67; Managed.Assets 89/89. User confirmed combined XYZ rotations, Cube/Sphere and cooked/None/Default material changes, and coupled sun controls through Undo/Redo and Save/reopen. Background presentation was confirmed earlier. All gates pass; see the [field/workflow results](validation/ED-M07A-field-workflows.md). |
| `ED-M07B` | `in_progress` | 2026-09-13 | ContentPipeline 221/221; publisher/worker/lease process tests 39/39; Runtime 90/90; SceneExplorer 167/167; MaterialEditor 48/48; packaged UI 196/196, including component-filter feedback, text/drag/color boundaries and mixed-target corrections. Same-key material refresh preserves scene identity and history. User confirmed recook/tab switching and leak-free shutdown under native debugging. Remaining slice gates stay open above. |
| `ED-M08` | `pending` | - | Not validated. |
| `ED-M09` | `pending` | - | Not validated. |
| `ED-M10` | `pending` | - | Not validated. |

## 6. Decision And Blocker Register

Use this table only for decisions or blockers that stop a named milestone.
Do not use it to list unfinished implementation work.

| ID | Affects | Status | Decision/Blocker | Required Action |
| --- | --- | --- | --- | --- |
| `DB-001` | `ED-M00` | `closed` | Top-level `PLAN.md` review feedback was applied and accepted. | No further action. |
| `DB-002` | `ED-M02` | `closed` | Multi-viewport stability is deferred out of ED-M02; V0.1 proceeds on the supported single live viewport only. | Do not reopen multi-viewport as an ED-M02 validation or implementation gate. |
| `DB-003` | `ED-M03` | `closed` | DynamicTree in-place rename has no pre-mutation commit hook today, so ED-M03 uses a loaded-adapter label-change bridge to preserve undo/redo and persistence. | Proper DynamicTree rename commit hook is deferred after ED-M03 and does not block milestone validation. |
| `DB-004` | `ED-M05`, `ED-M06`, `ED-M07` | `closed` | Project layout and predefined templates were redefined by the accepted game-project filesystem architecture/LLD. ED-M06A is validated, and ED-M05/ED-M06 validation is closed against the corrected layout. | Proceed to ED-M07 planning/implementation after recording any remaining ED-M02 viewport validation separately. |
| `DB-005` | `ED-M07` | `closed` | ED-M07.1 audit selected: keep ED-M05 material cook on managed `ImportService.ImportAsync` because it already invokes `LooseCookedBuildService`; use native import manifest schema validation; use native `Oxygen.Cooker` through a bounded ImportTool adapter for scene/folder/project import until an in-proc Interop wrapper is added; inspect/validate use native loose cooked `Inspection`/`ValidateRoot` semantics with one synthesized diagnostic on failure; mount layout is `.cooked/<Mount>` with virtual root `/<Mount>`; scene descriptor names normalize from file stems; environment output is complete defaults plus supported overrides or omitted with warnings; procedural geometry descriptors are derived under `.pipeline/Geometry`; every direct unvalidated `AssetsCookedMessage` publisher is removed or rerouted. | ED-M07.2+ coding may proceed against these decisions; do not double-call `LooseCookedBuildService`, do not reintroduce save-time cook or unvalidated mount refresh, and keep multi-viewport deferred. |
