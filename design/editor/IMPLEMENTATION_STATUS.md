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
4. Unfinished work is tracked in milestone/work-package checklists, not in a
   generic gaps list.
5. Blockers and decisions are recorded only when they block a named milestone
   or work package.

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

Current program target: **Oxygen Editor V0.1**.

Current execution focus:

- [x] Prepare `ED-M01` by reviewing `project-workspace-shell.md`,
      `project-services.md`, and `diagnostics-operation-results.md`.
- [ ] Implement `ED-M01` from
      [ED-M01-project-browser-workspace-activation.md](plan/ED-M01-project-browser-workspace-activation.md).
- [ ] Validate `ED-M02` live viewport stabilization with one/two/four viewport
      layouts, correct surface presentation, sane camera framing, native
      runtime discovery, and runtime settings.
- [ ] Prepare `ED-M03` by reviewing its required LLDs and producing a detailed
      implementation plan.

`ED-M02` may close in parallel with `ED-M03` LLD review and detailed planning.
`ED-M03` implementation may not rely on live preview as validation evidence
until `ED-M02` is `validated`.

Current resume point:

1. Implement `ED-M01` from
   [ED-M01-project-browser-workspace-activation.md](plan/ED-M01-project-browser-workspace-activation.md),
   continuing with `ED-M01.4 - Shell Activation Coordinator`.
2. Continue `ED-M02` validation in parallel where it does not block `ED-M01`.
3. Keep this ledger synchronized whenever a milestone or detailed plan changes.

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

Status: `active`

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
- [ ] Editor starts at Project Browser.
- [ ] Recent project, create project, open project, and invalid project states
      are usable.
- [ ] Successful project open transitions into the editor workspace.
- [ ] Workspace restoration is best effort and visible when partially
      unsuccessful.
- [ ] Project open/create failures produce visible operation results.

Exit evidence required:

- [ ] One `ED-M01` validation ledger row records startup path, project
      open/create behavior, invalid project behavior, workspace activation, and
      restoration failure behavior.

### ED-M02 - Live Viewport Stabilization

Status: `landed`

Trace: `GOAL-001`, `GOAL-003`; `REQ-025`, `REQ-027`, `REQ-028`,
`REQ-030`; `SUCCESS-003`, `SUCCESS-005`

Outcome: embedded Vortex preview, surface/view lifecycle, native runtime
discovery, runtime settings, and viewport presentation are stable.

Implementation:

- [x] Embedded engine starts from the editor process.
- [x] Native engine runtime DLLs load from the engine install runtime
      directory instead of being copied into editor output.
- [x] A live Vortex-rendered editor viewport is visible.
- [x] Editor viewport camera uses sane projection/framing defaults.

Validation:

- [ ] One-pane viewport layout is validated after clean editor launch.
- [ ] Two-pane viewport layout is validated without surface/view mismatch or
      abort.
- [ ] Four-pane viewport layout is validated without surface/view mismatch or
      abort.
- [ ] Correct surface presentation is validated for every visible viewport.
- [ ] Engine FPS/runtime settings are validated in the embedded engine.

Exit evidence required:

- [ ] One `ED-M02` validation ledger row records launch path, build config,
      viewport layout matrix, runtime DLL discovery path, runtime settings
      result, and outcome.

### ED-M03 - Authoring Foundation

Status: `planned`

Trace: `GOAL-001`, `GOAL-002`, `GOAL-003`, `GOAL-006`; `REQ-004`,
`REQ-005`, `REQ-006`, `REQ-007`, `REQ-008`, `REQ-009`, `REQ-022`,
`REQ-023`, `REQ-024`, `REQ-026`, `REQ-037`; `SUCCESS-001`, `SUCCESS-002`,
`SUCCESS-003`, `SUCCESS-004`

Outcome: scene documents, commands, dirty state, undo/redo, selection model,
scene explorer, operation results, and save/reopen form a reliable authoring
core.

- [ ] Required LLDs are reviewed:
      `documents-and-commands`, `scene-authoring-model`, `scene-explorer`,
      `diagnostics-operation-results`.
- [ ] Detailed `ED-M03` implementation plan exists.
- [ ] Node create/delete/rename/reparent operations use command paths.
- [ ] Component add/remove/edit operations for V0.1 components use command
      paths.
- [ ] Dirty state and undo/redo work for supported mutations.
- [ ] Scene save/reopen round-trips supported values.
- [ ] Live-sync intent is requested after supported mutations.
- [ ] Operation result presentation exists for command, save, and sync
      failures.

Exit evidence required:

- [ ] One `ED-M03` validation ledger row records command/dirty/undo behavior,
      save/reopen result, live-sync intent result, operation-result behavior,
      and cook readiness.

### ED-M04 - Scene Editing UX And Component Inspectors

Status: `planned`

