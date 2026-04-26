# ED-WP02.1 - Normalize Scene Mutation Commands

Status: `planned`

## 1. Goal

Create a single command path for scene mutations so dirty state, undo/redo,
validation, save, and live sync behave consistently.

## 2. Problem

Current scene edits are spread across view models, scene helpers, messenger
messages, and direct component mutation. This makes it hard to reason about:

- whether a scene is dirty
- whether undo is available
- whether validation is stale
- whether live sync should run
- whether save/cook sees the same state as the viewport

## 3. Scope

In scope:

- command abstractions for scene document mutations
- command service owned by the scene document/world editor
- dirty-state integration
- undo/redo history
- mutation result object with affected scopes
- sync/validation notification hooks

Out of scope:

- final UI polish
- transform gizmo implementation
- full content browser rewrite

## 4. Likely Touch Points

- `projects/Oxygen.Editor.WorldEditor/src/SceneEditor`
- `projects/Oxygen.Editor.WorldEditor/src/SceneExplorer`
- `projects/Oxygen.Editor.WorldEditor/src/Inspector`
- `projects/Oxygen.Editor.WorldEditor/src/Services/SceneEngineSync.cs`
- `projects/Oxygen.Editor.World`

## 5. Command Contract

```text
IEditorCommand
  Execute(context) -> CommandResult
  Undo(context) -> CommandResult
  CanExecute(context)
```

`CommandResult` includes:

- changed scene ID
- changed node IDs
- changed component types
- validation scopes to invalidate
- live sync requests
- user-visible diagnostics if execution partially failed

## 6. First Commands

1. Create node
2. Delete node/subtree
3. Rename node
4. Reparent node
5. Add component
6. Remove component
7. Edit transform
8. Edit component property
9. Set geometry/material reference
10. Set scene environment value

## 7. Acceptance Criteria

- Existing hierarchy and inspector operations route through commands.
- Undo/redo works for node create/delete/rename/reparent and component add/remove.
- Dirty state changes only through command execution or explicit document load/save.
- Live sync is requested from command results, not ad hoc view-model calls.
- Tests cover command execute/undo and scene JSON round-trip for changed state.

## 8. Risks

- Existing view models may rely on direct mutation side effects.
- Messenger notifications may currently double as sync requests.
- Command batching is needed for text/slider edits to avoid unusable undo history.
