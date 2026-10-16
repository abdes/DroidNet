# Scene edits

Status: **draft for review**, revised after review.

Scope: the redesign of scene commands in `Oxygen.Editor.Scenes`. It replaces
`ISceneDocumentCommandService`, `SceneDocumentCommandService` (18 partials,
11.8k lines with its value types), `SceneAuthoringGate`, `EditSessionToken`,
`ISceneMutator` and `ISceneOrganizer`. It works within
[scenes-module.md](scenes-module.md); section 3.6 there holds the four mandatory
edit rules, which this design implements.

The design states the contracts scene authoring needs. Existing code (World,
Schemas, TimeMachine, Documents, the engine sync and the views) changes to meet
them; the current service is used only to find behavior users must keep.

## 1. Facts this design rests on

- The service has 42 public methods. Each repeats one frame by hand: gate, stale
  and lock checks, validation, mutation, an undo closure that calls back into
  the service, `AdvanceRevision`, an engine call, posting, a result. 18 history
  replay bodies (`*ForHistoryAsync`, `*ForUndoAsync`, `*ForRedoAsync`) repeat it
  again for undo. 16 `catch (Exception)` blocks report and continue.
- Property edits run through two pipelines: the schema pipeline (`PropertyEdit`,
  `PropertyOp`, `PropertySnapshot`, `PropertyApply`, descriptors in
  `Oxygen.Editor.Schemas`) and five typed edits (`TransformEdit`,
  `PerspectiveCameraEdit`, `DirectionalLightEdit`, `GeometryEdit`,
  `SceneEnvironmentEdit`). `PropertyAdapters` converts the first into the
  second. Eight builders (`Build*PropertyEntries`) build engine entries
  (`EnginePropertyValueEntry(component, ushort field, float value)`) by hand,
  while every `PropertyDescriptor` already carries an `EngineCommandKey`.
- `HistoryKeeper.AddChange` never routes into an open transaction, and
  `RollbackTransaction` pops transactions without applying anything. TimeMachine
  transactions cannot roll a scene back. Decision 11 of the scenes design
  ("change sets become TimeMachine transactions") cannot be implemented as
  written; section 3.5 replaces it.
- The synchronous `HistoryKeeper.Undo`/`Redo` drop the entry when it throws;
  only the async pair restores it, and rethrows.
- The views compose several commands into one user action under `BeginChangeSet`
  (atmosphere lights reset, camera move) or under a gate they enter themselves
  (explorer context actions). A failure part way leaves a partial entry.
- `World` objects are observable (`ScopedObservableObject`). Views bind
  properties directly and use the service's five events for structure.
- The interop layer serializes all engine commands.
- `SceneNodeData`, `ComponentData` and `ExplorerEntryData` are serialization
  records with mutable `IList` members.

## 2. The model

One `SceneAuthoring` per open scene document executes every change to the scene.
A change is an `ISceneEdit`: a serializable record of ids and values that
validates against the scene, applies itself, and returns its exact inverse and
the changes it made. The frame (thread, lifetime, targets, locks, atomicity,
history, change notification, result) is written once, in `SceneAuthoring`. The
engine, the explorer, selection and Play follow the change stream; no edit knows
they exist.

```text
caller (view, MCP) ── ExecuteAsync(edit) ──► SceneAuthoring (scene thread)
                                              ├ retired? targets exist and unlocked? valid?
                                              ├ edit.Apply(scope) → revert + changes
                                              ├ history.AddChange(entry(revert))
                                              ├ Changed(changes)  ──► engine projector ─► interop
                                              │                   ──► explorer, selection, play
                                              └ SceneEditResult
```

## 3. Specification

### 3.1 `SceneAuthoring`

```csharp
public sealed class SceneAuthoring : IDisposable
{
    public DocumentId Document { get; }
    public Scene Scene { get; }                       // read access for views and queries
    public event EventHandler<SceneChangedEventArgs>? Changed;

    public Task<SceneEditResult> ExecuteAsync(ISceneEdit edit);
    public Task<SceneEditValidation> ValidateAsync(ISceneEdit edit);  // dry run, no mutation
    public Task<bool> UndoAsync();
    public Task<bool> RedoAsync();

    public Task<SceneGesture> BeginGestureAsync(
        IReadOnlyCollection<NodeId> targets,      // empty: the scene's own properties
        IReadOnlyCollection<PropertyId> properties,
        string label);
    public bool HasEditsInProgress { get; }
    public Task FinishEditsAsync();                   // commits open gestures
    public void Dispose();                            // retires: every later call is refused
}
```

