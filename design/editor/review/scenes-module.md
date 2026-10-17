# Scenes module

Status: **implemented**. Open problems are in the register (section 6).

Scope: slice S1.6 of
[issue-069-di-topology-and-decomposition.md](../plan/issue-069-di-topology-and-decomposition.md):
`Oxygen.Editor.Scenes`, the domain module for scene authoring, and the scene
document kind of [document-kinds.md](document-kinds.md). It works within
[editor-module-architecture.md](editor-module-architecture.md) section 7.2 and
[editor-documents.md](editor-documents.md).

## 1. Scenarios

Actors:

- the user;
- the scene's views: the scene editor (viewports and gizmos), the Scene Explorer
  and the Inspector, which all edit one scene;
- the Content Browser, which creates, renames, moves, duplicates and deletes
  assets;
- the engine, which shows the scene through engine sync (LivePreview, S1.7);
- the cook, and tools outside the editor.

1. **Open a scene.** The user double-clicks a scene in the Content Browser, or
   the editor opens the project's start scene. The scene opens as a document,
   and the engine shows it. Opening it again brings it forward.
2. **Edit the scene.** The user adds a light, renames a node and sets its
   intensity. Each is one undo entry, and the engine follows each one.
3. **A multi-node action.** The user deletes, moves, groups, duplicates or
   pastes twelve nodes. Each is one undo entry with one label. If it fails part
   way, nothing of it is left: no partial undo entry, and no half-changed scene.
4. **A gesture.** The user drags a light's intensity. The engine previews every
   step, and the release records one entry. Saving, playing or cooking during
   the drag commits it first (scenario 2.7 of the documents design).
5. **Selection.** Selecting nodes in any of the three views selects them in the
   others, and the Inspector shows them.
6. **Save, undo back to clean, and changes outside the editor.** These follow
   the common document behavior of S1.5. A scene that is clean reloads quietly
   when its file changes, and the views and the engine show the reloaded scene.
7. **Rename or move a scene asset.** The user renames `Level1` to `Intro` in the
   Content Browser or at the root of the Scene Explorer, whether the scene is
   open or not. Its file is renamed, the scenes that reference it are rewritten,
   an open scene follows its file and takes the new title, and one Undo in the
   global history reverses the whole rename.
8. **Duplicate or delete a scene asset.** Duplicate writes a new scene with its
   own identity and the copy's name. Deleting an open scene closes it (decision
   6 of document kinds); the files go to the Recycle Bin. The project's start
   scene cannot be deleted, alone or with the folder that holds it.
9. **Create a scene.** New Scene writes an empty scene with a free name and
   opens it.
10. **Rename a material the scene uses.** The scene follows (S1.4), as any
    document does.

## 2. What happens today

### 2.1 Where the code is

| Code                                                                            | Size        | Lives in                    | Coupled to                                                                                                                     |
| ------------------------------------------------------------------------------- | ----------- | --------------------------- | ------------------------------------------------------------------------------------------------------------------------------ |
| Command service (`SceneDocumentCommandService`, 23 partials, and its interface) | 10.0k lines | WorldEditor `Documents`     | `WindowId` and Aura's `IDocumentService`, `IMessenger` (5 messages), `SceneDocumentMetadata`, the concrete interaction service |
| Edit records, edit sessions, results, descriptors and field helpers             | 3.9k lines  | same                        | Nothing outside World, Schemas and Managed                                                                                     |
| Selection service and its types                                                 | 0.3k lines  | WorldEditor `Documents`     | Nothing                                                                                                                        |
| Mutator and organizer                                                           | 1.9k lines  | WorldEditor `SceneExplorer` | The organizer has DynamicTree adapter code that nothing calls                                                                  |
| `ISceneEngineSync` and its value types                                          | 1.0k lines  | WorldEditor `Services`      | Two Runtime types and `SceneDocumentMetadata`                                                                                  |
| Authoring gate (`SceneAuthoringGate`)                                           | 0.2k lines  | WorldEditor `Documents`     | Nothing; internal, used by three view models                                                                                   |
| Node lock and visibility (`WorkspaceInteractionService`)                        | 0.4k lines  | WorldEditor `Workspace`     | Nothing outside Managed                                                                                                        |
| Preview, viewport and transform tool settings                                   | 1.0k lines  | WorldEditor `Workspace`     | Runtime types, viewport view model constants                                                                                   |
| `ScriptSlotEdits`                                                               | 0.2k lines  | WorldEditor `Scripting`     | `ScriptParameter` in the Content Browser                                                                                       |
| Scene lifetime, versions, rename and the scene list                             | 1.0k lines  | Projects                    | `IProject.Scenes`, `ProjectContext.Scenes`                                                                                     |

