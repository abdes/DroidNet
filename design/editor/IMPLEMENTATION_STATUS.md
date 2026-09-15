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

1. Finish the remaining ED-M02 single-viewport surface/resize evidence.
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

- [x] One-pane viewport layout is validated after a fresh Debug editor launch;
      repeated user restart/viewport checks and the live demand-cooking workflow
      confirm the central scene viewport is usable.
- [ ] Complete the supported single-viewport surface/resize qualification.
      Rendered scene content and live material/background updates are confirmed;
      the explicit window/dock-resize sweep remains to be recorded.
- [x] Engine FPS/logging controls apply to the native session and report scoped
      rejection after shutdown. Both packaged RuntimeSettingControls cases pass
      in the 2026-09-13 Debug UI run.

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
  - [x] Scene, material and geometry requests capture saved inputs and dependencies
        under document read gates, preserve later edits, and use private native inputs.
  - [x] Persisted source/dependency fingerprints and verified output hashes support
        incremental reuse, corruption repair and current no-op cooks without workers,
        output replacement or preview pause.
  - [x] One writer owns queued work, project lifetimes and native drain. Pending
        scopes coalesce with independent caller cancellation; explicit/demand work
        precedes background saves. Pause/resume and later saved revisions are covered.
        Native admission reserves capacity before import-thread dispatch, and batch
        submission/completion bookkeeping is synchronized. Importer and batch
        regressions pass 57/57 in Debug and Release, including a 1,000-material
        cook; both updated SDKs are installed.
  - [x] Imported output ownership resolves from saved settings or verified
        provenance. Asset, output-folder and consuming-scene cases regenerate
        from retained sources in a clean project. Exact requested outputs are
        required before publication; ambiguous owners and authored descriptor
        collisions fail before native work. Status follows source changes without
        starting a native worker or buffering entire model dependencies.
        Bulk reads evaluate each shared source once and retain per-output
        availability; a native model with 258 outputs covers valid and missing
        output requests in the same read.
  - [x] Cross-mount project dependencies cook in order, retaining same-mount
        batching and reusing completed work. Scene/geometry manifests use ordered
        native lookup roots. Reused roots remain leased through native work and
        failed termination drain; interleaved mount stages retain all provenance.
        Native tests pass 52/52 in Debug and Release; installed-SDK cases cover
        new/reused imported geometry and material/geometry/scene chains.
  - [x] Nested material, geometry and scene descriptor paths survive native cooking.
        Per-job layout retains the shared container and default resource folders.
        Same-named materials in separate folders have distinct native keys;
        dependency resolution and incremental reuse pass. Native manifest/batch
        cases pass 23/23 in both configurations and ContentPipeline passes 427/427.
  - [x] Scenes consume geometry from declared cooked libraries without authored
        descriptors. Selected native keys, types and container hashes participate
        in snapshots, provenance and status. Library updates and saved priority
        changes invalidate consumers; missing/corrupt inputs report scoped errors
        and preserve published output. Readers survive failed native termination.
  - [x] Native scene/geometry lookup preserves source priority before index
        publication. Newly written project descriptors beat lower-priority
        indexed libraries; explicitly higher libraries still win. Scene and
        material key regressions and related native suites pass 43/43 in both
        Debug and Release, with both SDK configurations installed.
  - [ ] Complete the remaining foreign dependency/typed-use qualification matrix
        in 07B.1 and 07B.4.
  - [x] Project-only uncooked dependencies resolve through batched native keys and
        a derived candidate-path cache. First scene cook includes the material
        without mounting its producer library. Background preparation creates no
        cooking run; unchanged status starts no workers. Rename/removal, corrupt
        cache and failed-drain regressions pass. Native lookup passes four cases
        in both configurations, including 1,001 paths; editor integration and
        lifecycle cases pass 11/11.
  - [x] Cache native Inspector dependency reports by verified library contents.
        Cooked geometry captures a material supplied by another library; changing
        that material invalidates its consuming scene. Native keys retain their
        binary identity across report/index representations. Corrupt cache entries
        become misses, status reads start no native processes, and failed worker
        termination retains readers and the project writer until drain. Native
        loader suites pass 39/39 in Debug and Release. Project material overrides
        reached through indexed library dependencies now enter saved-input capture
        and cooking; saved priority selects the winner, and shadowed versions do
        not invalidate consumers. Both orders and status transitions pass; the
        ContentPipeline suite passes 444/444. Embedded native keys remain distinct
        from authored path references: a same-path project material with a different
        key cannot replace a cooked mesh dependency. Per-consumer provenance and
        two-scene freshness regressions preserve both identities independently.