- `SceneContent` creates it when the document decodes and owns it. Views reach
  it through `SceneContent.Of(document).Authoring`.
  `IDocumentContent.HasEditsInProgress` and `FinishEditsAsync` delegate to it.
  Its `Dispose` (content replaced by a reload, document closed) cancels open
  gestures and retires it. The content no longer holds a service.
- It is constructed with the `SynchronizationContext` the document decoded on
  (the UI thread in the editor, a single-threaded context in tests). Every
  public call posts to that context; the edit then runs synchronously there.
  Because validation and application are synchronous and run on one thread,
  edits cannot interleave: no lock, no `AsyncLocal`, no operation counting.
  `SceneAuthoringGate` is deleted, and so is its "installing" state (3.7, engine
  registration).
- `ValidateAsync` replaces `ValidateExplorerPaste` and the views' "can execute"
  checks.

### 3.2 `ISceneEdit`

```csharp
public interface ISceneEdit
{
    string Label { get; }
    IReadOnlyCollection<NodeId> Targets { get; }   // nodes that must exist and be unlocked
    bool ChangesDocument => true;                  // false: the history keeps its position (hide)
    SceneEditValidation Validate(SceneEditScope scope);
    SceneEditApplication Apply(SceneEditScope scope);
}

public abstract record SceneEditApplication
{
    public sealed record Applied(ISceneEdit Revert, IReadOnlyList<SceneChange> Changes) : SceneEditApplication;
    public sealed record Rejected(SceneEditValidation Validation) : SceneEditApplication;
}
```

- **Records of ids and values.** Allowed members: typed ids, primitives, `Uri`,
  `Vector3`, `Quaternion`, enums, `PropertyEdit` (immutable, 3.10) and
  serialization records. A serialization record is deep-copied when the edit is
  constructed, so the edit owns it and never shares a list with the live scene.
  Live model objects (`Scene`, `SceneNode`, `GameComponent`) are never members.
- **The apply contract.** `Apply` is called only after `Validate` succeeded on
  the current scene. It either applies completely and returns `Applied`, or
  leaves the scene exactly as it found it and returns `Rejected`. A leaf edit
  validates everything it relies on, so it always returns `Applied`. Only a
  composite (3.5) can return `Rejected`. An exception from `Apply` is a defect,
  not a result (3.3).
- **Exact inverse.** The revert restores exactly the state the edit replaced,
  captured during `Apply`, including state the edit changed as a consequence
  (layout entries, compensated transforms, coupled properties). It is never a
  recomputation and never a whole-layout or whole-scene snapshot. The revert's
  own revert is the redo.
- `SceneEditScope` gives the edit the scene, the node-interaction port (keyed by
  `NodeId`, 3.9), the asset redirects and the component rules (3.10), plus
  `CheckTargets(ISceneEdit)`, the frame's existence and lock check (3.3 step 2).
  Edits receive nothing else.

### 3.3 The frame

`ExecuteAsync(edit)`, on the scene thread:

1. Refused if retired.
2. `CheckTargets(edit)`: each target exists; refused if a target or one of its
   ancestors is locked.
3. `edit.Validate(scope)` for a leaf edit; refused with its diagnostics if it
   fails. A batch is not validated here: its `Apply` validates each inner edit
   and rolls back (3.5).
4. `edit.Apply(scope)`; `Rejected` ends here with its diagnostics.
5. `history.AddChange(new SceneHistoryEntry(this, applied.Revert))`.
6. `Changed` is raised with `applied.Changes`.
7. Returns `SceneEditResult` with outcome `Applied`. New node, component and
   folder ids are in the edit, chosen by its builder, so the caller already
   holds them.

If `Apply` throws, the frame does not catch it: the scene state is undefined and
the exception propagates. This replaces the 16 `catch (Exception)` blocks.

### 3.4 Undo and redo

`SceneHistoryEntry : IChange` holds the `SceneAuthoring` and the edit to apply.
Its `Apply` runs steps 1, 3, 4 and 6 of the frame. It skips the lock check: a
lock protects nodes from new authoring, and replaying the history returns the
scene to states the user already made. The inverse is recorded by the history
itself: the entry adds it during `Undoing`/`Redoing`, which `HistoryKeeper`
routes to the other stack.

If the authoring is retired, validation fails, or `Apply` returns `Rejected`
during replay (the scene changed in a way the entry does not expect), the entry
throws `SceneReplayRefusedException` before adding any inverse, so the history
restores it.