Trace: `GOAL-002`, `GOAL-003`, `GOAL-006`; `REQ-005`, `REQ-007`,
`REQ-008`, `REQ-009`, `REQ-022`, `REQ-024`, `REQ-026`, `REQ-037`;
`SUCCESS-002`, `SUCCESS-003`, `SUCCESS-004`

Outcome: V0.1 scene components except real material asset authoring have
production-ready inspectors, defaults, validation, and live-sync requests.

- [ ] Required LLDs are reviewed:
      `property-inspector`, `environment-authoring`, `settings-architecture`,
      `live-engine-sync`, `runtime-integration`.
- [ ] Detailed `ED-M04` implementation plan exists.
- [ ] Transform, Geometry, PerspectiveCamera, DirectionalLight, and
      Environment have scoped editing behavior.
- [ ] Geometry components expose a material assignment/override slot that
      persists and can hold an unresolved or placeholder material identity,
      with real material asset creation/picking/assignment closing in `ED-M05`.
- [ ] Component edits use commands/services, not direct interop.
- [ ] Component edits persist, request sync, and report failures visibly.

Exit evidence required:

- [ ] One `ED-M04` validation ledger row records component editor coverage,
      settings/environment behavior, persistence, and live preview readiness.

### ED-M05 - Scalar Material Authoring

Status: `planned`

Trace: `GOAL-002`, `GOAL-004`, `GOAL-005`, `GOAL-006`; `REQ-010`,
`REQ-011`, `REQ-012`, `REQ-013`, `REQ-014`, `REQ-021`, `REQ-022`,
`REQ-037`; `SUCCESS-002`, `SUCCESS-004`, `SUCCESS-007`

Outcome: users can create/open/edit scalar material assets and assign them to
geometry through real editor UI.

- [ ] Required LLDs are reviewed:
      `material-editor`, `content-browser-asset-identity`,
      `asset-primitives`, `content-pipeline`.
- [ ] Detailed `ED-M05` implementation plan exists.
- [ ] Users can create/open scalar material assets.
- [ ] Users can inspect and edit scalar material values.
- [ ] Users can assign material assets to geometry through asset identity.
- [ ] Material values save and reopen.
- [ ] Minimum scalar material descriptor/cook/preview slice is validated or a
      visible engine/API limitation is recorded.

Exit evidence required:

- [ ] One `ED-M05` validation ledger row records material create/edit/assign,
      save/reopen, minimum cook/preview slice, and visible failure behavior.

### ED-M06 - Asset Identity And Content Browser

Status: `planned`

Trace: `GOAL-004`, `GOAL-005`, `GOAL-006`; `REQ-013`, `REQ-020`,
`REQ-021`, `REQ-022`, `REQ-024`, `REQ-036`, `REQ-037`; `SUCCESS-006`,
`SUCCESS-007`

Outcome: source, generated, descriptor, cooked, mounted, missing, and broken
asset states are visible and selectable by identity.

- [ ] Required LLDs are reviewed:
      `content-browser-asset-identity`, `asset-primitives`,
      `project-services`, `diagnostics-operation-results`.
- [ ] Detailed `ED-M06` implementation plan exists.
- [ ] Content browser distinguishes source, generated/descriptor, cooked,
      mounted, missing, and broken states.
- [ ] Asset picker returns typed asset identity.
- [ ] Broken references become visible diagnostics.
- [ ] Authoring data avoids raw cooked-path text as the user-facing identity.

Exit evidence required:

- [ ] One `ED-M06` validation ledger row records asset browsing, picking,
      missing-reference behavior, and persistence impact.

### ED-M07 - Content Pipeline And Cooking

Status: `planned`

Trace: `GOAL-001`, `GOAL-002`, `GOAL-005`, `GOAL-006`; `REQ-015`,
`REQ-016`, `REQ-017`, `REQ-018`, `REQ-019`, `REQ-022`, `REQ-023`,
`REQ-024`, `REQ-036`, `REQ-037`; `SUCCESS-001`, `SUCCESS-004`,
`SUCCESS-006`

Outcome: descriptor/manifest generation, cook, inspect, cooked validation,
catalog refresh, and mount refresh work as explicit workflows.

- [ ] Required LLDs are reviewed:
      `content-pipeline`, `project-services`, `asset-primitives`,
      `runtime-integration`, `diagnostics-operation-results`.
- [ ] Detailed `ED-M07` implementation plan exists.
- [ ] Procedural geometry descriptors are generated for supported procedural
      meshes.
- [ ] Scoped source import produces supported descriptors.
- [ ] Cooking includes current scene and referenced V0.1 assets.
- [ ] Cook output validates before mount.
- [ ] Cook, inspect, and mount failures produce visible operation results.

Exit evidence required:

- [ ] One `ED-M07` validation ledger row records descriptor/manifest inputs,
      cook output, inspect result, mount result, and failure-result behavior.

### ED-M08 - Runtime Parity And Standalone Validation

Status: `planned`

