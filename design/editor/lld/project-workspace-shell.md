# Project Workspace Shell LLD

Status: `scaffold`

## 1. Purpose

Define Project Browser startup, project open/create, workspace activation,
workspace restoration, and the shell/project service boundary.

## 2. PRD Traceability

- `REQ-001`
- `REQ-002`
- `REQ-003`
- `REQ-022`
- `REQ-024`
- `SUCCESS-001`
- `SUCCESS-006`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 6, 8.1, 10.1, 14, 15
- `PROJECT-LAYOUT.md` ownership for `Oxygen.Editor`,
  `Oxygen.Editor.ProjectBrowser`, `Oxygen.Editor.Projects`, and
  `Oxygen.Editor.Runtime`

## 4. Current Baseline

To be reviewed against the existing Project Browser, shell routing, window
manager, project services, and workspace activation code.

## 5. Target Design

The project browser is the first experience. Opening or creating a project
establishes active project context before workspace activation. Workspace
restoration is best effort, failure-visible, and does not hide project or
runtime failures.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor` | application bootstrap, top-level routes, windows, native runtime discovery |
| `Oxygen.Editor.ProjectBrowser` | no-project UX, recent projects, create/open project UI |
| `Oxygen.Editor.Projects` | project metadata, content roots, project settings, project cook scope policy |
| `Oxygen.Editor.Runtime` | runtime startup, runtime settings, cooked-root mounting |

## 7. Data Contracts

To define:

- project identity
- recent project record
- project open result
- workspace restoration record
- invalid project diagnostic

## 8. Commands, Services, Or Adapters

To define:

- create project workflow
- open project workflow
- activate workspace workflow
- restore workspace workflow
- refresh cooked roots workflow

## 9. UI Surfaces

To define:

- Project Browser window
- invalid project state
- recent project list
- create project flow
- open project flow
- workspace activation failure surface

## 10. Persistence And Round Trip

To define:

- recent project persistence
- workspace layout persistence ownership
- partial restoration behavior

## 11. Live Sync / Cook / Runtime Behavior

Project open may trigger runtime and mount preparation, but shell/project UI
does not own engine operations.

## 12. Operation Results And Diagnostics

Project open/create and workspace restoration failures must produce visible
operation results.

## 13. Dependency Rules

Project Browser must not depend on WorldEditor internals. Project services must
not call native interop.

## 14. Validation Gates

To define:

- first launch opens Project Browser
- valid project opens workspace
- invalid project reports visible failure
- restoration failure is visible and non-fatal where possible

## 15. Open Issues

- Exact project template model.
- Exact workspace restoration failure UX.