TimeMachine changes for this: `HistoryKeeper.Undo` and `Redo` return `bool`;
when the change throws, they restore the entry to its stack and position and
return `false`, as `UndoAsync`/`RedoAsync` already restore it. `SceneAuthoring`
posts `UndoAsync`/`RedoAsync` to the scene thread and calls `Undo`/`Redo`. Every
scene undo goes through `SceneAuthoring`; the explorer's direct
`History.UndoAsync` calls go.

### 3.5 Atomic batches

```csharp
public sealed record SceneEditBatch(string Label, IReadOnlyList<ISceneEdit> Edits) : ISceneEdit;
```

- `Targets` is empty: each inner edit is checked when it runs, against the scene
  as the previous edits left it. This is what lets a batch create a node and
  then edit it.
- `Apply` runs, for each inner edit in order, `scope.CheckTargets`, `Validate`
  and `Apply`. If one is refused or rejected, it applies the reverts of the
  edits already applied, in reverse order, and returns `Rejected` with that
  edit's diagnostics. The scene is as before, nothing is recorded, no change is
  raised. A revert returning `Rejected` during this rollback is a defect and
  throws: an exact inverse (3.2) cannot be refused on the state its edit left.
- `Validate` of a batch runs the batch on a scratch scene, hydrated from the
  current scene's `Dehydrate()`, and returns the first failure. Only
  `ValidateAsync` uses it, for the views' "can execute" checks; execution relies
  on the rollback above.
- The revert is a batch of the inner reverts in reverse order. The changes are
  the concatenation of the inner changes.

Every multi-step user action is one batch: the eight change sets in the service,
the two in the views and the explorer's context actions.

### 3.6 Gestures

A gesture is a property edit that previews many times and records once.

```csharp
public sealed class SceneGesture
{
    public Task<SceneEditResult> PreviewAsync(SetProperties values);
    public Task<SceneEditResult> CommitAsync();   // one history entry: before → last preview
    public Task CancelAsync();                    // restores before, no entry
}
```

- `BeginGestureAsync` checks targets and locks once, and captures the before
  state of the targets for the named properties and the properties coupled to
  them (3.10). It applies nothing; the first `PreviewAsync` writes values, so
  the inspector can begin on focus and the gizmo at its first move. A preview
  with other targets or other properties is refused.
- A preview validates, writes the values, and raises
  `PropertiesChanged(..., Phase: Preview)`. It records nothing.
- Commit records one `SetProperties` entry whose revert restores the before
  state, and raises `Phase: Commit`. Cancel restores the before state and raises
  `Phase: Cancel`. A commit whose values equal the before state records nothing.
- If a target is removed or replaced while the gesture is open (an undo in
  another view), the gesture is cancelled at its next call and that call reports
  it.
- The gestures belong to the `SceneAuthoring`. `HasEditsInProgress` is true
  while an open gesture's current values differ from its before state, so a drag
  returned to its start does not hold a save or a cook. `FinishEditsAsync`
  commits them all; `Dispose` cancels them.
- Gestures are a UI mechanism. Remote callers use one-shot edits.

### 3.7 The change stream

`SceneChangedEventArgs(DocumentId Document, IReadOnlyList<SceneChange> Changes)`,
raised on the scene thread after each edit, replay, preview and relocation, in
order. A change names what changed, by id; consumers read the values from the
scene, which already holds them.

| Change                                                         | Raised by                                   |
| -------------------------------------------------------------- | ------------------------------------------- |
| `NodesAdded(NodeId[] roots)`                                   | insert                                      |
| `NodesRemoved(NodeId[] roots)`                                 | remove                                      |
| `NodesPlaced(NodeId[] nodes)`                                  | reparent, reorder, placement restore        |
| `NodeRenamed(NodeId)`                                          | rename                                      |
| `ComponentAdded(NodeId, ComponentId)`                          | add component                               |
| `ComponentRemoved(NodeId, ComponentId, string kind)`           | remove component                            |
| `PropertiesChanged(NodeId? node, PropertyId[] ids, EditPhase)` | properties (`node` null: scene)             |
| `GeometryChanged(NodeId)`                                      | geometry asset                              |
| `MaterialSlotsChanged(NodeId)`                                 | material slots                              |
| `LayoutChanged`                                                | every edit that changes the explorer layout |
| `EditorVisibilityChanged(NodeId[])`                            | hide or show in editor                      |
| `ScenePlayChanged`, `SceneReferencesChanged`                   | scene settings                              |
| `ScriptSlotsChanged(NodeId)`                                   | script slots                                |