The command service receives `WindowId` and `IDocumentService` for two things
only: publishing a changed document's metadata, and asking whether the document
is the window's active one before a reload.

### 2.2 How scenes behave

| Concern              | Today                                                                                                                                                                                                                  |
| -------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Identity             | Three of them. The scene id inside the file keys the engine, the explorer, the history and the start scene. The name, which must equal the file name, keys every request. The asset URI keys cooking and file changes. |
| Where scenes are     | Only directly in `Content/Scenes`. The relocation service refuses to move a scene there or out of it.                                                                                                                  |
| The scene list       | `IProject.Scenes` is read from that folder when the project opens and then changed by hand by each operation. `ProjectContext.Scenes` is copied from it and goes stale after a rename, duplicate or delete.            |
| Open, save, reload   | Projects stages and accepts each load, keeps the versions, and writes the scenes of a project one at a time. A clean scene never reloads when its file changes; only missing is noticed.                               |
| Dirty                | A change counter: undoing back to the saved state stays dirty.                                                                                                                                                         |
| Rename               | A Projects operation, not the relocation service. Open, it is an entry in the scene's own history; closed, it has no undo. It reports no files, so the Content Browser refreshes the whole feed after it.              |
| Duplicate and delete | Handled by `DocumentManager` through `SceneAssetCommandMessage`, by name. Delete is refused for an open scene. Neither has an undo or reports its files.                                                               |
| Multi-node actions   | Change sets: eight in the command service, two in the views. They have no rollback, so a failure part way leaves a partial entry.                                                                                      |
| Notifications        | The command service sends five messages on the window's messenger (node added or removed, component added or removed, selection changed).                                                                              |

What makes this stiff:

1. Scenes are not assets like the others: they live in one folder, are addressed
   by name, keep their own list, and are renamed, copied and deleted by their
   own code, which reports nothing and keeps no global undo.
2. The scene list is a copy of the file system that each operation must update
   by hand, and some do not.
3. The domain code needs a window and a message bus, so its 230 tests compile
   against WinUI types, even though none needs the UI.
4. Change sets cannot roll back.

## 3. Proposal

### 3.1 The module

`Oxygen.Editor.Scenes` is a class library with no WinUI. It references World,
Schemas, Documents, Projects, ContentPipeline, Managed, TimeMachine and Storage
(decision 13). It holds:

- **The scene document kind** (`.oscene.json`): decode, capture, capture for a
  copy, finish edits, apply a relocation (S1.5). Its content holds the `Scene`
  and the document's `SceneAuthoring`. The decode checks that node ids are
  unique and that the explorer layout owns its ids.
- **Scene edits** (3.6): the edit records, `SceneAuthoring`, results, coded
  diagnostics, descriptors and field helpers. `SceneAuthoring` records into the
  history of the scene's document and reports every change on one stream
  (`Changed`).
- **Selection,** with its own event.
- **Node lock and visibility,** behind an interface, because edits obey it.
- **The engine sync port** (`ISceneEngineSync`), with its own value types and no
  Runtime type. `SceneEngineSync` and its projector stay in the World Editor
  until S1.7 moves them to LivePreview.

