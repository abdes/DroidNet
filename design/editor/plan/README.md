# Editor Milestone Planning

Status: `active planning index`

This folder contains the detailed milestone plans listed below.
[../PLAN.md](../PLAN.md) owns order and milestone outcomes. Each active
execution milestone has one detailed plan; earlier delivery plans remain records.

`PLAN.md` owns the top-level milestone order and exit gates. Files in this
folder own implementation sequencing, touch points, risks, and validation plans
for a specific milestone, using numbered implementation slices inside its plan.
Shared contracts live in the LLDs; delivery gates belong to named milestones.
Issue-specific repair notes may also live here. They do not replace milestone
plans or establish milestone completion.

[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) is authoritative for
milestone progress and validation evidence. A detailed plan's presence or
acceptance is not evidence that its workflow has been implemented or validated.

## Required Plan Sections

Detailed milestone plans should include:

1. Purpose
2. PRD Traceability
3. Required LLDs
4. Scope
5. Non-Scope
6. Implementation Sequence
7. Project/File Touch Points
8. Dependency And Execution Risks
9. Validation Gates
10. Status Ledger Hook

## Milestone Plans

| Plan | Milestone | Purpose |
| --- | --- | --- |
| [ED-M01-project-browser-workspace-activation.md](./ED-M01-project-browser-workspace-activation.md) | `ED-M01` | Project Browser startup, project open/create, invalid project handling, workspace activation, restoration visibility, and operation results. |
| [ED-M02-live-viewport-stabilization.md](./ED-M02-live-viewport-stabilization.md) | `ED-M02` | Embedded runtime startup, surface/view lifecycle, viewport layout validation, runtime settings diagnostics, and visual validation. |
| [ED-M03-authoring-foundation.md](./ED-M03-authoring-foundation.md) | `ED-M03` | Scene document commands, selection, scene explorer hierarchy operations, dirty state, undo/redo, save/reopen, and authoring diagnostics. |
| [ED-M04-scene-editing-ux-component-inspectors.md](./ED-M04-scene-editing-ux-component-inspectors.md) | `ED-M04` | Component inspectors, environment authoring, settings handling, and scene-side material slot identity. |
| [ED-M05-scalar-material-authoring.md](./ED-M05-scalar-material-authoring.md) | `ED-M05` | Scalar material documents, material picker identity, geometry assignment, descriptor save/reopen, and minimum material cook/catalog slice. |
| [ED-M06-asset-identity-content-browser.md](./ED-M06-asset-identity-content-browser.md) | `ED-M06` | Content Browser asset identity rows, shared state reducer, typed picker projection, missing/broken references, and browser persistence boundaries. |
| [ED-M06A-game-project-layout-and-template-standardization.md](./ED-M06A-game-project-layout-and-template-standardization.md) | `ED-M06A` | Game project layout, predefined templates, project creation, scene/material authoring targets, Content Browser roots, and material picker filtering before content pipeline work. |
| [ED-M07-content-pipeline-and-cooking.md](./ED-M07-content-pipeline-and-cooking.md) | `ED-M07` | Descriptor generation, manifest/import orchestration, cook, inspect, validation, catalog refresh, and validated runtime mount refresh. |
| [ED-M07A-authoring-integrity-and-runtime-convergence.md](./ED-M07A-authoring-integrity-and-runtime-convergence.md) | `ED-M07A` | Source-identified authoring/inspector omissions, save integrity, field diagnostics and runtime convergence. |
| [ED-M07B-safe-content-publication-and-compatibility.md](./ED-M07B-safe-content-publication-and-compatibility.md) | `ED-M07B` | Saved snapshots, staged publication/rollback, required native mappings, matched-build and import qualification. |
| [ED-M08-runtime-parity-and-standalone-validation.md](./ED-M08-runtime-parity-and-standalone-validation.md) | `ED-M08` | Post-M07B reviewed plan: eight implementation slices for exact loading, publication/library ownership, complete observations, controlled captures and an automatic standalone check with joint acceptance. |
| [ED-M09-viewport-authoring-tools.md](./ED-M09-viewport-authoring-tools.md) | `ED-M09` | Concrete viewport navigation, picking, transform and overlay interaction. |
| [ED-M10-v01-release-qualification.md](./ED-M10-v01-release-qualification.md) | `ED-M10` | Qualified build/workload, full workflow, failure safety and performance proof. |