Consumers:

- **The engine projector** (`SceneEngineSync` today, LivePreview in S1.7)
  translates changes into interop calls. It owns:
  - preview throttling and the preview, commit and cancel phases;
  - the directional-light role transfer order (release the native occupant
    before assigning the new owner);
  - engine field encoding from the descriptors (3.10);
  - `NodesPlaced`: push the node's current local transform, then reparent
    without compensation; the scene already holds the compensated transform;
  - waiting for relocated assets to be mounted before attaching them (3.11);
  - registration: `RegisterDocument` snapshots the scene synchronously on the
    scene thread; every `Changed` raised after that point is replayed after the
    full sync, and none raised before it;
  - reporting engine outcomes.

  `CaptureRevision`, `AdvanceRevision`, `TryPreviewSyncAsync`,
  `CompleteTerminalSyncAsync`, `CancelPreviewSyncAsync` and the per-operation
  methods leave the port; nothing in Scenes calls the engine.

- **Selection** drops removed nodes on `NodesRemoved`. Edits do not set the
  selection; a caller that wants created nodes selected selects the ids it put
  in the edit. `SceneCommandSelectionEventArgs` and the service's
  `SelectionChanged` go.
- **The explorer and Play** replace their subscriptions to the five events and
  the explorer's `suppressNodeMessages`.

### 3.8 Results

```csharp
public sealed record SceneEditResult(
    SceneEditOutcome Outcome,                     // Applied, Refused, Invalid, Retired
    IReadOnlyList<SceneDiagnostic> Diagnostics);
```

Scenes posts nothing. `IOperationResultPublisher` and `IStatusReducer` leave the
module. The views map results to operation results until the Shell's
notification policy replaces them (S1.10). Engine outcomes are reported by the
projector, not returned by `ExecuteAsync`: the edit succeeded when the scene
changed.

### 3.9 Ids

`NodeId`, `ComponentId` and `SceneId` (`Oxygen.Managed.Core`, shared with the
Runtime and the Interop, so the engine API takes them and the `Guid` is unboxed
only at the native boundary), `FolderId` (World, explorer layout) and
`DocumentId` (Documents: documents are an editor concept, so neither
Managed.Core nor the Runtime knows them) are `readonly record struct` wrappers
of `Guid` with JSON converters that keep the file format unchanged.
`SceneNode.Id`, `GameComponent.Id`, `Scene.Id`,
`ExplorerEntryData.NodeId/FolderId` and `Document.Id` use them. Edits, changes,
results, selection, the node-interaction port (`ISceneNodeInteraction`, which
today takes `Guid` and `SceneNode`) and the engine port use them; no `Guid` and
no `SceneNode` crosses those APIs.

### 3.10 One property path

- `SetProperties(IReadOnlyDictionary<NodeId, PropertyEdit> edits, string label)`
  replaces `EditPropertiesAsync` (both overloads),
  `EditPropertiesForTargetsAsync`, `EditTransformAsync`,
  `EditPerspectiveCameraAsync`, `EditDirectionalLightAsync` and the
  descriptor-only kinds. `SetSceneProperties(PropertyEdit edit, string label)`
  replaces `EditSceneEnvironmentAsync` and
  `EditSceneEnvironmentPropertiesAsync`. Each captures, during `Apply`, the
  before values of the edited properties and of the properties coupled to them;
  its revert writes those values back.
- `PropertyEdit` becomes immutable (built with a builder) so an edit can own it.
  Its keys are `PropertyId(ComponentKind, JsonPointer)` and its values are the
  descriptor's value type, which is serializable.
- One `IComponentRules` per component kind holds the component's domain rules:
  - **coupling**: the properties whose before state must be captured with an
    edited one (editing `Transform.RotationX` captures all three Euler axes,
    because normalization can change the other displayed axes);
  - **validation**: cross-field and cross-node rules against the candidate state
    (camera near < far, directional light cascades, the single atmosphere sun,
    the environment constraints). They are written against `PropertyEdit` and
    the candidate component state; the `Validate*Edit` and `Validate*Candidates`
    methods, which take the deleted typed edits, are rewritten, not moved;
  - **defaults on add**: the consequences of adding the component (3.10.1).

  Per-field validation stays on the descriptor (`ValidateBoxed`).

- Each descriptor gains its engine mapping (component and field id) next to its
  `EngineCommandKey`; the projector builds `EnginePropertyValueEntry` from the
  descriptor. The five typed edits, `OptionalEditValue`, `OptionalEditValues`,
  the `*State` records, `PropertyAdapters`, `DescriptorKinds` and the eight
  `Build*PropertyEntries` builders go.

