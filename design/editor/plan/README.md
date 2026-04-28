# Editor Milestone Planning

Status: `active planning index`

This folder contains detailed implementation plans for milestones and work
packages listed in [../PLAN.md](../PLAN.md).

`PLAN.md` owns the top-level milestone order and exit gates. Files in this
folder own implementation sequencing, touch points, risks, and validation plans
for a specific milestone or work package.

## Required Plan Sections

Detailed milestone/work-package plans should include:

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

## Existing Work-Package Plans

These plans predate the final milestone structure and should be reconciled when
their milestone becomes active.

The `ED-WPxx.y` numeric prefix is historical and does not necessarily match the
current milestone ID after the `ED-M01` insertion.

| Work Package | Milestone | Purpose |
| --- | --- | --- |
| [ED-WP02.1-normalize-scene-mutation-commands.md](./ED-WP02.1-normalize-scene-mutation-commands.md) | `ED-M03` | Command model, dirty state, undo/redo. |
| [ED-WP02.2-component-inspectors-and-live-sync.md](./ED-WP02.2-component-inspectors-and-live-sync.md) | `ED-M03` / `ED-M04` | V0.1 component editors and sync completion. |
| [ED-WP04.1-asset-reference-model.md](./ED-WP04.1-asset-reference-model.md) | `ED-M05` / `ED-M06` | Asset reference identity and picker model. |
| [ED-WP05.1-manifest-driven-cooking.md](./ED-WP05.1-manifest-driven-cooking.md) | `ED-M07` | Descriptor/manifest cooking workflow. |
| [ED-WP06.1-settings-architecture-and-editors.md](./ED-WP06.1-settings-architecture-and-editors.md) | `ED-M04` / `ED-M07` | Settings ownership, persistence, validation, and editors. |
| [ED-WP08.1-validation-model.md](./ED-WP08.1-validation-model.md) | `ED-M03` / `ED-M09` | Structured validation and diagnostics model. |

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