Creating a scene and the scene rules of the relocation service are not in
Scenes: they live in ContentPipeline, beside the other kinds' (3.3).

### 3.2 A scene is a document like any other

Opening, saving, reloading, missing and changed on disk, Keep Mine, save a copy
and following a relocation are `Document`'s behavior from S1.5. The scene's
views listen to the document: when a reload replaces the content, they show the
new scene and the engine sync registers it.

The scene's content tells the document when an edit is in progress, such as a
gesture. A file that changes on disk during a drag then marks the scene changed
on disk instead of reloading it under the drag. Hiding a node in the editor is a
history entry that keeps the history's position, so it does not make the scene
dirty.

What goes from Projects and the World Editor:

- Projects' scene members, `SceneLoadSnapshot`, `SceneReloadSnapshot`,
  `SceneSourceVersion` and the scene versions it keeps;
- the save gates, the metadata change counter for scenes, and the
  `UndoRedo.GetHistory(sceneId)` calls (the document gives the history).

The scene's id stays inside the file. It keys what is about the scene rather
than the file: the engine's scene, the explorer's layout, viewport state, and
the project's start scene.

### 3.3 Scene assets are assets

The relocation service renames, moves, copies and deletes scenes as it does
every other asset:

- with one entry in the global history;
- reporting the files it changed, so the asset feed follows without a refresh;
- rewriting the scenes that reference a moved scene, as it rewrites any
  referrer;
- with open documents following their files.

Two rules are the scene's own:

- **A copy gets its own identity:** a new scene id, and the copy's name.
- **The project's start scene cannot be deleted,** alone or with its folder.

ContentPipeline applies them, in the reference format that already reads and
rewrites scene files for a relocation. New Scene lives in ContentPipeline's
authoring service, beside New Script and New Input File, because the Content
Browser, which offers it, does not depend on Scenes.

`SceneAssetCommandMessage`, `SceneAssetRenameOperation`, the scene branches of
`DocumentManager` and the Content Browser's refresh after a scene command go.

### 3.4 The scene list comes from the files

`IProject.Scenes`, `IProject.ActiveScene` and `ProjectContext.Scenes` go. The
scenes of a project are the scene files that the asset catalog lists. The places
that read the list change:

- the default name of a new scene asks the catalog for a free one;
- the cook reads each scene's id from its file and its name from the file name;
- the start scene is the scene file whose id is the project's start scene id, in
  any folder;
- the workspace remembers the last opened scene by its id;
- the open scene is the document that is open.

### 3.5 Transactions

Superseded by [scene-edits.md](scene-edits.md) section 3.5: TimeMachine
transactions cannot roll a scene back; atomic batches live in Scenes.

### 3.6 Scene edits

Every change to a scene is an edit executed by the scene's authoring object
(`ExecuteAsync`), which owns the gate, lock checks, transaction, undo and the
change stream. The edit model is specified in [scene-edits.md](scene-edits.md).
These rules are mandatory for every scene edit:

1. Edits are serializable records of ids and values only. No `SceneNode`,
   `SceneNodeData` or other live object goes into an edit.
2. Ids are typed: `DocumentId`, `SceneId`, `NodeId`.
3. Edits are explicit: targets and data are carried in the edit. Delete and
   paste take ids and data; the UI passes its selection and clipboard in.
4. `ExecuteAsync` is callable from any thread; the authoring object marshals
   onto the scene's thread.

## 4. Decisions

1. **A scene's name is its file name, everywhere.** The decode takes the name
   from the file, the save writes it into the file, and the cook takes it from
   the file name, as it already does for the cooked path. Renaming or moving a
   scene needs no scene-specific rewrite.
2. **Scenes live in any folder of an authoring mount,** as materials and
   textures do. "Directly in `Content/Scenes`" goes, with the relocation
   planner's refusals and the name addressing. New Scene creates the scene in
   the folder the Content Browser shows.