#### 3.10.1 Adding a component

`SceneComponents.Add(SceneAuthoring, NodeId, string kind)` builds the edit for
the inspector's "add component": `AddComponent(node, data)`, followed in one
batch by the `SetProperties` its component rules require. Adding a directional
light to a node with identity rotation sets
`DirectionalLightComponent.DefaultLocalRotation`. The revert falls out of the
batch.

### 3.11 Asset references

- **In history.** An entry recorded before an asset moved holds the old URI.
  Edits that carry asset references (`SetGeometry`, `SetMaterialSlot`,
  `InsertNodes`, `SetScenePlay`, `SetSceneReferences`, `SetScriptSlots`,
  `SetSceneProperties`) retarget them through `scope.Redirects` in `Validate`
  and `Apply`, and report a reference to a deleted asset as a warning
  diagnostic. This replaces `RedirectCaptured`, `RedirectOrNull` and
  `WarnIfDeleted`.
- **On relocation.** `SceneContent.ApplyRelocation` changes the scene outside
  the history (the relocation's undo is global). It runs on the scene thread
  through the authoring and raises `GeometryChanged` for every retargeted node
  and `PropertiesChanged(null, ...)` when the environment changed. The projector
  attaches a relocated asset once it is mounted; the view's
  `RelocatedRuntimeRefresh` moves into the projector, and
  `RefreshRelocatedRuntimeAsync`, `SceneRelocatedEventArgs` and
  `SceneContent.Relocated` go.
- **On reload.** A reload replaces the content, so the new `SceneAuthoring`
  raises nothing; the views and the projector follow `Document.ContentReplaced`.

### 3.12 Reading a document while it is cooked

The cook reads a scene document's file and captures its state; a reload must not
replace the content during that read. This is a document concern, not an
authoring one: `Document` gains `AcquireReadLease()`, which defers content
replacement until the lease is released. `SceneCookInputRegistrar` uses it in
place of `SceneAuthoringGate.TryEnter`.

A lease does not defer a save. The cook opens the files it reads with
`FileShare.Read | FileShare.Delete`: in-place writes stay excluded, and a save's
atomic replace proceeds. The cook keeps reading the replaced file, whose bytes
match the saved state it captured under the lease, and the save is a change of
its own that the cook reads again.

## 4. Edit catalogue

Types used below:

```csharp
public sealed record NodePosition(NodeId? Parent, int Index);                       // scene graph
public sealed record LayoutPosition(FolderId? Folder, NodeId? Node, int Index);     // explorer layout
public sealed record NodeInsert(SceneNodeData Node, NodePosition At, LayoutPosition Layout, ExplorerEntryData? LayoutEntry);
public sealed record NodePlacementState(NodeId Node, NodePosition At, TransformData Local, LayoutPosition Layout);
```

| Edit                                                                                | Replaces                                                                                                       | Revert                                                                                                   |
| ----------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| `InsertNodes(NodeInsert[])`                                                         | `CreatePrimitive/Light/LocalFogVolume/Camera/EmptyNode`, `CreateNode`, the three `DuplicateNodes*`, paste-copy | `RemoveNodes(roots)`                                                                                     |
| `RemoveNodes(NodeId[] roots)`                                                       | `DeleteNodes`                                                                                                  | `InsertNodes` with each root's data, positions and layout entry subtree (folders under it, `IsExpanded`) |
| `RenameNode(NodeId, string)`                                                        | `RenameNode`                                                                                                   | `RenameNode` with the old name                                                                           |
| `ReparentNodes(NodeId[], NodeId? parent, bool preserveWorld, NodeId? after)`        | `ReparentNodes`, paste-cut of nodes                                                                            | `PlaceNodes` with each node's prior placement state                                                      |
| `ReorderNode(NodeId, NodePosition, LayoutPosition)`                                 | `ReorderNodes`                                                                                                 | `PlaceNodes` with the prior state                                                                        |
| `PlaceNodes(NodePlacementState[])`                                                  | the reparent undo paths                                                                                        | `PlaceNodes` with the states it replaced                                                                 |
| `CreateFolder(FolderId, LayoutPosition, string name)`                               | `CreateFolder`                                                                                                 | `RemoveFolder(id, promoteChildren: false)`                                                               |
| `RemoveFolder(FolderId, bool promoteChildren)`                                      | `DeleteFolder`, the folder part of `DeleteItems`                                                               | `RestoreFolder` with its entry, position and its children's prior positions                              |
| `RestoreFolder(ExplorerEntryData entry, LayoutPosition, LayoutPosition[] children)` | the folder undo paths                                                                                          | `RemoveFolder`                                                                                           |
| `RenameFolder(FolderId, string)`                                                    | `RenameFolder`                                                                                                 | `RenameFolder` with the old name                                                                         |
| `MoveLayoutEntries(LayoutEntryId[], LayoutPosition to)`                             | the same-scope part of `MoveNodesToFolder`, `RemoveNodesFromFolder`, `MoveFolderToParent`                      | `MoveLayoutEntries` per entry back to its prior position                                                 |
| `AddComponent(NodeId, ComponentData, int index)`                                    | `AddComponent` (with 3.10.1)                                                                                   | `RemoveComponent`                                                                                        |
| `RemoveComponent(NodeId, ComponentId)`                                              | `RemoveComponent`                                                                                              | `AddComponent` with its data at its old index                                                            |
| `SetProperties`, `SetSceneProperties`                                               | 3.10                                                                                                           | the captured before values, coupled ones included                                                        |
| `SetGeometry(NodeId[], Uri)`                                                        | `EditGeometry`                                                                                                 | per node, the old geometry                                                                               |
| `SetMaterialSlot(NodeId[], MaterialSlotTarget, Uri?)`                               | `EditMaterialSlot`                                                                                             | per node, the old slot state                                                                             |
| `RemoveMaterialSlots(IReadOnlyDictionary<NodeId, MaterialSlotTarget[]>)`            | `ClearUnresolvedMaterialSlots`                                                                                 | restores the removed slots                                                                               |
| `SetScenePlay(ScenePlayData)`, `SetSceneReferences(SceneReferencesData)`            | the same methods                                                                                               | the old value                                                                                            |
| `SetScriptSlots(NodeId, ImmutableArray<ScriptSlot>)`                                | `EditScriptSlots`                                                                                              | the old slots                                                                                            |
| `SetEditorHidden(NodeId[], bool)` (`ChangesDocument = false`)                       | `SetEditorHidden`                                                                                              | the old flags                                                                                            |
| `SceneEditBatch`                                                                    | change sets, `DeleteItems`, paste, the view compositions                                                       | 3.5                                                                                                      |

`LayoutEntryId` is a node id or a folder id. Every edit that changes the layout
raises `LayoutChanged`.

- **Layout scope.** A node's layout entry lives in the scope of its scene
  parent: the root, or the entry of its parent node.
  `LayoutScopes.ScopeOf(LayoutPosition)` computes the scope of a position, for
  the edits and for every caller. `MoveLayoutEntries.Validate` refuses a
  destination whose scope differs from a moved node's parent, or from the
  parents of a moved folder's nodes. Moving into another scope is a batch:
  `ReparentNodes(nodes, scope parent, preserveWorld: false, ...)` and then
  `MoveLayoutEntries`, which keeps today's behavior of grouping (the local
  transform is kept).