- [ ] 07B.2 staged validation, preview pause, publication, rollback/recovery and leases.
  - [x] Private-root seeding preserves unrelated content; whole-root validation
        precedes publication. Output leases exclude competing readers and writers.
        Contended inspections and publication wait without throwing first-chance
        exceptions; reader exclusion and cancellation regressions pass. Staging,
        recovery and mount registration also wait through short gate contention;
        nested verification reuses its existing lease. The reproduced catalog/
        staging race, three registration modes and nested-reader deadlock
        regressions pass with the full ContentPipeline suite (449/449).
  - [x] Journaled root/receipt/provenance replacement and rollback/recovery cover
        failed publication, metadata writes, abandoned operations and corrupt receipts.
        Fifteen publisher-termination cases and the 39-case process/worker/lease
        suite exercise interruption boundaries.
  - [x] Publication can install a privately captured retained source alongside
        cooked roots in the same journal. Source baseline checks, commit,
        cancellation, rollback and persisted recovery pass; later source edits
        remain ordinary authoring changes. Reviewed replacement requests use
        this transaction after native validation.
  - [x] Runtime publication awaits pending loads, retains readers through refresh
        and teardown, and refreshes current material bindings without scene reload.
        The user confirmed recook/tab switching and leak-free native-debug shutdown.
  - [x] Production workspace publication updates two existing material consumers
        through automatic Save cooking and explicit asset/folder/scene/project
        scopes. Material creation, pre-cook discovery, typed-picker readiness,
        current catalog status and unchanged scene history pass 5/5 packaged
        native integration cases. Automatic publication preserves the unsaved
        scene. [Workspace publication evidence](validation/ED-M07B-workspace-publication.md).
  - [x] Queued workspace publication respects Undo, cleared material assignments,
        removed geometry and replacement scene lifetimes. Cancellation and source
        conflict retain the prior native material until recovery. Unchanged
        asset/folder/scene/project cooks preserve native content revision and
        published file timestamps. These ten additional native integration cases
        pass alongside the five creation/shared-refresh cases.
  - [ ] Finish the project-lifetime/recovery and complete cross-trigger integration
        matrix, including the remaining import workflows.
  - [x] The UI Automation exception flood was isolated to the computer-use helper;
        the user confirmed it disappears with the helper stopped.
  - [x] Close the reported native scene-switch shadow-barrier failure. The native
        registry forgets backend state before retiring a resource; its cache
        regression fails before the fix and passes afterward, with 121 native
        cases passing in each configuration. Direct scene-replacement stress and
        real document-host Main/Inspect/Main transitions pass 30 cycles at both
        60 and 10 FPS. The latter explicitly enables D3D12 debug/validation and
        conventional shadows, checks retired view IDs and zero/one surface
        ownership at every activation, and reports no engine-loop failure.
        [Document transition evidence](validation/ED-M07B-document-transitions.md).
- [x] 07B.3 required PostProcess/Background native descriptor/load mappings.
      Scene v4 carries all 23 post-process fields and display-background RGB through
      saved JSON, descriptors, native cooking/loading and hydration. Debug/Release
      SDKs and Interop are updated; native descriptor/loader/hydration suites pass.
      Managed descriptor cases pass 16/16 and focused PakGen cases pass 43/43.
      Eight full-PakGen failures were reproduced on the unchanged baseline.
      Standalone visual parity remains ED-M08.
