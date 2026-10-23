# Scene Edits LLD

Status: `agreed`

Last Updated: 2026-10-20 01:32

## 1. Purpose

Define how every change to a scene is made in `Oxygen.Editor.Scenes`: one
`SceneAuthoring` per open scene document runs serializable edit records through
one frame that owns the thread, lifetime, targets, locks, atomicity, history,
change notification and result. The module that holds this design, and the four
mandatory edit rules it implements, are in
[scenes-module.md](scenes-module.md#56-scene-edits).

## 2. PRD Traceability

| ID         | Coverage                                                                                                         |
| ---------- | ---------------------------------------------------------------------------------------------------------------- |
| `REQ-004`  | Insert, remove, rename, reparent and reorder nodes are hierarchy edits (5.15).                                   |
| `REQ-005`  | Add and remove component edits, and one property path for every component and the environment (5.11).            |
| `REQ-006`  | Every change is an edit recorded into its document's history by one frame; batches leave no partial entry (5.4). |
| `REQ-008`  | The change stream (5.8) is what the live preview projects; no edit calls the engine.                             |
| `REQ-012`  | `SetOverrides` against the geometry's surface inventory (5.16).                                                  |
| `REQ-043`  | `SetScriptSlots` with undo, and retargeting of asset references held in history (5.12).                          |
| `REQ-044`  | `SetStartingCamera` and `SetStartingInputContexts` author what Play starts with.                                 |
| `GOAL-006` | Every refusal is a result with coded diagnostics; an exception from an edit is a defect, never swallowed (5.9).  |

## 3. Architecture Links

- [scenes-module.md](scenes-module.md): the Scenes module, the scene document
  kind and the mandatory edit rules.
- [editor-documents.md](editor-documents.md): one history per document, handed
  to the domain code that records into it.
- [document-kinds.md](document-kinds.md): the common document behavior (reload,
  relocation) that `SceneContent` takes part in.
- [live-preview.md](live-preview.md): the preview that projects the change
  stream into the engine.
- [editor-module-architecture.md](editor-module-architecture.md#72-scenes): the
  Scenes module's place in the layers.

## 4. Current Baseline

The code follows this design.

## 5. Target Design

### 5.1 The model

One `SceneAuthoring` per open scene document executes every change to the scene.
A change is an `ISceneEdit`: a serializable record of ids and values that
validates against the scene, applies itself, and returns its exact inverse and
the changes it made. The frame (thread, lifetime, targets, locks, atomicity,
history, change notification, result) is written once, in `SceneAuthoring`. The
live preview, the explorer, selection and Play follow the change stream; no edit
knows they exist.

```text
caller (view, MCP) ── ExecuteAsync(edit) ──► SceneAuthoring (scene thread)
                                              ├ retired? targets exist and unlocked? valid?
                                              ├ edit.Apply(scope) → revert + changes
                                              ├ history.AddChange(entry(revert))
                                              ├ Changed(changes) ──► LivePreview (ScenePreview) ─► interop
                                              │                  ──► explorer, selection, Play
                                              └ SceneEditResult
```

Why: a frame repeated by hand in every command drifts (one check missed, one
undo closure wrong); two property pipelines disagree; and grouping without
rollback leaves partial entries. Writing the frame once, making each edit return
its own exact inverse and making multi-step actions atomic batches removes all
three.

### 5.2 `SceneAuthoring`

```csharp
public sealed partial class SceneAuthoring : IDisposable
{
    internal SceneAuthoring(
        DocumentDecodeContext document, Scene scene, IList<ExplorerEntryData> layout,
        ISceneNodeInteraction? interaction, IAssetRedirects? redirects);

    public DocumentId Document { get; }
    public Scene Scene { get; }                        // read access for views and queries
    public IList<ExplorerEntryData> Layout { get; }    // the explorer layout, read access
    public bool IsRetired { get; }
    public event EventHandler<SceneChangedEventArgs>? Changed;

    public Task<SceneEditResult> ExecuteAsync(ISceneEdit edit);
    public Task<SceneEditValidation> ValidateAsync(ISceneEdit edit);  // dry run, no mutation
    public Task<SceneEditResult> UndoAsync();
    public Task<SceneEditResult> RedoAsync();

    public Task<SceneGesture> BeginGestureAsync(
        IReadOnlyCollection<NodeId> targets,      // empty: the scene's own properties
        IReadOnlyCollection<PropertyId> properties,
        string label);
    public bool HasEditsInProgress { get; }
    public Task FinishEditsAsync();                    // commits open gestures
    public void Dispose();                             // retires: every later call is refused
}
```

- `SceneContent` creates it when the document decodes and owns it. Views reach
  it through `SceneContent.Of(document).Authoring`.
  `IDocumentContent.HasEditsInProgress` and `FinishEditsAsync` delegate to it.
  Its `Dispose` (content replaced by a reload, document closed) cancels open
  gestures and retires it. The content holds no service.
- **It is complete when it is constructed.** The document service creates the
  document's `DocumentId` and `HistoryKeeper` before it decodes the file, and
  `IDocumentKind.DecodeAsync` receives them in a `DocumentDecodeContext` (5.14).
  `SceneContent` builds the authoring from them at decode; the `Document` then
  takes the same id and history. A reload decodes with the document's own id and
  history, so the new authoring records into the same history the document
  clears. The authoring records into the history it is given and never creates,
  finds or clears one ([editor-documents.md](editor-documents.md#51-decisions)
  decision 1). There is no later binding step and no authoring that exists
  without its history.
- It is constructed with the `SynchronizationContext` current at decode (the UI
  thread in the editor, a single-threaded context in the Scenes tests). Every
  public call posts to that context; the edit then runs synchronously there.
  Because validation and application are synchronous and run on one thread,
  edits cannot interleave: no lock, no `AsyncLocal`, no operation counting, and
  no global gate.
- When no `SynchronizationContext` is current at decode, every public call runs
  inline on the caller's thread, and the caller keeps its calls serialized. The
  editor always decodes on the UI thread; the Scenes tests that cover rule 4
  decode under a single-threaded context; code that opens scenes on a plain
  thread, such as unit tests of the views, works unchanged.
- `ValidateAsync` serves the views' "can execute" checks, including paste.

### 5.3 `ISceneEdit`

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

- **Records of ids and values.** Allowed members: typed ids, primitives,
  `AssetPath`, `Vector3`, `Quaternion`, enums, `PropertyEdit` (immutable, 5.11)
  and serialization records. A serialization record is deep-copied when the edit
  is constructed, so the edit owns it and never shares a list with the live
  scene. Live model objects (`Scene`, `SceneNode`, `GameComponent`) are never
  members.
- **The apply contract.** `Apply` is called only after `Validate` succeeded on
  the current scene. It either applies completely and returns `Applied`, or
  leaves the scene exactly as it found it and returns `Rejected`. A leaf edit
  validates everything it relies on, so it always returns `Applied`. Only a
  composite (5.6) can return `Rejected`. An exception from `Apply` is a defect,
  not a result (5.4).
- **Nothing to change.** An edit that finds the scene already as it asks
  (renaming to the same name, equal values, hiding a hidden node) returns
  `Applied` with no changes. The frame then records nothing and raises nothing,
  and the result is `Applied`. A replay always records its inverse, so undo and
  redo stay paired.
- **Refuse, never clamp.** A value outside its allowed range is refused as
  `Invalid` with the range in the message; the scene holds exactly what the
  caller sets. The range of a component property is the one the scene property
  schema generates for it ([scene model](scene-authoring-model.md) 5.3), and
  `PropertyChecks` is its only check and its only message: the descriptor's
  validator reports it with a generic range code, the component's setter and
  `Hydrate` throw on it, so loading a file refuses an out-of-range value
  too. A rule that relates several values (near below far, the spot cone pair,
  increasing cascade distances, representable light intensity) stays in the
  component's rules; for lights `LightValidation` checks those rules alone and
  reports the broken `LightRule` with its properties, which the rules refuse as
  `PropertiesInconsistent`. Clamping is the view's: an inspector edit commits
  through `PropertyChecks.Commit`, which clamps a property whose range declares
  `commit: clamp` before the edit is built, so the edit receives a value in
  range. Values of other properties reach the edit as entered, and it refuses
  them.
- **Exact inverse.** The revert restores exactly the state the edit replaced,
  captured during `Apply`, including state the edit changed as a consequence
  (layout entries, compensated transforms, coupled properties). It is never a
  recomputation and never a whole-layout or whole-scene snapshot. The revert's
  own revert is the redo.
- `SceneEditScope` gives the edit the scene, the explorer layout, the
  node-interaction port (`Interaction`, 5.10), the asset redirects (`Redirects`,
  with `Resolve`, `Moved` and `WarnIfDeleted`, 5.12) and the checks of the
  frame: `CheckTargets(ISceneEdit)` (5.4 step 2), `CheckScope` and
  `CheckSubtrees` (5.9). Component rules are reached through the property
  descriptors' kinds (5.11). Edits receive nothing else.

### 5.4 The frame

`ExecuteAsync(edit)`, on the scene thread:

1. Refused if retired.
2. `CheckTargets(edit)`: each target exists; refused if a target or one of its
   ancestors is locked.
3. `edit.Validate(scope)` for a leaf edit; refused with its diagnostics if it
   fails. A batch is not validated here: its `Apply` validates each inner edit
   and rolls back (5.6).
4. `edit.Apply(scope)`; `Rejected` ends here with its diagnostics.
5. `history.AddChange(new SceneHistoryEntry(this, applied.Revert, label))`.
6. `Changed` is raised with `applied.Changes`.
7. Returns `SceneEditResult` with outcome `Applied`. New node, component and
   folder ids are in the edit, chosen by its builder, so the caller already
   holds them.

If `Apply` throws, the frame does not catch it: the scene state is undefined and
the exception propagates.

### 5.5 Undo and redo

`SceneHistoryEntry : IChange` (internal) holds the `SceneAuthoring`, the edit to
apply and its label. Its `Apply` runs steps 1, 3, 4 and 6 of the frame. It skips
the lock check: a lock protects nodes from new authoring, and replaying the
history returns the scene to states the user already made. The inverse is
recorded by the history itself: the entry adds it during `Undoing`/`Redoing`,
which `HistoryKeeper` routes to the other stack. The entry is named after the
edit people made, not its revert: after `RenameNode` to "Box" the history shows
the rename to "Box", and the inverse recorded during undo or redo keeps that
name. Its `ChangesState` is the edit's `ChangesDocument`.

If the authoring is retired, validation fails, or `Apply` returns `Rejected`
during replay (the scene changed in a way the entry does not expect), the entry
throws `SceneReplayRefusedException` before adding any inverse, so the history
restores it.

The TimeMachine contract this relies on: `HistoryKeeper.Undo` and `Redo` return
`bool`; when the change throws, they restore the entry to its stack and position
and return `false`, as `UndoAsync`/`RedoAsync` restore it. `SceneAuthoring`
posts `UndoAsync`/`RedoAsync` to the scene thread and calls `Undo`/`Redo`. Every
scene undo goes through `SceneAuthoring`; no view calls the history directly.

`UndoAsync` and `RedoAsync` return a `SceneEditResult`, as `ExecuteAsync` does:
`Applied` with the replay's warnings (a restored reference to an asset deleted
since the edit reports `AssetReference.Deleted` on its node), `Invalid`
[`Edit.Empty`] when there is nothing to undo or redo, `Refused` when the entry
cannot replay (it stays on its stack), and `Retired`.

### 5.6 Atomic batches

```csharp
public sealed record SceneEditBatch(string Label, IReadOnlyList<ISceneEdit> Edits) : ISceneEdit;
```

- `Targets` is empty: each inner edit is checked when it runs, against the scene
  as the previous edits left it. This is what lets a batch create a node and
  then edit it.
- A batch with no edits is `Invalid` [`Batch.Empty`]. A batch whose edits change
  nothing records nothing, by the rule of 5.3.
- `Apply` runs, for each inner edit in order, `scope.CheckTargets`, `Validate`
  and `Apply`. If one is refused or rejected, it applies the reverts of the
  edits already applied, in reverse order, and returns `Rejected` with that
  edit's diagnostics. The scene is as before, nothing is recorded, no change is
  raised. A revert returning `Rejected` during this rollback is a defect and
  throws: an exact inverse (5.3) cannot be refused on the state its edit left.
- `Validate` of a batch runs the batch on a scratch scene, hydrated from the
  current scene's `Dehydrate()`, with a scratch view of the node-interaction
  port that reads the live locks and keeps its own hidden flags. It returns the
  first failure. Only `ValidateAsync` uses it, for the views' "can execute"
  checks; execution relies on the rollback above.
- The revert is a batch of the inner reverts in reverse order. The changes are
  the concatenation of the inner changes.

Every multi-step user action is one batch: delete, move, group, duplicate and
paste of several items, the atmosphere lights reset and the camera move in the
views, and the explorer's context actions. TimeMachine change sets have no
rollback and never group a scene action.

### 5.7 Gestures

A gesture is a property edit that previews many times and records once.

```csharp
public sealed class SceneGesture
{
    public string Label { get; }
    public bool IsEnded { get; }
    public Task<SceneEditResult> PreviewAsync(SetProperties values);
    public Task<SceneEditResult> PreviewAsync(SetSceneProperties values);
    public Task<SceneEditResult> CommitAsync();   // one history entry: before → last preview
    public Task CancelAsync();                    // restores before, no entry
}
```

- `BeginGestureAsync` checks targets and locks once, and captures the before
  state of the targets for the named properties and the properties coupled to
  them (5.11). It applies nothing; the first `PreviewAsync` writes values, so
  the inspector can begin on focus and the gizmo at its first move. A refused
  begin throws `SceneEditRefusedException`, since it has no result to carry the
  refusal. A preview with other targets or other properties is refused
  [`Gesture.Mismatch`].
- A preview validates, writes the values, and raises
  `PropertiesChanged(..., Phase: Preview)`. It records nothing.
- Commit records one `SetProperties` entry whose revert restores the before
  state, and raises `Phase: Commit`. Cancel restores the before state and raises
  `Phase: Cancel`. A commit whose values equal the before state records nothing.
- If a target is removed or replaced while the gesture is open (an undo in
  another view), the gesture is cancelled at its next call and that call reports
  it [`Gesture.Cancelled`].
- The gestures belong to the `SceneAuthoring`. `HasEditsInProgress` is true
  while an open gesture's current values differ from its before state, so a drag
  returned to its start does not hold a save or a cook. `FinishEditsAsync`
  commits them all; `Dispose` cancels them.
- The frame never commits or cancels a gesture implicitly: an edit, undo or redo
  runs beside an open gesture, which a removed or replaced target cancels at its
  next call. A view that undoes during a drag calls `FinishEditsAsync` first.
- Gestures are a UI mechanism. Remote callers use one-shot edits.

### 5.8 The change stream

`SceneChangedEventArgs(DocumentId Document, IReadOnlyList<SceneChange> Changes)`,
raised on the scene thread after each edit, replay, preview and relocation, in
order. A change names what changed, by id; consumers read the values from the
scene, which already holds them.

| Change                                                                                     | Raised by                                   |
| ------------------------------------------------------------------------------------------ | ------------------------------------------- |
| `NodesAdded(roots)`                                                                        | insert                                      |
| `NodesRemoved(roots)`                                                                      | remove                                      |
| `NodesPlaced(nodes)`                                                                       | reparent, reorder, placement restore        |
| `NodeRenamed(NodeId)`                                                                      | rename                                      |
| `ComponentAdded(NodeId, ComponentId)`                                                      | add component                               |
| `ComponentRemoved(NodeId, ComponentId, string kind)`, the kind `SceneComponents.Add` takes | remove component                            |
| `PropertiesChanged(NodeId? node, properties, EditPhase)`                                   | properties (`node` null: scene)             |
| `GeometryChanged(NodeId)`                                                                  | geometry asset                              |
| `OverridesChanged(NodeId, properties)`                                                     | overrides, such as surface materials        |
| `LayoutChanged`                                                                            | every edit that changes the explorer layout |
| `EditorVisibilityChanged(nodes)`                                                           | hide or show in editor                      |
| `StartingCameraChanged`, `StartingInputContextsChanged`, `SceneReferencesChanged`          | scene settings                              |
| `ScriptSlotsChanged(NodeId)`                                                               | script slots                                |

Node and property lists are `IReadOnlyList<NodeId>` and
`IReadOnlyList<PropertyId>`. `EditPhase` is `Commit` (a one-shot edit, an undo
or redo, a committed gesture), `Preview` or `Cancel`.

Consumers:

- **LivePreview's scene preview** (`ScenePreview`, behind `IScenePreview`)
  translates changes into interop calls; nothing in Scenes calls the engine
  ([live-preview.md](live-preview.md)). It owns:
  - preview throttling and the preview, commit and cancel phases;
  - the directional-light role transfer order: within one change set, the light
    that releases an atmosphere slot is projected before the light that takes
    it;
  - engine field encoding: LivePreview's `SceneWire` maps each `PropertyId` to
    its engine component, fields and encoding, with the field ids Interop
    publishes from the native constants; descriptors carry no engine mapping;
  - `NodesPlaced`: push the node's current local transform, then reparent
    without compensation; the scene already holds the compensated transform;
  - waiting for relocated assets to be mounted before attaching them (5.12);
  - registration: `RegisterDocument` snapshots the scene on the document's owner
    thread; every `Changed` raised after that point is projected after the full
    sync, and none raised before it;
  - reporting engine outcomes.
- **LivePreview's content demand** follows `NodesAdded`, `NodesRemoved`,
  `ComponentAdded`, `ComponentRemoved`, `GeometryChanged` and
  `OverridesChanged`.
- **Selection** drops removed nodes: the selection is reconciled with the scene
  when nodes are removed. Edits do not set the selection; a caller that wants
  created nodes selected selects the ids it put in the edit.
- **The explorer and Play** follow the stream for structure; they keep no
  suppression flags.

### 5.9 Results

```csharp
public sealed record SceneEditResult(
    SceneEditOutcome Outcome,                     // Applied, Refused, Invalid, Retired
    IReadOnlyList<SceneDiagnostic> Diagnostics);

public sealed record SceneDiagnostic(
    bool IsError, string Code, string Message, NodeId? Node = null, PropertyId? Property = null);
```

- `Refused`: a target is missing or locked. `Invalid`: the edit does not fit the
  scene. `Retired`: the scene was closed or replaced by a reload. Nothing
  changes in any of the three.
- **The code is the contract.** Callers, remote clients and tests branch on
  `Code`; the message is for people and never a contract. The codes are the
  constants of `SceneEditCodes` (Authoring), one per rule category, each
  documented on its constant. A new rule reuses a category; a new category is a
  change to this design. The values keep the editor's `OXE.SCENE.` prefix, so a
  view copies `Code` into the operation result's diagnostic unchanged.
- A value a descriptor rejects reports `Property.OutOfRange` and names the
  property; rules across fields or nodes report `Properties.Inconsistent`,
  naming the node whose candidate state breaks the rule (none for the scene
  environment) and no property, since the rule spans fields.

Scenes posts nothing and references no operation-result publisher. The views map
results to operation results; the Shell decides what to show. Engine outcomes
are reported by LivePreview, not returned by `ExecuteAsync`: the edit succeeded
when the scene changed.

Layout edits name no target node, since their scope depends on where the entries
are; they check their scope node with `SceneEditScope.CheckScope`, which refuses
a missing or locked node as the frame does. Edits that remove or move whole
subtrees (`RemoveNodes`, `ReparentNodes`) also refuse a locked node under a
root, naming it, through `SceneEditScope.CheckSubtrees`. A replay checks that
its targets exist, but no lock.

### 5.10 Ids

`NodeId`, `ComponentId` and `SceneId` (`Oxygen.Managed.Core`, shared with the
Runtime and the Interop, so the engine API takes them and the `Guid` is unboxed
only at the native boundary), `FolderId` (Scenes, explorer layout) and
`DocumentId` (`DroidNet.Documents`: documents are an editor concept, so neither
Managed.Core nor the Runtime knows them) are `readonly record struct` wrappers
of `Guid` with JSON converters that keep the file format unchanged.
`SceneNode.Id`, `GameComponent.Id`, `Scene.Id`,
`ExplorerEntryData.NodeId/FolderId` and `Document.Id` use them. Edits, changes,
results, selection and the engine port use them; no `Guid` and no `SceneNode`
crosses those APIs.

The node-interaction port is `ISceneNodeInteraction` (Authoring), implemented by
`SceneNodeInteraction` (Interaction), which keeps and persists the state:

```csharp
public interface ISceneNodeInteraction
{
    bool IsHidden(NodeId nodeId);
    bool IsLocked(NodeId nodeId);
    SceneNode? GetLockOwner(SceneNode node);   // the node or its closest locked ancestor
    void SetHidden(NodeId nodeId, bool isHidden);
}
```

### 5.11 One property path

- `SetProperties(IReadOnlyDictionary<NodeId, PropertyEdit> edits, string label)`
  edits the properties of any node component, and
  `SetSceneProperties(PropertyEdit edit, string label)` the scene environment's.
  Each captures, during `Apply`, the before values of the edited properties and
  of the properties coupled to them; its revert writes those values back. Its
  `PropertiesChanged` lists the edited and coupled properties with the changed
  ones, so a view refreshes every axis a rotation edit can move. The environment
  is the exception: one property can rewrite records no property names (the
  backdrop rewrites the background and the sky sphere), so `SetSceneProperties`
  and environment gestures revert with `SetSceneEnvironment`, which restores the
  whole environment and names the properties whose values differ.
- `PropertyEdit` is immutable (built with `PropertyEdit.Builder`) so an edit can
  own it. Its keys are `PropertyId(ComponentKind, JsonPointer)` and its values
  are the descriptor's value type, which is serializable.
- One `IComponentRules` per component kind holds the kind's descriptors and its
  domain rules:
  - **coupling** (`Coupled`): the properties whose before state must be captured
    with an edited one (editing `Transform.RotationX` captures all three Euler
    axes, because normalization can change the other displayed axes);
  - **validation** (`Validate`): cross-field and cross-node rules against the
    candidate state (camera near < far, directional light cascades, the single
    atmosphere sun, the environment constraints), written against
    `PropertyTargetEdit` and the candidate component state;
  - **defaults on add** (`OnAdd`): the consequences of adding the component
    (5.11.1).

  Per-field validation stays on the descriptor (`ValidateBoxed`).

- Descriptors describe authoring only. Engine encoding is LivePreview's
  (`SceneWire`, 5.8). There are no typed per-component edits, no adapters
  between two pipelines and no hand-built engine entries per component.

#### 5.11.1 Adding a component

`SceneComponents.Add(SceneAuthoring, NodeId, string kind)` builds the edit for
the inspector's "add component": `AddComponent(node, data, index)`, followed in
one batch by the edits its component rules require (`OnAdd`). Adding a
directional light to a node with identity rotation sets
`DirectionalLightComponent.DefaultLocalRotation`. The revert falls out of the
batch.

### 5.12 Asset references

- **In history.** An entry recorded before an asset moved holds the old
  `AssetPath`. Edits that carry asset references (`SetGeometry`,
  `SetOverrides`, `InsertNodes`, `SetStartingInputContexts`,
  `SetSceneReferences`, `SetScriptSlots`, `SetSceneProperties`) retarget them
  through `scope.Redirects` in `Validate` and `Apply`, and report a reference to
  a deleted asset as an `AssetReference.Deleted` warning naming the node that
  holds it (none for the scene's own references).
- **On relocation.** `SceneContent.ApplyRelocation` changes the scene outside
  the history (the relocation's undo is global). It runs on the scene thread
  through the authoring and raises `GeometryChanged` for every retargeted node
  and `PropertiesChanged(null, ...)` when the environment changed. LivePreview
  attaches a relocated asset once it is mounted; the views do nothing on a
  relocation.
- **On reload.** A reload replaces the content, so the new `SceneAuthoring`
  raises nothing; the views and LivePreview follow `Document.ContentReplaced`.

### 5.13 Reading a document while it is cooked

The cook reads saved files only and holds nothing of an open scene document. It
opens the files it reads with `FileShare.Read | FileShare.Delete`: in-place
writes stay excluded, and a save's atomic replace proceeds; the cook keeps
reading the replaced file, and the save is a change of its own that is cooked
again. Before an explicit cook or Play, the unsaved documents it reads,
including one with an edit in progress (`HasEditsInProgress`), are saved after
asking ([editor-documents.md](editor-documents.md#533-asking-before-play-and-cook)).

### 5.14 Decoding with the document's identity and history

```csharp
public sealed record DocumentDecodeContext(DocumentId Document, HistoryKeeper History);

Task<DocumentDecodeResult> DecodeAsync(
    DocumentFile file, ReadOnlyMemory<byte> content, DocumentDecodeContext context, CancellationToken cancellationToken);
```

- `DocumentDecodeContext` is in `Oxygen.Editor.Documents`, next to
  `IDocumentKind`.
- `OpenDocuments` creates the id and the history, decodes with them and
  constructs the `Document` with the same pair; `Document` does not create its
  history. On a failed decode, both are dropped.
- `Document.ReplaceContentAsync` decodes with its own id and history, takes the
  new content, clears the history and disposes the replaced content. The
  replaced authoring is retired by that dispose;
  the cleared history holds none of its entries.
- A kind whose content needs neither (material) ignores the context. A kind
  never stores the history beyond the content it decodes.

### 5.15 Edit catalogue

Types used below:

```csharp
public sealed record NodePosition(NodeId? Parent, int Index);                       // scene graph
public sealed record LayoutPosition(FolderId? Folder, NodeId? Node, int Index);     // explorer layout
public sealed record NodeInsert(SceneNodeData Node, NodePosition At, LayoutPosition Layout, ExplorerEntryData? LayoutEntry = null);
public sealed record NodePlacementState(NodeId Node, NodePosition At, TransformData Local, LayoutPosition Layout);
public readonly record struct LayoutEntryId(NodeId? Node, FolderId? Folder);       // a node or a folder entry
```

| Edit                                                                                                                                                                                                    | Revert                                                                                                   |
| ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------- |
| `InsertNodes(IReadOnlyList<NodeInsert>, string? label)`                                                                                                                                                 | `RemoveNodes(roots)`                                                                                     |
| `RemoveNodes(IReadOnlyList<NodeId> roots)`                                                                                                                                                              | `InsertNodes` with each root's data, positions and layout entry subtree (folders under it, `IsExpanded`) |
| `RenameNode(NodeId, string)`                                                                                                                                                                            | `RenameNode` with the old name                                                                           |
| `ReparentNodes(IReadOnlyList<NodeId>, NodeId? parent, bool preserveWorld, NodeId? after)`                                                                                                               | `PlaceNodes` with each node's prior placement state                                                      |
| `ReorderNode(NodeId, NodePosition, LayoutPosition)`                                                                                                                                                     | `PlaceNodes` with the prior state                                                                        |
| `PlaceNodes(IReadOnlyList<NodePlacementState>)`                                                                                                                                                         | `PlaceNodes` with the states it replaced                                                                 |
| `CreateFolder(FolderId, LayoutPosition, string name, bool? isExpanded)`                                                                                                                                 | `RemoveFolder(id, promoteChildren: false)`                                                               |
| `RemoveFolder(FolderId, bool promoteChildren)`                                                                                                                                                          | `RestoreFolder` with its entry, position and its children's prior positions                              |
| `RestoreFolder(ExplorerEntryData entry, LayoutPosition, IReadOnlyList<LayoutEntryId> children)`                                                                                                         | `RemoveFolder`                                                                                           |
| `RenameFolder(FolderId, string)`                                                                                                                                                                        | `RenameFolder` with the old name                                                                         |
| `MoveLayoutEntries(IReadOnlyList<LayoutEntryId>, LayoutPosition to)`                                                                                                                                    | `PlaceLayoutEntries` with each entry's prior position                                                    |
| `PlaceLayoutEntries(IReadOnlyList<LayoutEntryPosition>)`                                                                                                                                                | `PlaceLayoutEntries` with the positions it replaced                                                      |
| `AddComponent(NodeId, ComponentData, int index)`                                                                                                                                                        | `RemoveComponent`                                                                                        |
| `RemoveComponent(NodeId, ComponentId)`                                                                                                                                                                  | `AddComponent` with its data at its old index                                                            |
| `SetProperties`, `SetSceneProperties`                                                                                                                                                                   | the captured before values, coupled ones included; `SetSceneEnvironment` for the environment (5.11)      |
| `SetGeometry(IReadOnlyCollection<NodeId>, AssetPath)`, or per node                                                                                                                                      | per node, the old geometry                                                                               |
| `SetOverrides(IReadOnlyCollection<NodeId>, IReadOnlyList<OverrideWrite>)`, or per node, an `OverrideWrite(Property, Part, LayoutRevision, Value)` writing, or with a null value resetting, one override | `SetOverrides` with each node's previous overrides                                                       |
| `SetStartingCamera(NodeId?)`, `SetStartingInputContexts(IReadOnlyList<StartingInputContextData>)`, `SetSceneReferences(SceneReferencesData)`                                                            | the old value                                                                                            |
| `SetScriptSlots(NodeId, ImmutableArray<ScriptSlot>)`                                                                                                                                                    | the old slots                                                                                            |
| `SetEditorHidden(IReadOnlyList<NodeId>, bool)` (`ChangesDocument = false`)                                                                                                                              | the old flags                                                                                            |
| `SceneEditBatch`                                                                                                                                                                                        | 5.6                                                                                                      |

`PlaceLayoutEntries` puts entries at positions captured from one layout, so they
return exactly where they were; `RestoreFolder` takes back, in order, the
entries its removal promoted to its place. Every edit that changes the layout
raises `LayoutChanged`.

- **Positions.** An index is in `[0, count]`, where `count` appends; any other
  index is `Invalid` [`Position.OutOfRange`], never clamped.
- **Removing a folder.** `RemoveFolder(promoteChildren: true)` puts the folder's
  children in its place in its parent, in their order.
  `RemoveFolder(promoteChildren: false)` on a folder that is not empty is
  `Invalid` [`Folder.NotEmpty`]; it serves as the revert of `CreateFolder`,
  which always meets an empty folder.
- **Hiding.** `SetEditorHidden` is presentation, not authoring: its `Targets` is
  empty, so a lock does not refuse it, and ids no longer in the scene are
  skipped. With no node-interaction port it is `Invalid`
  [`InteractionPort.Missing`].
- **The atmosphere sun.** Taking an atmosphere slot another light owns is
  `Invalid` [`AtmosphereSlot.Occupied`]; nothing transfers implicitly. Moving
  the sun is a batch: a `SetProperties` that clears the old owner's slot, then
  one that sets the new owner's.
- **Layout scope.** A node's layout entry lives in the scope of its scene
  parent: the root, or the entry of its parent node. `LayoutScopes.TryScopeOf`
  computes the scope of a position or of an entry, for the edits and for every
  caller. `MoveLayoutEntries.Validate` refuses a destination whose scope differs
  from a moved node's scene parent, or from the scene parents of a moved
  folder's nodes [`Layout.ScopeMismatch`]; an empty folder fits any scope. The
  scene parent decides, not where the entry is, so a batch that reparents first
  can then move the folders. Moving into another scope is a batch:
  `ReparentNodes(nodes, scope parent, preserveWorld: false, ...)` and then
  `MoveLayoutEntries`, which keeps the local transform (grouping).
- **Reparent and reorder keep the layout entry.** `ReparentNodes` and
  `ReorderNode` move a node's layout entry, with its nested folders and
  `IsExpanded`, to the destination scope; they never delete it. A
  `LayoutPosition` is therefore enough for `PlaceNodes` to put it back.
- **Geometry.** `SetGeometry` refuses a missing asset ("Remove the component to
  detach geometry."), and leaves the overrides untouched; unresolved surface materials are
  removed only by `SetOverrides` resetting them.
- The compensated transforms of `ReparentNodes` with `preserveWorld` raise no
  `PropertiesChanged`: views read them from the scene, and LivePreview pushes
  them on `NodesPlaced`.

### 5.16 Supporting code

These build edits; they are not edits.

- **`SceneNodeTemplates`**: the default nodes for primitives, lights, cameras,
  fog volumes and empty nodes (`Primitive`, `Light`, `Camera`, `LocalFogVolume`,
  `Empty`) as functions returning `SceneNodeData` with new ids, with an optional
  `NodePlacement`; `Append` builds the `NodeInsert` that appends a node under a
  parent or folder. They take enumerations of the primitive, light and camera
  kinds (`PrimitiveKind`, `LightKind`, `CameraKind`); an undefined value throws
  `ArgumentOutOfRangeException`, and parsing names belongs to the remote
  boundary.
- **`SceneNodeCopies`**: captures nodes as `SceneNodeData` with new node and
  component ids, remapping node references in script slots
  (`CopiedNodeReferences`) and clearing the atmosphere slot of copied
  directional lights, with an optional transform per root. It serves duplicate,
  duplicate in place and the clipboard. `InsertNodes.Validate` applies the
  component rules to inserted data, so a copy can never create a second sun; an
  inserted node id already in the scene is [`Node.Exists`], a component id
  [`Component.Duplicate`].
- **`SceneClipboard`**: the explorer clipboard (`Capture` of nodes and folders
  with their layout, or `Of` plain nodes) as data. `TryPaste` builds one batch
  from it: a copy creates its folders (`CreateFolder`) and inserts its nodes
  (`InsertNodes`) at the root; then, for a copy or a cut, `ReparentNodes` takes
  the top-level nodes to the target parent and `MoveLayoutEntries` refills the
  folders and places the top-level entries. Copied folders keep their names and
  `IsExpanded`. A cut moves items from one scope (the root or one node's
  children), else it fails [`Layout.ScopeMismatch`]; a removed cut node fails
  [`Target.Missing`]. Other problems, such as a cycle or a lock, are the built
  batch's to refuse. Duplicating to a target is a capture and a paste;
  `SceneNodeCopies.DuplicateInPlace` builds the `InsertNodes` of the viewport's
  duplicate.
- **Surface inventories** are read before the edit, never by it.
  `SurfaceEdits.SetAsync` and `ClearUnresolvedAsync` read the inventory
  through ContentPipeline's `IGeometrySurfaceProvider` and build
  `SetOverrides` with the surface ids and the inventory's `LayoutRevision`,
  refusing a surface the current inventory does not have. `SetOverrides` checks
  that the property is overridable, the part kind, and the value's type and rule
  (`OverrideRules`), and that each node's component still uses the write's
  `PartOwner` (otherwise `Invalid` [`Part.Stale`]; `Refused` is only for missing
  or locked targets). Its undo writes back the previous overrides. An assignment replaces the node's override of the surface, the revision
  it records included, whichever inventory the override came from: as in other
  engines, a reimport never locks a surface the inspector shows. Overrides no surface
  resolves show as unresolved, and a null-valued write clears them.
- **`LayoutScopes`** answers what callers need to build layout edits: the scope
  of a place or entry (`TryScopeOf`), an entry's place (`PositionOf`), a
  container's entries (`EntriesOf`) and its end (`EndOf`). A drop in the
  explorer is a paste of the dragged items (a copy for a Ctrl-drag, a cut
  otherwise), except a reorder under the same parent, which is a `ReorderNode`.
- **`SceneComponents.KindOf`** maps a component, such as one a menu offers, to
  the kind `SceneComponents.Add` takes.

### 5.17 Callers

| Caller                                                                 | Uses                                                              |
| ---------------------------------------------------------------------- | ----------------------------------------------------------------- |
| Scene editor toolbar                                                   | `InsertNodes` from `SceneNodeTemplates`                           |
| Gizmo, `TransformEditController`, `InspectorEditSessionCoordinator`    | `BeginGestureAsync`, `PreviewAsync`, `CommitAsync`, `CancelAsync` |
| Inspector "add component"                                              | `SceneComponents.Add`                                             |
| Inspector sections (geometry, play, references, scripting, components) | the matching edit                                                 |
| Atmosphere lights reset, camera move                                   | one `SceneEditBatch`                                              |
| Scene explorer                                                         | edits and batches, `ValidateAsync`, `Changed`, `UndoAsync`        |
| Scene editor close and reload                                          | `Document` close and reload; the content disposes the authoring   |
| Relocation of a referenced asset                                       | nothing in the views; LivePreview (5.12)                          |
| Unsaved documents before a cook or Play                                | `HasEditsInProgress`, through `IUnsavedDocuments`                 |

The explorer evaluates command enablement synchronously from `CanExecute`; it
precomputes enablement with `ValidateAsync` when it captures a context and
raises `CanExecuteChanged`.

### 5.18 The JSON form of edits

Rule 1's JSON form is `SceneEditJson.Options` (Authoring): it round-trips every
public `ISceneEdit` and every revert, polymorphic on `ISceneEdit` with the
`$edit` member (`SceneEditJson.TypeMember`) naming the type, property values in
their descriptor's value type, vectors and quaternions in the scene file's form
and script slots in their saved form.

## 6. Ownership

```text
Oxygen.Editor.Scenes
  Authoring/    SceneAuthoring, ISceneEdit, SceneEditScope, SceneEditApplication, SceneEditBatch,
                SceneHistoryEntry, SceneGesture, SceneEditResult, SceneDiagnostic, SceneEditValidation,
                SceneEditCodes, SceneEditJson, ISceneNodeInteraction
  Changes/      SceneChange and its records, SceneChangedEventArgs, EditPhase
  Edits/        Hierarchy/, Layout/, Components/, Properties/, Assets/ (geometry, surfaces),
                Settings/ (play, references, scripts), EditorView/
  Properties/   descriptors and IComponentRules per kind
  Nodes/        SceneNodeTemplates, SceneNodeCopies, SceneClipboard, SceneComponents
  Layout/       ExplorerEntryData, FolderId, the scene file data
  Documents/    SceneDocumentKind, SceneContent, SceneFiles
  Selection/    keyed by DocumentId
```

Namespaces follow the folders under `Oxygen.Editor.Scenes`. Edit records are
public (callers and remote control construct them); their `Validate`/`Apply` are
explicit interface implementations. Hierarchy and layout logic lives in the
hierarchy and layout edits; there is no separate mutator or organizer.

Outside Scenes: the engine projection and content demand are LivePreview's
(5.8); the surface inventory is ContentPipeline's.

## 7. Data Contracts

- Edit records, `SceneEditBatch` and their JSON form (5.3, 5.15, 5.18).
- `SceneChange` and `SceneChangedEventArgs` (5.8).
- `SceneEditResult`, `SceneDiagnostic`, `SceneEditOutcome` and the
  `SceneEditCodes` constants (5.9).
- The typed ids (5.10), `PropertyId` and immutable `PropertyEdit` (5.11).
- `DocumentDecodeContext` (5.14).

## 8. Commands, Services, Or Adapters

`SceneAuthoring` (5.2) is the only way to change a scene.
`ISceneNodeInteraction` is the one port Scenes declares for edits (5.10).
Builders of edits are static helpers (5.16). There is no scene command service.

## 9. UI Surfaces

None of its own. The views that call scene edits are listed in 5.17.

## 10. Persistence And Round Trip

Edits change the in-memory `Scene` and explorer layout; the scene document saves
them ([scenes-module.md](scenes-module.md)). The typed ids keep the file format
unchanged (5.10). Edits round-trip through `SceneEditJson` (5.18).

## 11. Live Sync / Cook / Runtime Behavior

Live sync follows the change stream in LivePreview (5.8). The cook reads saved
files; an explicit cook or Play saves the unsaved documents it reads first,
after asking (5.13).

## 12. Operation Results And Diagnostics

Edits return `SceneEditResult` with coded diagnostics; Scenes posts nothing
(5.9). Engine outcomes are LivePreview's.

## 13. Dependency Rules

- Nothing in Scenes calls the engine or references LivePreview, Runtime or
  WinUI.
- Edits hold ids and values only; no `Scene`, `SceneNode` or `GameComponent`
  member.
- Edits receive only their `SceneEditScope`.
- No view mutates the scene or calls its history directly; every change and
  every undo goes through `SceneAuthoring`.
- TimeMachine's `HistoryKeeper.Undo`/`Redo` restore a failed entry and return
  `false` (5.5).
- Documents creates each document's id and history before decoding and passes
  them in `DocumentDecodeContext` (5.14).
- Schemas' descriptors carry no engine mapping.

## 14. Validation Gates

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

TimeMachine and Documents test their contracts in their own projects. The World
Editor's tests test the views against a real `SceneAuthoring`; LivePreview's
test the projection.

## 15. Open Issues

None.
