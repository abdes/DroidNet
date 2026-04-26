# Documents And Commands LLD

Status: `scaffold`

## 1. Purpose

Define document lifecycle, command-based mutation, undo/redo, dirty state,
diagnostic invalidation, and command operation results.

## 2. PRD Traceability

- `REQ-004`
- `REQ-005`
- `REQ-006`
- `REQ-007`
- `REQ-008`
- `REQ-022`
- `REQ-024`
- `SUCCESS-002`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 8.2, 8.3, 10.2, 10.3, 15
- `PROJECT-LAYOUT.md` ownership for `Oxygen.Editor.Documents`,
  `Oxygen.Editor.WorldEditor`, and feature editor modules

## 4. Current Baseline

To be reviewed against current document services, scene editor view models,
command implementations, dirty-state handling, and direct mutation paths.

## 5. Target Design

Every supported scene/material/document mutation flows through commands or a
command-equivalent service. Commands update authoring state, dirty state,
undo/redo history, diagnostics, and live-sync intent.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.Documents` | generic document contracts and lifecycle primitives |
| `Oxygen.Editor.WorldEditor` | scene document commands and workspace command orchestration |
| feature editors | feature-specific document commands |
| authoring domains | state mutated by commands |

## 7. Data Contracts

To define:

- document identity
- command identity
- command payload/result
- undo/redo record
- dirty-state transition
- diagnostic invalidation scope

## 8. Commands, Services, Or Adapters

To define:

- command dispatcher
- command history
- command batching
- edit commit semantics
- command-to-live-sync request adapter

## 9. UI Surfaces

To define:

- save state
- dirty markers
- undo/redo affordances
- command failure display

## 10. Persistence And Round Trip

Document save/reopen must round-trip supported V0.1 authoring state.

## 11. Live Sync / Cook / Runtime Behavior

Commands request live sync where supported but do not execute native engine
operations directly.

## 12. Operation Results And Diagnostics

Command failures and command-triggered sync/save/cook failures must produce
visible operation results when triggered by user workflow.

## 13. Dependency Rules

Generic document contracts must not depend on WorldEditor. Feature commands
must not call native interop directly.

## 14. Validation Gates

To define:

- create/edit/delete command updates dirty state
- undo/redo restores authoring state
- failed command reports visible result
- save/reopen preserves supported values

## 15. Open Issues

- Command batching policy for sliders and text fields.
- Cross-document command scope.