3. **Renaming, moving, duplicating and deleting a scene go through the
   relocation service (3.3):** one path for every asset, with global undo, file
   reports and followers.
4. **The project's scene list goes (3.4),** and the open scene is the open
   document.
5. **Scenes become documents in this slice (3.2).** The World Editor opens
   scenes through `OpenDocuments`, and the scene editor saves, reloads and
   follows through the scene's document. `EditorDocumentService` still draws the
   tabs, with `SceneDocumentMetadata` reading the document's title and state,
   until the Shell's document service replaces both in S1.10; the Shell then
   moves the placement rules without rewriting the scene's behavior.
6. **Scenes reports through events, not messages.** Scene changes come from
   `SceneAuthoring.Changed` and selection changes from the selection service's
   event; the command service's five messages go. The views' own messages
   (selection requests, scene loaded) stay until the Shell's S1.10.
7. **The engine sync port speaks Scenes' language.** `ObserveEnvironmentAsync`
   returns a Scenes type; the synchronized event names the scene and its
   document's id; `RegisterDocument` and `CloseDocument` take the document's id,
   and engine sync keeps the revision that orders the scene's projections. The
   implementation maps them and stays in the World Editor until S1.7.
8. **Only node lock and visibility go to Scenes.** Preview, viewport and
   transform tool settings are the scene editor's view preferences and move with
   it in S1.12.
9. **`ScriptSlotEdits` moves in S1.8,** when World gains `Script` with its
   declared parameters.
10. **The content-demand service loses its `WindowId` in S1.7,** where it moves
    to LivePreview.
11. **All ten change sets become atomic scene edit batches** (scene-edits.md
    3.5), including the atmosphere lights reset and the camera move in the
    views.
12. **Scenes' behavior is tested in `Oxygen.Editor.Scenes.Tests`,** through its
    public API and without the WinUI host
    ([scene-edits-tests.md](scene-edits-tests.md)). The World Editor's tests
    keep the views and the engine projection, over a real `SceneAuthoring`.
13. **Scenes sits downstream of ContentPipeline.** ContentPipeline does not
    reference Scenes. Scenes uses its rules, not a port of them: the cook's
    post-process validation and canonical virtual paths (one rule for authoring
    and cooking), the relocation redirects contract, and the cooked geometry's
    material slot inventory.
14. **A scene document kind belongs to one project.** The workspace creates it
    with its project; activating another project closes every window first.
15. **The scene list is the asset catalog's scene files (3.4).** A scene's id is
    read from its file, as the cook reads it.

## 5. Gate

- `Oxygen.Editor.Scenes` tests without the WinUI host.
- The World Editor, Content Browser, ContentPipeline, Projects and Documents
  tests that cover the callers.
- Your editor build, and your acceptance in the running editor.

## 6. Problem register

The code moved into `Oxygen.Editor.Scenes` does not yet follow this design. Each
problem below is open until it is resolved and closed in this register, one at a
time. A problem that is postponed becomes a GitHub issue and is linked here.

### 6.1 Design problems in the moved code

- **P1. The module has no identity.** Status: closed. Every type in the module
  is in an `Oxygen.Editor.Scenes.*` namespace that follows its folder
  (scene-edits.md section 6). Was: The namespaces are still
  `Oxygen.Editor.WorldEditor.Documents.Commands` (51 files),
  `Oxygen.Editor.World.Services` (18),
  `Oxygen.Editor.World.SceneExplorer.Operations`,
  `Oxygen.Editor.World.Documents` and `Oxygen.Editor.World.Diagnostics`; only
  `Interaction` is in `Oxygen.Editor.Scenes`.