Trace: `GOAL-001`, `GOAL-002`, `GOAL-003`, `GOAL-006`; `REQ-018`,
`REQ-019`, `REQ-022`, `REQ-023`, `REQ-024`, `REQ-026`, `REQ-030`,
`REQ-037`; `SUCCESS-001`, `SUCCESS-003`, `SUCCESS-004`, `SUCCESS-006`

Outcome: a minimum authored content slice proves embedded preview, cooked
output, mounted content, and standalone runtime load agree.

- [ ] Required LLDs are reviewed:
      `standalone-runtime-validation`, `live-engine-sync`,
      `runtime-integration`, `content-pipeline`, `environment-authoring`.
- [ ] Detailed `ED-M08` implementation plan exists.
- [ ] Embedded preview renders the minimum authored content slice.
- [ ] Cooked output for the minimum slice loads in standalone runtime.
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
- [ ] Detailed `ED-M09` implementation plan exists.
- [ ] Camera navigation and frame selected/all are usable.
- [ ] Selection highlight is implemented.
- [ ] Transform gizmo UX mutates through commands.
- [ ] Non-geometry node icons exist for cameras/lights.
- [ ] One/two/four viewport layouts remain stable with overlays enabled.

Exit evidence required:

- [ ] One `ED-M09` validation ledger row records viewport UX coverage across
      one/two/four layouts.

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

Detailed plans live under [plan/](./plan/). Existing work-package plans predate
the final milestone model and must be reconciled when their milestone becomes
active. Each active milestone has either a single milestone plan
(`ED-Mxx-...md`) or one or more work-package plans (`ED-WPxx.y-...md`).
The `ED-WPxx.y` numeric prefix is historical and does not necessarily match
the current milestone ID after the `ED-M01` insertion.

| Plan | Milestone | Status | Next Action |
| --- | --- | --- | --- |
| [ED-M01-project-browser-workspace-activation.md](plan/ED-M01-project-browser-workspace-activation.md) | `ED-M01` | `landed` | Continue implementation with `ED-M01.4 - Shell Activation Coordinator`. |
| [ED-WP02.1-normalize-scene-mutation-commands.md](plan/ED-WP02.1-normalize-scene-mutation-commands.md) | `ED-M03` | `planned` | Reconcile with `documents-and-commands.md` and `scene-explorer.md`. |
| [ED-WP02.2-component-inspectors-and-live-sync.md](plan/ED-WP02.2-component-inspectors-and-live-sync.md) | `ED-M03` / `ED-M04` | `planned` | Split command foundation from inspector/sync work if needed. |
| [ED-WP04.1-asset-reference-model.md](plan/ED-WP04.1-asset-reference-model.md) | `ED-M05` / `ED-M06` | `planned` | Reconcile with `asset-primitives.md` and `content-browser-asset-identity.md`. |
| [ED-WP05.1-manifest-driven-cooking.md](plan/ED-WP05.1-manifest-driven-cooking.md) | `ED-M07` | `planned` | Reconcile with `content-pipeline.md` and `project-services.md`. |
| [ED-WP06.1-settings-architecture-and-editors.md](plan/ED-WP06.1-settings-architecture-and-editors.md) | `ED-M04` / `ED-M07` | `planned` | Reconcile with `settings-architecture.md` and `environment-authoring.md`. |
| [ED-WP08.1-validation-model.md](plan/ED-WP08.1-validation-model.md) | `ED-M03` / `ED-M09` | `planned` | Reconcile with `diagnostics-operation-results.md`. |

## 5. Validation Ledger

One row per milestone. Do not add running notes here; update the milestone
checklist or detailed plan tracker instead.

| Milestone | Status | Date | Evidence |
| --- | --- | --- | --- |
| `ED-M00` | `validated` | 2026-04-26 | Design package approved: README, RULES, PROJECT-LAYOUT, PRD, ARCHITECTURE, DESIGN, PLAN, LLD index/scaffolds, plan index, and status ledger are accepted as the V0.1 planning baseline. |
| `ED-M01` | `pending` | - | Not validated. |
| `ED-M02` | `pending` | - | Not validated. |
| `ED-M03` | `pending` | - | Not validated. |
| `ED-M04` | `pending` | - | Not validated. |
| `ED-M05` | `pending` | - | Not validated. |
| `ED-M06` | `pending` | - | Not validated. |
| `ED-M07` | `pending` | - | Not validated. |
| `ED-M08` | `pending` | - | Not validated. |
| `ED-M09` | `pending` | - | Not validated. |
| `ED-M10` | `pending` | - | Not validated. |

## 6. Decision And Blocker Register

Use this table only for decisions or blockers that stop a named milestone or
work package. Do not use it to list unfinished implementation work.

| ID | Affects | Status | Decision/Blocker | Required Action |
| --- | --- | --- | --- | --- |
| `DB-001` | `ED-M00` | `closed` | Top-level `PLAN.md` review feedback was applied and accepted. | No further action. |
