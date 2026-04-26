# Scene Explorer LLD

Status: `scaffold`

## 1. Purpose

Define the scene hierarchy UI, hierarchy selection presentation, node
create/delete/rename/reparent UX, drag/drop behavior, and coordination with the
document selection model.

## 2. PRD Traceability

- `REQ-004`
- `REQ-005`
- `REQ-006`
- `REQ-022`
- `REQ-024`

## 3. Architecture Links

- `ARCHITECTURE.md` sections 6, 8.3, 10.3, 14
- `DESIGN.md` selection model contract

## 4. Current Baseline

To be reviewed against current scene explorer view models, item adapters,
selection behavior, context menus, and node commands.

## 5. Target Design

The scene explorer is the hierarchy UI for the active scene document. It
presents document selection state and requests hierarchy mutations through
commands.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.WorldEditor` | scene explorer UI, hierarchy presentation, hierarchy command UX |
| `documents-and-commands.md` | shared selection model and command dispatch |
| `scene-authoring-model.md` | scene graph and component domain data |

## 7. Data Contracts

To define:

- hierarchy item model
- selection projection
- drag/drop payload
- context menu command payload

## 8. Commands, Services, Or Adapters

To define:

- create node
- delete node/subtree
- rename node
- reparent node
- select node(s)

## 9. UI Surfaces

To define:

- hierarchy tree
- context menu
- drag/drop reparent affordance
- empty scene state
- invalid node state

## 10. Persistence And Round Trip

Scene explorer does not persist scene data directly. It invokes commands that
mutate the scene authoring model.

## 11. Live Sync / Cook / Runtime Behavior

Hierarchy commands request live sync through the command/sync pipeline. Scene
explorer does not call runtime or interop directly.

## 12. Operation Results And Diagnostics

Hierarchy command failures must be visible near the hierarchy action where
practical and also publish operation results.

## 13. Dependency Rules

Scene explorer may depend on WorldEditor command/document contracts and the
World domain. It must not call native interop directly.

## 14. Validation Gates

To define:

- select node updates inspector and viewport consumers
- rename updates hierarchy and document dirty state
- reparent preserves defined transform policy
- delete undo restores subtree

## 15. Open Issues

- Multi-select hierarchy policy.
- Drag/drop transform preservation policy.