- [ ] 07B.4 matched artifacts, qualified static/scalar import and clean-copy reproduction.
  - [x] Normal Interop builds record native SDK identity. Startup and cooking check
        their compatibility boundaries independently of unrelated managed edits;
        artifact leases follow native ownership and drain. No qualification manifest,
        promotion command or separately built startup probe remains.
        Core 82/82 and the installed-cooker/schema and mismatch cases pass.
  - [x] Native discovery and source retention capture a coherent bundle under the
        project writer and saved-input gates. A private primary copy and captured
        hashes preserve relative paths through changed-discovery retries. Sources
        remain under `Content/SourceMedia/DCC` in the declared Content mount without
        overwriting an existing bundle. All 23 retention cases pass, including
        native glTF/FBX, missing/changed inputs, cancellation, project closure and
        retaining the copy/operation marker through failed native termination.
  - [x] Native manifest scene imports can require static geometry and scalar
        materials. Both adapters reject unsupported source features and ambiguous
        FBX coordinate metadata before emission; ordinary imports retain their
        existing policy. Fixtures cover accepted sources, excluded features and
        preservation of REQ-009 perspective cameras and directional lights.
  - [x] Native source inspection exposes format, coordinate conversion, source
        counts and external buffer paths without cooking. The managed adapter
        validates the shared schema and preserves worker/artifact ownership.
        Combined native suites pass 21/21 in Debug and Release; managed source
        cases pass 9/9, including both installed native formats,
        and compatibility cases pass 10/10. Retained settings and workflow
        integration are recorded below.
  - [x] Versioned retained native settings record the source bundle, output
        namespace and explicit import policies without rewriting legacy sidecars.
        Configured models enter asset/folder/project cooking through the existing
        snapshot, incremental planner, staging and journaled publication. Native
        reports establish actual emitted files and preserve warnings; collisions
        and identity-changing reimports leave published output untouched.
        Native-backed cases cover glTF/FBX, mixed project scopes, changed dependency
        layouts, no-worker reuse and clean-copy logical identities.
  - [x] Explicit import retains source, saves settings and publishes through one
        Cooking run. Retry uses retained source after settings/cooking failure;
        Save and resume continues the same run without copying the original again.
        Native-backed tests cover those paths, in-project source identity and
        rejecting a changed reviewed project. Completed runs list actual named
        outputs and their types beside the retained source.
  - [x] Reviewed retained-source replacement cooks private incoming bytes under
        the existing source identity, policies and output namespace. Native glTF
        cases cover external buffers, missing-source repair, changed baselines,
        authored conflicts, late unsaved edits, native failure and publication
        rollback. Retry uses the captured candidate after external files disappear.
  - [x] glTF external buffers, GLB binary chunks and FBX load expected geometry,
        normals, winding, bounds, hierarchy, camera and scalar material/light
        values across unit/handedness variations and clean source copies. The
        camera-unit and retained-parent defects are fixed. Related native suites
        pass 23/23 in both configurations; 16 final profiles validate both source
        copies. [Numeric import evidence](validation/ED-M07B-import-values.md).
  - [ ] Complete source/output catalog and typed-use integration and the remaining
        combined import qualification journeys.
- [ ] 07B.5a-c shared status, correct browser navigation/details, and consistent
      authored/built-in/cooked presentation and typed picking.
  - [x] Shared input/dependency and publication facts drive browser, material editor
        and both pickers. Cook/document events update status; native root and asset
        acknowledgments report preview availability separately. Current request
        generations reject obsolete success/failure callbacks.
        Catalog changes now refresh cooked-library metadata in the background
        without blocking rows or creating cooking runs. Completed cache writes
        refresh status; unchanged catalog revisions do not repeat inspection.
  - [x] Material editor status is a compact header chip with a next-action tooltip.
        Live updates, Save/recook/reopen, light/dark rendering and color history pass.
  - [x] Verified built-in copies retain engine origin, native names and Built-in
        labels, and expose no standalone Cook action. Independent authored assets
        remain distinct. Geometry picker rows retain identity and focus on updates.
  - [x] Valid uncooked geometry is assignable. Geometry/material picker assignments
        request saved dependencies after the undoable edit; Undo, selection changes,
        node removal, document closure and late callbacks retire obsolete observers.
  - [x] Folder scope changes publish one complete snapshot. Completed navigation
        updates the displayed scope; stale rows cannot become selection/action
        targets, and delayed folder lookup cannot restore an obsolete selection.
        List/tile and mount-persistence regressions pass on the UI dispatcher.
        Multi-folder scopes now use the router's query parser after navigation,
        preserving the full selection when repeated parameters are serialized
        as one multi-value parameter.
        Folder selection updates command state synchronously without a diagnostic
        catalog query; combined browser/Cooking tests cover the former unhandled
        callback failure.
  - [x] The selected asset and visible highlight follow list/tile switches. Catalog
        reordering preserves selection and keyboard focus. Filtered-out rows and
        unloaded layouts cannot restore or invoke an obsolete selection.
        Routed layouts have outlet-owned lifetimes; the actual-router regression
        verifies repeated tiles/list/tiles replacement without reusing disposed
        models or rescanning the catalog.
  - [x] Search and Type/Status filters share one session query across list and tiles.
        Live status changes re-evaluate results without rescanning or cooking.
        Empty results expose reset, and empty Cooked folders link to source content.
        Light/dark rendered controls and query semantics pass.
  - [x] Proven cooked companions are grouped with their logical built-ins without
        merging same-named authored assets or generator aliases. Existing cooked
        references remain resolvable. The user confirmed that Default appears once
        when filtering Vortex for materials and scenes.
  - [x] List/tile asset tooltips show full names/locations, source or built-in
        ownership and known cooked outputs. Information follows publication without
        scanning, cooking or changing selection. Light/dark rendered cases pass.
  - [x] Cooked catalog snapshots retain native keys, container identities, types,
        descriptor locations and revisions. Browser type/location follows those
        records even when filenames differ; invalid descriptor paths remain
        unselectable. Catalog, browser and inspection adapter regressions pass.
  - [x] Local cooked-library discovery, saved priority, winning-source details and
        runtime mounting are connected. New libraries default below project output
        and above older libraries; explicit reordering is preserved. The user
        confirmed the browser mounting and inspection workflows.
  - [x] Project manifests persist that priority order, retain explicit overrides when
        adding libraries, and reject invalid order entries. Confirmed configuration
        saves use atomic replacement and reject externally changed baselines;
        project persistence tests pass 65/65.
  - [x] Ordered mount preparation validates library descriptor integrity and retains
        file readers through native ownership. Project changes keep the cook writer
        until refresh finishes, including context replacement. The 43-case focused
        mount/coordinator suite covers priority, cancellation and reader release.
  - [x] Model sources show Not imported before configuration, then share cooking
        status with their actual named outputs. Tooltips list output types and
        retained origins; contextual Show source/Reimport and selected-asset
        inspection use that relationship. Missing catalog/output files retain
        previously published names without inventing subassets. Foreign library
        copies stay read-only. Production native import/catalog and rendered
        browser/picker/menu cases pass in light/dark themes.
  - [ ] Finish the remaining combined browser navigation/query qualification
        journeys, including the 1,000-entry workload.
  - [x] Measure rendered list/tile response with 1,000 authored inputs and a live
        100-node viewport. Release cold loading is 1.98/1.73 s; search p95 is
        18.42/19.09 ms. The native cook, loaded geometry, current statuses and
        mount acknowledgment pass. [Fixture, profile and evidence](validation/ED-M07B-browser-workload.md)
        record the scope of this CPU/UI measurement.
  - [ ] Finish cooked-only typed-use qualification and the complete assignment,
        publication, cancellation and Save/reopen matrix.
