# Oxygen.Editor.Schemas

Property descriptor pipeline driven by engine JSON schemas plus editor overlays.

This library is the editor-side single source of truth for **what authored
properties exist, how they are validated, how they are presented in the
inspector, and how they map to engine commands and PAK records**.

It does so by composing two files for every authored kind:

| File                              | Owner       | Purpose                          |
| --------------------------------- | ----------- | -------------------------------- |
| `*.schema.json`                   | Engine team | Field set, types, constraints.   |
| `*.editor.schema.json` (sibling)  | Editor team | UI metadata in `x-editor-*`.     |

The overlay `$ref`s the engine schema and adds annotation keywords only.
A dedicated CI lint asserts no overlay file is ever embedded into the
engine binary via `oxygen_embed_json_schemas(...)`. See the design doc at
`design/editor/lld/property-pipeline-redesign.md`.

## Public surface

- `PropertyId<T>` — typed property identity.
- `PropertyDescriptor<T>` — reader / writer / validator / sync / PAK roles.
- `PropertyEdit` — generic edit map.
- `PropertyApply` — the **single** apply function. The same function is
  used for "do" and "undo", which is what makes the
  `redo(OP) == undo(UOP) == OP` identity hold structurally.
- `PropertyOp` — TimeMachine operand record carrying `(Nodes, Before,
  After)`.
- `PropertyBinding<T>` — multi-select binding with `IsMixed`.
- `CommitGroupController` — drag/wheel session controller, replaces
  per-VM session machinery.
- `EditorSchemaCatalog` / `EditorSchemaOverlay` — discover and merge.

## Dependency

- `JsonSchema.Net` (json-everything) draft-07 validator. Unknown keywords
  (the `x-editor-*` namespace) are spec-mandated to be ignored.
