# Scene Authoring Model LLD

Status: `review`

## 1. Purpose

Define the managed scene authoring model that ED-M03 commands mutate and save.
The model is the editor-owned source of truth for scene hierarchy, components,
editor-only explorer layout, and JSON round trip.

This LLD is intentionally domain-focused. It does not own inspector layout,
asset picking, material authoring UI, or native runtime implementation.

## 2. PRD Traceability

| ID | Coverage |
| --- | --- |
| `REQ-004` | Scene documents have durable managed scene state. |
| `REQ-005` | Partial: component cardinality/domain rules are defined; full component add/remove/edit workflow completion is ED-M04. |
| `REQ-006` | Dirty state is based on managed authoring model mutations. |
| `REQ-007` | ED-M03-supported scene data saves and reopens; full component/environment round trip continues in ED-M04. |
| `REQ-008` | ED-M03-supported mutations provide the domain state needed for live-sync requests. |
| `REQ-009` | Partial: Transform, Geometry, and Light/Camera domain rules are grounded; Environment and material assignment are later LLD scope. |
| `REQ-036` | Scene persistence can use editor-owned schema where needed and later generate engine descriptors. |
| `REQ-037` | Supported ED-M03 scene data saves and reopens without manual repair. |
| `SUCCESS-002` | Supported scene edits survive save/reopen. |

## 3. Architecture Links

- `ARCHITECTURE.md`: authoring domain vs. runtime/cook separation.
- `PROJECT-LAYOUT.md`: `Oxygen.Editor.World` owns scene authoring domain data.
- `documents-and-commands.md`: command surface that mutates this model.
- `scene-explorer.md`: hierarchy UI and explorer layout projection.
- `live-engine-sync.md`: later adapter from this model to native runtime.

## 4. Current Baseline

`Oxygen.Editor.World` already provides:

- `Scene` with `RootNodes`, `AllNodes`, `ExplorerLayout`, hydrate/dehydrate.
- `SceneNode` with parent/children, flags, components, override slots, and
  cycle prevention in `SetParent`.
- required `TransformComponent` enforcement during hydration.
- geometry, camera, and light component classes with JSON DTOs.
- scene serializer infrastructure and source-generated JSON context.
- editor-only `ExplorerEntryData` persisted in scene DTOs.

WorldEditor already provides:

- `SceneMutator` for root/child create, remove, hierarchy remove, and reparent.
- `SceneOrganizer` for explorer layout/folder operations.
- `SceneExplorerService` to coordinate mutator, organizer, and live sync.
- inspector paths that directly mutate transform/geometry/component state and
  record undo entries.

Brownfield gaps:

- `SceneNode.AddComponent` does not enforce component cardinality; callers do.
- Quick-add and inspector paths can bypass `SceneMutator`.
- Rename is direct property mutation.
- Undo data is mostly inverse delegates, not command-result records.
- The domain model has no central revision/validation state.
- Environment authoring is not yet a complete ED-M03 domain workflow.

## 5. Target Design

The ED-M03 target is a reliable managed scene model for hierarchy authoring:

```text
Scene
  RootNodes
    SceneNode
      TransformComponent (required)
      optional GeometryComponent
      optional CameraComponent
      optional LightComponent
      OverrideSlots
      Children
  ExplorerLayout (editor-only)
```

Commands mutate this model synchronously first. Persistence and live sync are
downstream consumers of the resulting authoring state.

## 6. Ownership

| Owner | Responsibility |
| --- | --- |
| `Oxygen.Editor.World` | Scene, node, component, DTO, serializer, and domain invariants. |
| `Oxygen.Editor.WorldEditor` | Command orchestration, UI adapters, undo, dirty state, selection, live-sync requests. |
| `Oxygen.Editor.Projects` | Scene file location and save/load plumbing until project services are further split. |
| `Oxygen.Editor.Runtime` / interop | Runtime projection only; no authoring source of truth. |

## 7. Data Contracts

### Scene

Required:

- stable `Id`.
- `Name`.
- ordered root node collection.
- optional editor-only explorer layout.

Rules:

- root node order is authoring state and must round-trip.
- `ExplorerLayout` is editor UI state persisted with the scene, but it must not
  create runtime scene graph nodes.
- scene files remain readable through `SceneSerializer`.