- [ ] 07B.5d-e approved triggers, safe import/save/cook flows, dockable Cooking
      progress/recovery, useful Inspect/Validate and accessible command layouts.
  - [x] The dockable Cooking panel owns session runs and scoped output, grouped
        issues, selected-run cancellation, inline Save listed & Cook, Retry and
        property navigation. Output/Assets use the shared details scroller.
  - [x] Explicit cook actions reveal Cooking at submission, required input and queue
        completion. Automatic work is visible without stealing focus; Show all
        reveals routine successes. Stale browser cook banners are removed.
  - [x] Changed material/scene Save and Save Copy trigger the shared scheduler;
        failed saves do not. Unchanged saves can resume blocked cooks without
        submitting another job.
  - [x] Scene activation and accepted picker assignments request only required saved
        geometry/material assets. The user confirmed that paused cooking, material
        creation/assignment and Resume update the viewport without reloading or
        saving the consuming scene.
  - [x] Aerial Start validation and property navigation preserve invalid saved data,
        enforce the finite minimum-zero bound and permit corrected publication.
  - [x] Explicit Inspect/Validate opens a read-only document tab with scoped assets,
        actual root files, verified source/dependency relationships and integrity
        issues. Source links reveal the requested browser row; report refresh and
        cancellation preserve reader ownership. Cooking offers Inspect inline in
        the selected completed run's header. Targeted native-backed and rendered
        document/router/action cases pass.
  - [x] Source and source-folder inspection follow imported output across mounts.
        Configured uncooked models report their declared destination without
        invoking native inspection on another mount; source associations use
        committed, byte-verified provenance. Native-backed and read-only cases pass.
  - [x] Opening a cooked-only asset routes to read-only inspection. Built-ins with
        no project copy show engine ownership and asset information without a cook
        or validation request. Refresh discovers verified project copies. Packaged
        invocation and rendered report cases cover this behavior.
  - [x] Indexed local libraries expose their own read-only contents and effective
        assignment source. Confirmed mount changes apply and save immediately with
        rollback before commit; saved priority follows native last-mounted-wins.
        New libraries default below project output and above older libraries.
        Packaged tests cover the Cooked menu, tree restoration/selection, source
        navigation, mount transactions and priority controls. Native material
        priority changes and Save/reopen pass without reloading or authoring edits.
  - [x] Import reviews glTF/GLB/FBX source, name and authoring destination through
        the existing dialog service, then submits the shared Cooking operation.
        Raw source invocation imports in place; configured source offers Reimport.
        Validation stays inline, Cancel starts no cook, and source-name collisions
        offer another name or an explicit reviewed replacement. Dialog-model and
        packaged command/rendering tests cover acceptance, cancellation, picker
        failures, retained-source Retry and light/dark presentation.
  - [x] The import dialog identifies an existing retained source before enabling
        Replace and import. It shows the preserved destination, resets the choice
        when the review changes, and reports unowned files inline. Normal import,
        replacement and cancellation controls pass in light/dark packaged checks.
  - [x] Successful model cooks offer Show imported assets beside Inspect in the
        selected-run header. Explicit navigation reveals available outputs across
        their folders, opens the Cooked tree when needed and preserves unrelated
        user focus until requested. Source-aware inspection carries the selected
        identity into its document. Combined Cooking/import/browser UI cases pass
        20/20; narrow/wide captures are verified. Revealing imported outputs also
        follows renamed project-output mounts; an authored folder named Cooked
        keeps its ordinary semantics. Routed UI checks pass 5/5 and the Content
        Browser suite passes 134/134 in Release. Library-only Inspect and output
        links now reveal their supplying mount's physical folder without adding
        project output. Combined routed navigation and live native priority checks
        pass 8/8 with the installed Inspector.
  - [x] Import/replacement review, browser list/tile query controls and material
        status/actions pass at 100%, 150% and 200% XAML scale in both themes.
        Actual Tab input traverses import fields; UI edits validate in place.
        The narrow material header preserves title/status and all four actions.
        Packaged Release passes 33/33 with reviewed captures and clean diagnostics;
        [scale evidence](validation/ED-M07B-scaled-layouts.md).
  - [x] Rendered cook feedback meets 100 ms for all four scopes and unchanged
        repeats on the saved 100-node/1,000-input fixture: final samples are
        13-35 ms. Synchronous cook verification runs off the caller thread;
        coalesced UI snapshots preserve full output/assets and avoid repeated
        collection scans. Pipeline 444/444 and Cooking/UI 27/27 pass; the isolated
        timing run passes 4/4. [Feedback evidence](validation/ED-M07B-cook-feedback.md).
  - [ ] Complete the remaining combined creation/import/reimport command journeys,
        including cooked-library dependency cooking and typed-use qualification.
