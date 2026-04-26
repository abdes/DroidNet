# Project Services LLD

Status: `scaffold`

## 1. Purpose

Define project metadata, project settings, content roots, project cook scope,
and project policy services.

## 2. PRD Traceability

- `REQ-002`
- `REQ-003`
- `REQ-017`
- `REQ-018`
- `REQ-019`
- `REQ-022`
- `REQ-024`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 6, 7, 12, 14
- `PROJECT-LAYOUT.md` ownership for `Oxygen.Editor.Projects`

## 4. Current Baseline

To be reviewed against current project services, project metadata, content root
policy, settings, and cook-scope behavior.

## 5. Target Design

Project services own project facts and policy. They do not execute native cook
operations or own workspace UI.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.Projects` | project metadata, settings, content roots, project cook scope/policy |
| `Oxygen.Editor.ContentPipeline` | cook execution orchestration |
| `Oxygen.Editor.ProjectBrowser` | project open/create UI |

## 7. Data Contracts

To define:

- project identity
- project settings
- content roots
- cook scope
- project validation result

## 8. Commands, Services, Or Adapters

To define:

- project load/save service
- project settings service
- content root policy service
- project cook scope provider

## 9. UI Surfaces

Project services do not own UI panels. Project Browser and workspace settings
surfaces consume project service contracts.

## 10. Persistence And Round Trip

Project metadata and settings must round-trip without changing content root or
cook scope semantics.

## 11. Live Sync / Cook / Runtime Behavior

Project services define cook scope and content-root policy. ContentPipeline and
Runtime execute cook and mount behavior.

## 12. Operation Results And Diagnostics

Project load, save, settings, and content root failures must produce operation
results through the consuming workflow.

## 13. Dependency Rules

Project services must not depend on WorldEditor UI or native interop.

## 14. Validation Gates

To define:

- valid project loads
- invalid project is classified
- content roots resolve
- cook scope is produced for current scene/project

## 15. Open Issues

- Exact project settings schema.
- Cook scope granularity for V0.1.