- **P2. The command service is a god service.** Status: closed. The service and
  its interface are deleted; each change is an edit record run by
  `SceneAuthoring`, which owns the frame, the history and the change stream
  (scene-edits.md). Was: `SceneDocumentCommandService` is 11.8k lines in 18
  partials behind one interface of about 40 methods: hierarchy, properties,
  gestures, clipboard, scripting, materials and relocation in one type. Partials
  are file organization, not design boundaries. Found in the analysis of P2:
  - every command repeats the same frame by hand (gate, lock check, validate,
    apply, a hand-written undo closure pair `Record*History` and
    `Apply*ForHistoryAsync`, a hand-written engine call, publish, result);
  - property edits go through two pipelines: the schema pipeline
    (`PropertyEdit`, `PropertyOp`, descriptors) and typed edits
    (`TransformEdit`, `PerspectiveCameraEdit`, `DirectionalLightEdit`,
    `GeometryEdit`, `SceneEnvironmentEdit`). The adapters convert one into the
    other (`EditComponentPropertiesThroughExistingCommandAsync`);
  - nothing outside the module calls six public methods, which only tests and
    the adapters reach: `EditTransformAsync` is used, but `EditGeometryAsync`,
    `EditPerspectiveCameraAsync`, `EditDirectionalLightAsync`,
    `EditSceneEnvironmentAsync`, `DeleteNodesAsync` and `DeleteFolderAsync` are
    not;
  - engine property entries are built by hand per component (about 15
    `Build*PropertyEntries` overloads) next to the descriptors that already
    describe the same fields;
  - commands call the engine port directly, and so does every undo and redo
    closure;
  - the only state in the service is the open gestures, keyed by `Scene` (P6).
- **P3. The domain posts notifications.** Status: closed. Scenes reports through
  `SceneEditResult` and coded diagnostics; the World Editor publishes them. Was:
  The command service takes `IOperationResultPublisher` and publishes failures
  (`PublishSceneFailure`, about 22 call sites), against the rule that domain
  services report through results and events and the Shell decides what to show.
- **P4. Undecided dependency on ContentPipeline.** Status: closed. Decision 13.
  Was: The commands use `ContentPipeline.Inspection` (material slots),
  `ContentPipeline.Relocation.IAssetRedirects` and ContentPipeline references,
  and section 3.1 did not list ContentPipeline.
- **P5. The authoring gate is static global state.** Status: closed.
  `SceneAuthoringGate` is deleted; each document's `SceneAuthoring` serializes
  its own edits. Was: `SceneAuthoringGate` keeps a
  `ConditionalWeakTable<Scene, State>` and an `AsyncLocal`, is called statically
  by commands, content and view models, and was made public by the move.
  Exclusive access to a document's scene should be owned per document and passed
  explicitly.
- **P6. Edit-session state is owned by the service, not the content.** Status:
  closed. Gestures belong to the document's `SceneAuthoring`, which
  `SceneContent` holds. Was: `SceneContent` holds the command service only to
  answer `HasEditsInProgress` and `FinishEditsAsync`, because edit sessions live
  in the service keyed by `Scene`.
- **P7. The engine sync port is shaped by its implementation.** Status: fixed,
  awaiting the editor check. The port left Scenes: LivePreview's `IScenePreview`
  follows the open scene documents and keys everything a view reads by
  `DocumentId` (readiness, pending fields, sky light), per
  [live-preview.md](live-preview.md) decisions 1 to 3. Was: `ISceneEngineSync`
  has about 50 members, two API generations (scene-scoped calls returning
  `SyncOutcome` beside fire-and-forget node calls), mixed keys (document id to
  register; scene id for pending counts and preview sync; `Scene` objects
  elsewhere) and exposed revisions (`CaptureRevision`, `AdvanceRevision`).
- **P8. The identity rule is not in the types.** Status: closed. Task 1 of
  scene-edits.md typed node, component, scene and folder ids; task 2 typed
  document ids. The types exposed three places that passed a scene id as another
  id, now fixed: the environment's property edits were sent through the
  node-keyed edit API (the command service and the Inspector's edit
  coordinator), the explorer logged the scene id as its document id, and the
  editing view mask reported the scene id as a document id. Was: Document id,
  scene id and node id are all `Guid`, so a scene id can be passed where a
  document id is meant; this caused the selection regression. They need distinct
  strong types and a written rule for which key each concern uses.
