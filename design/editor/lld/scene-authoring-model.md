# Scene Authoring Model LLD

Status: `reviewed LLD`

## 1. Purpose

Define the editor-owned source-of-truth model for scenes and how mutations
flow through save, live sync, cooking, validation, dirty state, and undo/redo.

## 2. Current Baseline

`Oxygen.Editor.World` provides:

- `Scene`
- `SceneNode`
- `TransformComponent`
- `GeometryComponent`
- perspective and orthographic camera components
- directional, point, and spot light components
- JSON DTOs and scene serialization

The model is usable, but mutation paths are inconsistent. Some UI paths mutate
components directly, send messenger notifications, or call sync services from
view models. Those paths must converge on a command model.

## 3. Target Document Model

Each scene document owns:

- authoring scene graph
- document metadata
- viewport layout and editor camera state
- dirty state
- validation state
- live sync state
- command history
- save/cook state

The scene graph is domain state. Viewport cameras and editor view layout are
document/editor state unless the user explicitly edits a scene camera.

## 4. Mutation Contract

Every scene mutation follows:

```text
SceneCommand.Execute
  -> mutate authoring model
  -> create undo snapshot or inverse command
  -> mark document dirty
  -> invalidate validation scopes
  -> enqueue live sync request
  -> emit UI update notifications
```

Undo follows the same path in reverse and also invalidates validation/sync.

## 5. Command Types

| Command | Scope | Notes |
| --- | --- | --- |
| `CreateNodeCommand` | scene/hierarchy | Creates node with initial transform and optional components. |
| `DeleteNodeCommand` | hierarchy subtree | Captures full subtree and component state for undo. |
| `RenameNodeCommand` | node | Updates hierarchy, tabs/search labels, validation. |
| `ReparentNodeCommand` | hierarchy | Defines local/world transform preservation policy. |
| `AddComponentCommand` | node/component | Enforces component cardinality rules. |
| `RemoveComponentCommand` | node/component | Captures component state for undo. |
| `EditComponentPropertyCommand` | component | Supports batching for sliders/text commits. |
| `SetAssetReferenceCommand` | component/asset field | Uses asset reference model, not raw path text. |
| `SetSceneEnvironmentCommand` | scene | Updates scene-owned environment state. |

## 6. Component Completion Matrix

| Component | Domain | Serialize | Inspector | Live Sync | Cook | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Transform | yes | yes | basic | yes | yes | `in_progress` |
| Geometry | yes | yes | basic | yes | partial | `in_progress` |
| PerspectiveCamera | yes | yes | missing | yes | yes | `in_progress` |
| OrthographicCamera | yes | yes | missing | partial | yes | `in_progress` |
| DirectionalLight | yes | yes | missing | yes | yes | `in_progress` |
| PointLight | yes | yes | missing | not complete | not complete | `in_progress` |
| SpotLight | yes | yes | missing | not complete | not complete | `in_progress` |
| Environment | partial | partial | missing | partial | partial | `in_progress` |
| Material override | partial | partial | missing | partial | partial | `planned` |

Point and spot lights are not absent. They exist in the authoring model and
serialization, but are not workflow-complete.

## 7. Cardinality Rules

Initial rules:

- every node has exactly one Transform component
- a node has at most one Camera component
- a node has at most one Light component
- a node may have one Geometry component for V0.1
- scene Environment is scene-level, not a node component, unless engine design
  later requires entity-attached volumes

These rules must be enforced by commands and reflected in add-component menus.

## 8. Dirty State

Dirty state is set by successful authoring commands and cleared only after a
successful save of the authoring scene. Cooking does not clear scene dirty
state unless it also saves the scene as part of an explicit save-and-cook
operation.

## 9. Validation Hooks

Scene commands invalidate validation scopes:

- component property changed -> component and node
- asset reference changed -> component, node, content reference graph
- hierarchy changed -> node subtree and scene
- environment changed -> scene environment and live sync

## 10. Exit Gate

ED-M02 closes when Transform, Geometry, PerspectiveCamera, DirectionalLight,
and scene Environment satisfy:

- command-based mutations
- undo/redo
- dirty state
- JSON round-trip
- live sync
- cook contribution or explicit runtime-only classification
- validation result coverage