- **Reparent and reorder keep the layout entry.** `ReparentNodes` and
  `ReorderNode` move a node's layout entry, with its nested folders and
  `IsExpanded`, to the destination scope; they never delete it. A
  `LayoutPosition` is therefore enough for `PlaceNodes` to put it back.
- **Geometry.** `SetGeometry` refuses a missing asset ("Remove the component to
  detach geometry."), and leaves override slots untouched; unresolved slots are
  removed only by `RemoveMaterialSlots`.
- The compensated transforms of `ReparentNodes` with `preserveWorld` raise no
  `PropertiesChanged`: views read them from the scene, and the projector pushes
  them on `NodesPlaced`.

Supporting code, not edits:

- **`SceneNodeTemplates`**: the default nodes for primitives, lights, cameras
  and fog volumes (`NormalizePrimitiveKind`, `CreateLightComponent`,
  `ApplyLightTransform`, `ApplyPlacement`) as functions returning
  `SceneNodeData` with new ids.
- **`SceneNodeCopies`**: captures nodes as `SceneNodeData` with new node and
  component ids, remapping node references in script slots
  (`CopiedNodeReferences`) and clearing the atmosphere slot of copied
  directional lights, with an optional transform per root. It serves duplicate,
  duplicate in place and the clipboard. `InsertNodes.Validate` applies the
  component rules to inserted data, so a copy can never create a second sun.
- **`SceneClipboard`**: the explorer clipboard (`CaptureExplorerClipboard`, the
  layout capture) as data. Paste is a batch built from it: `InsertNodes` and
  layout edits for a copy, `ReparentNodes` and layout edits for a cut.
- **Material slot inventories** are read before the edit. A helper reads the
  inventory asynchronously and builds `SetMaterialSlot` or `RemoveMaterialSlots`
  with the slot ids and the inventory's `LayoutRevision`. `Validate` checks that
  each node still uses that geometry and revision.
  `IGeometryMaterialSlotProvider` and `IProjectContextService` leave the edit
  path.

## 5. Callers

| Caller                                                                 | Today                                                                         | After                                                             |
| ---------------------------------------------------------------------- | ----------------------------------------------------------------------------- | ----------------------------------------------------------------- |
| Scene editor toolbar                                                   | `Create*Async(context, kind, placement)`                                      | `InsertNodes` from `SceneNodeTemplates`                           |
| Gizmo, `TransformEditController`, `InspectorEditSessionCoordinator`    | `EditSessionToken.Begin/Commit/Cancel`                                        | `BeginGestureAsync`, `PreviewAsync`, `CommitAsync`, `CancelAsync` |
| Inspector "add component"                                              | `AddComponentAsync(context, node, type)`                                      | `SceneComponents.Add`                                             |
| Inspector sections (geometry, play, references, scripting, components) | the matching service method                                                   | the matching edit                                                 |
| Atmosphere lights reset, camera move                                   | `BeginChangeSet` around several calls                                         | one `SceneEditBatch`                                              |
| Scene explorer                                                         | 15 service methods, its own gate, `suppressNodeMessages`, `History.UndoAsync` | edits and batches, `ValidateAsync`, `Changed`, `UndoAsync`        |
| `SceneEditorViewModel` close and reload                                | `CompleteEditSessionsAsync`, `BeginReplacementAsync`, `Retire`                | `Document` close and reload; the content disposes the authoring   |
| `SceneEditorViewModel.Relocation`                                      | `RelocatedRuntimeRefresh`, `RefreshRelocatedRuntimeAsync`                     | nothing; the projector (3.11)                                     |
| `SceneCookInputRegistrar`                                              | `HasEditInProgress`, `SceneAuthoringGate.TryEnter` read lease                 | `HasEditsInProgress`, `Document.AcquireReadLease()`               |
| `DocumentManager`                                                      | `BeginInstall`/`CompleteInstall`                                              | nothing                                                           |

The explorer evaluates command enablement synchronously from `CanExecute`; it
precomputes enablement with `ValidateAsync` when it captures a context and
raises `CanExecuteChanged`.

`SceneDocumentCommandContext` goes.

## 6. Module layout

```text
Oxygen.Editor.Scenes
  Authoring/    SceneAuthoring, ISceneEdit, SceneEditScope, SceneEditApplication, SceneEditBatch,
                SceneHistoryEntry, SceneGesture, SceneEditResult, SceneDiagnostic, SceneEditValidation
  Changes/      SceneChange and its records, SceneChangedEventArgs, EditPhase
  Edits/        Hierarchy/, Layout/, Components/, Properties/, Assets/ (geometry, material slots),
                Settings/ (play, references, scripts), EditorView/
  Properties/   descriptors, IComponentRules per kind, engine mappings
  Nodes/        SceneNodeTemplates, SceneNodeCopies, SceneClipboard, SceneComponents
  Documents/    SceneDocumentKind, SceneContent, SceneFiles
  Selection/    keyed by DocumentId
  Engine/       the port: register, close, and the change stream it consumes
```

Namespaces follow the folders under `Oxygen.Editor.Scenes`. Edit records are
public (callers and remote control construct them); their `Validate`/`Apply` are
explicit interface implementations. The mutator and organizer logic becomes the
hierarchy and layout edits; `ISceneMutator`, `ISceneOrganizer`,
`SceneNodeChangeRecord` and `LayoutChangeRecord` go.

## 7. Changes outside Scenes

- **World**: typed ids (3.9).
- **Schemas**: immutable `PropertyEdit`; descriptors carry their engine mapping.
- **TimeMachine**: `Undo`/`Redo` return `bool` and restore a failed entry (3.4).
- **Documents**: `DocumentId`; `Document.AcquireReadLease()` (3.12).
- **Engine sync**: becomes the change projector (3.7, 3.11).
- **WorldEditor views**: section 5.

## 8. Tests

`Oxygen.Editor.Scenes.Tests`, no WinUI host, through the public API only:

- per edit: validate refusals, the scene after apply, the changes raised, and
  the revert restoring the scene exactly (compared through `Dehydrate()`);
- the frame: retired, locked, stale targets, undo and redo through
  `HistoryKeeper`, a refused replay keeping its entry, `ChangesDocument = false`
  keeping the position;
- batches: a refusal part way leaves the scene, the history and the change
  stream untouched; a batch that creates then edits; `ValidateAsync` of a batch
  leaves the scene untouched;
- gestures: preview, commit as one entry, cancel, a target removed during a
  gesture, a drag back to its start not in progress, `FinishEditsAsync`;
- coupling: undo of a rotation edit restores all three axes;
- layout: a cross-scope move refused alone and applied as a batch; reparent and
  its undo keep the nested folders and expanded state of the moved entry;
- every edit round-trips through JSON (rule 1);
- calls from another thread run on the scene thread (rule 4).

TimeMachine and Documents test their changes in their own projects. The World
Editor's tests keep testing the views against a real `SceneAuthoring`.

## 9. Order of work

Tasks 1 and 2 are done, and so is the document identity design
([document-identity.md](document-identity.md)) that came between tasks 2 and 3.
The next task is 3.

Each task builds, passes its tests, and deletes the code it replaces. The old
service and `SceneAuthoringGate` keep working until their last caller moves, so
the gate goes only in the last task.

1. **Scene ids:** `NodeId`, `ComponentId`, `SceneId` in Managed.Core and
   `FolderId` in World, with JSON converters; consumers in World, Runtime (the
   engine API's node and scene ids), Scenes, ContentPipeline (scene descriptor
   generator and JSON) and WorldEditor; `ISceneNodeInteraction` keyed by
   `NodeId`.
2. **Documents:** `DocumentId` in Documents and `Document.AcquireReadLease()`;
   consumers in Scenes, MaterialEditor and WorldEditor. The cook keeps no
   document identity: its registration handle identifies the owner. Aura's tab
   contract and the Runtime's surface ownership stay `Guid` and convert at their
   boundary.
3. **TimeMachine:** `Undo`/`Redo` return `bool` and restore a failed entry.
   Independent.
4. **Test project:** create `Oxygen.Editor.Scenes.Tests` and move the command
   service, selection, mutator and organizer unit tests out of the World Editor
   as they are. After task 1.
5. **Frame:** `SceneAuthoring`, `ISceneEdit`, `SceneEditScope`,
   `SceneEditApplication`, batches, history entries, the change stream, results,
   `ValidateAsync`, `UndoAsync`/`RedoAsync`, `SceneContent.Authoring`. After
   tasks 1 to 3.
6. **Engine projector:** `SceneEngineSync` consumes `Changed` (phases,
   registration boundary, relocation mount wait, role transfer order); the
   per-operation methods stay until their last caller moves. After task 5; your
   check in the editor.
7. **Settings and editor view:** `SetScenePlay`, `SetSceneReferences`,
   `SetScriptSlots`, `SetEditorHidden`; their partials go. After task 6.
8. **Properties:** immutable `PropertyEdit`, descriptor engine mappings,
   `IComponentRules`, `SetProperties`, `SetSceneProperties`, gestures; gizmo,
   transform controller, inspector coordinator, camera move, atmosphere reset;
   the typed edits, adapters, builders, `EditSessionToken` and the gestures
   partial go. After task 6.
9. **Components:** `AddComponent`, `RemoveComponent`, `SceneComponents.Add`.
   After task 8.
10. **Layout:** folder edits, `MoveLayoutEntries`, `LayoutScopes`;
    `ISceneOrganizer` goes. After task 6.
11. **Hierarchy:** `InsertNodes`, `RemoveNodes`, `RenameNode`, `ReparentNodes`,
    `ReorderNode`, `PlaceNodes`, `SceneNodeTemplates`; `ISceneMutator` and the
    hierarchy and create methods go. After task 10.
12. **Copies and clipboard:** `SceneNodeCopies`, `SceneClipboard`, paste, cut
    and duplicate as batches; the clipboard and duplicate partials go. After
    task 11.
13. **Geometry and material slots:** `SetGeometry`, `SetMaterialSlot`,
    `RemoveMaterialSlots`, the inventory helper. After task 8.
14. **Removal:** the service, its interface, `SceneDocumentCommandContext`,
    `SceneAuthoringGate`, the relocation partial, the per-operation port methods
    and `SceneContent.Relocated`; `DocumentManager`, the cook registrar, the
    scene editor and the explorer drop the gate. After all.