- **P9. Decoding reads ambient project state.** Status: closed.
  `SceneDocumentKind` takes its project when it is created (decision 14). Was:
  `SceneDocumentKind.DecodeAsync` deserialized with
  `IProjectManagerService.CurrentProject`.
- **P10. The scene list reads the disk, not the catalog.** Status: closed.
  `SceneFiles` lists the scene files the asset catalog holds (decision 15). Was:
  `SceneFiles.List` enumerated directories.
- **P11. Visibility widened by the move.** Status: closed.
  `SceneReferenceRetargeting` and `SceneTransformMath` are internal; the
  reference queries the engine projection needs are `SceneAssetReferences`. The
  descriptors and field rules stay public: they are the Inspector's property
  contract. `Scene.SetEnvironment`, `SetPlay` and `SetReferences` stay public:
  they are World's model API, used by ContentPipeline and its tests.
  `SceneAuthoringGate` is deleted (P5). Was: these types and members were made
  public so the move would compile.
- **P12. Multi-node actions cannot roll back.** Status: closed. Multi-node
  actions are atomic batches that roll back on refusal (scene-edits.md 3.5);
  Scenes has no `BeginChangeSet` left. Was: Eight `BeginChangeSet` call sites
  remain (decision 11).
- **P13. Unused package reference.** Status: closed. The project no longer
  references `CommunityToolkit.Mvvm`.

- **P14. TimeMachine transactions cannot roll back.** Status: closed. The
  transactions API is deleted; it had no user, and change sets group changes.
  Was: `HistoryKeeper.AddChange` never routed into an open transaction, and
  `RollbackTransaction` popped transactions without applying anything.

- **P15. Schemas keys property targets by Guid.** Status: closed.
  `PropertySnapshot`, `PropertyOp` and `IPropertyTarget` are deleted, and
  `PropertyBinding` folds the targets it is given without keying them. Was:
  these types keyed a target that is a node or, for the environment, the scene.
- **P16. `NodeHandle` is unused.** Status: closed, not a problem. The interop's
  node creation callbacks use `Oxygen.Managed.Core.NodeHandle`.

- **P17. Managed.Core diagnostics carry a document id.** Status: closed.
  `AffectedScope` has no document fields; nothing read them. A failure names its
  scene, node and asset. Was: `AffectedScope.DocumentId` (`Guid?`), with the
  document's path, name, lifetime and authoring revision.

### 6.2 Defects and unfinished work in the working tree

- **D1. Material save and cook repeat as if dirty.** Status: closed; checked in
  the editor.
- **D2. Integration test hangs.** Status: closed. The automatic cook waits for
  the cook writer to be idle before it reads statuses and queues a run.
  `ClosingProjectCancelsCookAndReopensPriorNativePublication` and
  `CancelledWorkspaceCookRetainsMaterialUntilRetry` held the writer and waited
  for that run, so they waited on themselves. Both now hold the automatic cook
  at preview capture instead; a change still waiting for the writer is checked
  to leave no run when the project closes. The World Editor integration suite
  passes.
- **D3. Fixes not checked in the editor.** Status: closed; checked in the
  editor. Viewport camera state keyed by scene id; the material tab told when it
  becomes dirty or is saved; selection keyed by document id (explorer,
  Inspector, `SceneLoadedMessage.DocumentId`); the last opened scene stored by
  id; the reload warning about the tree.
- **D4. Test projects do not build.** Status: closed. The World Editor's Unit,
  Unit.UI, Integration.UI and Benchmarks.UI projects build, and their suites
  pass on `SceneAuthoring`. `Oxygen.Editor.Scenes.Tests` holds the unit and
  functional tests of the module.
- **D5. This document is stale.** Status: closed. This document,
  `document-kinds.md` and the #69 plan describe the code as it is. Was: Decision
  13 and section 5 described the old commit order, not the split.