### Scene Node

Required:

- stable `Id`.
- `Name`.
- exactly one `TransformComponent`.
- zero or more child nodes.

ED-M03 command rules:

- parent cycles are invalid.
- deleting a node deletes its hierarchy unless a command explicitly says it
  promotes children.
- reparent default preserves local transform. Preserve-world-transform is a
  later explicit command option.
- rename cannot produce empty/whitespace display names.

### Component Cardinality

ED-M03 quick-add/create command rules:

- exactly one transform component.
- at most one geometry component.
- at most one camera component.
- at most one light component.

The domain classes may remain permissive internally for serialization and
legacy load. ED-M03 quick-add/create commands construct component sets that
respect the rules above. Generalized add/remove cardinality enforcement is
owned by ED-M04 `property-inspector.md` and is not an ED-M03 gate.

### Procedural Primitive Node

ED-M03 quick-add creates a node with:

- transform component.
- geometry component using a supported procedural mesh kind.
- clear default name based on kind.

No texture/material authoring is required in ED-M03.

### Light Node

ED-M03 quick-add creates directional light nodes as a closure gate with:

- transform component.
- matching light component.
- safe default values already present in the component model.

Point and spot light quick-add remain best effort in ED-M03. If their current
domain create/save/reopen path works, it is kept, but point/spot runtime sync
or component-completion gaps do not block ED-M03 closure.

## 8. Commands, Services, Or Adapters

Domain mutation is exposed through command services described in
`documents-and-commands.md`.

`SceneMutator` remains the right low-level model mutator for hierarchy
operations. ED-M03 should either extend it or wrap it; it should not introduce a
parallel hierarchy mutation implementation.

`SceneOrganizer` remains the owner of explorer layout/folder state. Scene graph
commands call it only when the hierarchy UI projection needs to change.

## 9. UI Surfaces

The model is consumed by:

- scene explorer tree.
- scene editor quick-add menu.
- inspector selection/details.
- document save/reopen.
- live viewport sync adapter.

The domain model itself has no WinUI dependencies.

## 10. Persistence And Round Trip

ED-M03 persistence scope:

- scene name and ID.
- root node order.
- child hierarchy.
- node names.
- transform components.
- geometry components for supported procedural primitives.
- camera/light components already serialized by current DTOs.
- explorer layout where present.

Directional light round-trip is an ED-M03 closure gate. Point/spot light
round-trip is best effort unless the implementation already supports it without
additional runtime/component-editor work.

ED-M03 does not require material asset authoring, content-pipeline descriptors,
or standalone runtime parity.

## 11. Live Sync / Cook / Runtime Behavior

The model does not call runtime or cook APIs. Command services may request live
sync after model mutation.

Cook contribution remains brownfield through existing save/cook behavior until
ED-M07. ED-M03 must avoid adding new cook policy to the scene model.

## 12. Operation Results And Diagnostics

Domain validation failures map to `SceneAuthoring`.

Examples:

- empty name.
- duplicate cardinality through user command.
- stale node ID.
- invalid reparent cycle.
- unsupported primitive kind.
- scene save serialization failure maps to `Document`.

## 13. Dependency Rules

Allowed:

- WorldEditor depends on `Oxygen.Editor.World`.
- Tests may construct scene/domain objects directly.

Forbidden:

- `Oxygen.Editor.World` must not depend on WorldEditor, WinUI, runtime,
  interop, routing, project browser, or content browser UI.
- Domain DTOs must not store live runtime handles.
- Editor-only explorer layout must not be interpreted by the engine runtime as
  scene graph data.

## 14. Validation Gates

ED-M03 scene model is complete when:

- user commands cannot create invalid component cardinality for supported
  ED-M03 mutations.
- create primitive/directional light, rename, delete, and reparent round-trip
  through save and reopen.
- undo/redo restores scene graph shape and node names for ED-M03 commands.
- invalid reparent and stale node cases fail without partial model mutation.
- explorer layout remains layout-only and does not create scene nodes.

## 15. Open Issues

- Environment authoring details move to `environment-authoring.md`.
- Material override authoring moves to `material-editor.md` and ED-M05/ED-M06
  asset identity work.
- Schema migration policy for future scene file versions is outside ED-M03
  unless the implementation changes the serialized shape.