- [ ] 07B.5f complete before/during/after workflow and user validation journeys.
- [x] 07B.5g compact single/multi-node inspector with component filtering and
      deselect/All behavior, preserving existing edit/history contracts.
  - [x] Compact fixed-type selection, the All icon/tooltip, original property rows,
        clipping tooltips and consistent icons are implemented and tested. Hidden
        sections retain feedback; recycled controls detach; gesture/history cases pass.
  - [x] Packaged Release rendering and control walkthroughs pass at verified
        100%, 150% and 200% XAML scale: compact single/multi-node headers,
        Geometry/All selection, unchanged dirty/history state, clipped-text
        tooltips and scrolling. Cooking also retains accessible recovery and
        expanded Output/Assets. All 18 cases pass;
        [captures and test scope](validation/ED-M07B-scaled-layouts.md).
- [x] 07B.6 owned native worker/descendant termination and I/O drain (#8).
      Cancellation retains ownership until workers and descendants drain, including
      termination failures. Process, stream and manifest cases pass.
- [x] 07B.7 single procedural authority for all eleven engine generator names
      and aliases; Cylinder and every picker choice pass scene/project cook (#11).
      Browser and picker discovery use the native catalog with the approved
      last-known cache and unavailable-preview notice. Native comparison suites
      verify geometry attributes, bounds and default material semantics; packaged
      tests verify preview and Save/reopen for every built-in. Issue #11 is resolved.

Current validation: ContentPipeline 432/432 and the expanded inspection adapter
suite 25/25; Managed.Assets 91/91, Content Browser 133/133, Runtime 95/95,
MaterialEditor 56/56, SceneExplorer 174/174. Packaged UI validated 252 cases: 240 passed in the full run; the four query
expectations corrected for shared initialization and eight new cooked-asset opening
cases pass in the final 32-case browser/inspection/Cooking rerun. The shared
toolbar suite passes 11/11, including conditional command visibility through overflow.
Native asset-request tests pass 28/28. The user confirmed startup without the
access violation and the live demand-cooking workflow described above.

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
| `ED-M02` | `in_progress` | 2026-09-13 | Fresh Debug launches and single-pane rendered content are user-confirmed. Packaged FPS/logging controls verify native application and stopped-runtime diagnostics. Window/dock-resize surface qualification and the consolidated launch/discovery record remain open; multi-viewport stability stays deferred. |
| `ED-M03` | `validated` | 2026-04-27 | User manually validated ED-M03 authoring foundation: quick-add, selection, dirty/save, rename undo/redo including in-place edit, save/reopen, and visible diagnostics expectations. Targeted test run passed 112/112 across Oxygen.Managed.Core.Tests, Oxygen.Editor.World.Tests, and Oxygen.Editor.WorldEditor.SceneExplorer.Tests. DynamicTree rename commit hook is deferred and non-blocking. |
| `ED-M04` | `landed` | 2026-04-28 | Reopened after ED-M07 because accepted `property-inspector.md` and `environment-authoring.md` gates were overclaimed. All non-deferred gates from those LLDs must be implemented and validated before ED-M08 runtime parity; the only deferred feature is multi-viewport. Earlier manual validation remains partial evidence for Transform, Geometry asset switching, material slot persistence UI, camera/light/default inspector behavior, Geometry deletion, and save/reopen behavior. |
| `ED-M05` | `validated` | 2026-04-28 | User manually validated scalar material authoring against the corrected ED-M06A project layout: material creation under `Content/Materials`, editor scalar/color editing with shared controls, save/reopen behavior, material picker refresh/filtering, geometry assignment by asset identity, asset URI/GUID identity display and copy affordances, and minimum cook/catalog behavior. |
| `ED-M06` | `validated` | 2026-04-28 | User manually validated asset identity and Content Browser behavior after ED-M06A: folder navigation refreshes rows, material picker shows one project material entry per material instead of arbitrary files, descriptor/cooked state badges remain user-facing identity facts, new material saves refresh browser/picker state without restart, and authored data remains under the accepted `Content` layout. |
| `ED-M06A` | `validated` | 2026-04-28 | User manually validated ED-M06A project layout and template standardization after starter-scene JSON fix: create project from template, starter scene load, new scene/material authored paths under `Content`, Content Browser folder navigation, Material Picker filtering, and authoring target resolution. MSBuild passed for Oxygen.Editor.App and focused ProjectBrowser tests; targeted VSTest run passed 96/96 across Projects, ContentBrowser, and ProjectBrowser assemblies before the final starter-scene regression test, then ProjectBrowser starter-scene regression passed 3/3. |
| `ED-M07` | `validated` | 2026-04-28 | User manually validated ED-M07 content pipeline and cooking: cook project, cook folder, cook selected asset, and cook current scene workflows; inspect cooked output shows visible summary feedback; validate cooked output shows visible feedback and drives validated cooked-root refresh; cooked mount root displays cooked files and persists/remounts from `Project.oxy`; material, scene, and cooked catalog refresh paths update without restart; failures produce visible operation results. Focused automated coverage included ContentPipeline tests 40/40 and ContentBrowser tests 62/62; functional ImportTool dry-run and actual temp Vortex import succeeded during implementation validation. |
| `ED-M07A` | `validated` | 2026-09-11 | Packaged controls/native 137/137; Runtime 71/71; SceneExplorer 161/161; World 67/67; Managed.Assets 89/89. User confirmed combined XYZ rotations, Cube/Sphere and cooked/None/Default material changes, and coupled sun controls through Undo/Redo and Save/reopen. Background presentation was confirmed earlier. All gates pass; see the [field/workflow results](validation/ED-M07A-field-workflows.md). |
| `ED-M07B` | `in_progress` | 2026-09-15 | ContentPipeline 449/449; final ownership/publication subset 94/94; production workspace creation, typed-material discovery, all cook triggers, current native bindings, cancellation/recovery and scene-lifetime cases 15/15. Native import numeric profiles and clean copies pass in Debug/Release. Main/Inspect/Main native document transitions pass 30 cycles each at 60/10 FPS with D3D12 validation. Browser workload and cook-feedback timing meet their gates; import/browser/material controls pass 33/33 across themes/scales, and inspector/Cooking scaling passes 18/18. User-confirmed startup, live material updates, duplicate removal, built-ins and leak-free shutdown remain recorded above. Completed contracts and remaining combined import, typed-use, navigation and project-lifetime gates are checked individually above. |
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
