# Scenes Module LLD

Status: `agreed`

Last Updated: 2026-10-22

## 1. Purpose

Define `Oxygen.Editor.Scenes`, the domain module for scene authoring, and the
scene document kind. A scene is a document like any other, a scene asset is an
asset like any other, and every change to a scene is an edit run by the scene
document's `SceneAuthoring`. The module works within
[editor-module-architecture.md](editor-module-architecture.md#72-scenes) and
[editor-documents.md](editor-documents.md); the edit model is in
[scene-edits.md](scene-edits.md).

## 2. PRD Traceability

| ID         | Coverage                                                                                                   |
| ---------- | ---------------------------------------------------------------------------------------------------------- |
| `GOAL-002` | Static scene authoring: the scene document kind and its edits.                                             |
| `REQ-004`  | Node create, delete, rename and reparent are scene edits (5.6).                                            |
| `REQ-005`  | Component add, remove and edit are scene edits (5.6).                                                      |
| `REQ-006`  | Every change is an edit recorded into the scene document's history and marks it dirty (5.3, 5.6).          |
| `REQ-007`  | The scene document kind decodes and captures the scene for save and reopen (5.2).                          |
| `REQ-038`  | Saving, reloading and external-change conflicts are the common document behavior (5.3).                    |
| `REQ-021`  | Scene assets are addressed by their files and relocated like every asset, with references rewritten (5.4). |

## 3. Architecture Links

- [editor-module-architecture.md](editor-module-architecture.md#72-scenes): the
  Scenes module in the domain layer.
- [editor-documents.md](editor-documents.md): one history per document; saving
  and cooking.
- [document-kinds.md](document-kinds.md): the common document behavior and the
  document kinds, including the scene's.
- [scene-edits.md](scene-edits.md): `SceneAuthoring`, the edits, batches,
  gestures, the change stream and results.
- [live-preview.md](live-preview.md): the preview of open scene documents.
- [asset-family-modules.md](asset-family-modules.md): `World` and the family
  models a scene references.

## 4. Current Baseline

`Oxygen.Editor.Scenes` holds what this design gives it, and the scene editor,
Scene Explorer and Inspector reach it through the ports of decision 6.

## 5. Target Design

### 5.1 Scenarios

Actors:

- the user;
- the scene's views: the scene editor (viewports and gizmos), the Scene Explorer
  and the Inspector, which all edit one scene;
- the Content Explorer, which creates, renames, moves, duplicates and deletes
  assets;
- the engine, which shows the scene through LivePreview;
- the cook, and tools outside the editor.

1. **Open a scene.** The user double-clicks a scene in the Content Explorer, or
   the editor opens the project's start scene. The scene opens as a document,
   and the engine shows it. Opening it again brings it forward.
2. **Edit the scene.** The user adds a light, renames a node and sets its
   intensity. Each is one undo entry, and the engine follows each one.
3. **A multi-node action.** The user deletes, moves, groups, duplicates or
   pastes twelve nodes. Each is one undo entry with one label. If it fails part
   way, nothing of it is left: no partial undo entry, and no half-changed scene.
4. **A gesture.** The user drags a light's intensity. The engine previews every
   step, and the release records one entry. Saving, playing or cooking during
   the drag commits it first
   ([editor-documents.md](editor-documents.md#527-an-edit-in-progress)).
5. **Selection.** Selecting nodes in any of the three views selects them in the
   others, and the Inspector shows them.
6. **Save, undo back to clean, and changes outside the editor.** These follow
   the common document behavior. A scene that is clean reloads quietly when its
   file changes, and the views and the engine show the reloaded scene.
7. **Rename or move a scene asset.** The user renames `Level1` to `Intro` in the
   Content Explorer or at the root of the Scene Explorer, whether the scene is
   open or not. Its file is renamed, the scenes that reference it are rewritten,
   an open scene follows its file and takes the new title, and one Undo in the
   global history reverses the whole rename.
8. **Duplicate or delete a scene asset.** Duplicate writes a new scene with its
   own identity and the copy's name. Deleting an open scene closes it
   ([document-kinds.md](document-kinds.md)); the files go to the Recycle Bin.
   The project's start scene cannot be deleted, alone or with the folder that
   holds it.
9. **Create a scene.** New Scene writes an empty scene with a free name and
   opens it.
10. **Rename a material the scene uses.** The scene follows, as any document
    does.

### 5.2 The module

`Oxygen.Editor.Scenes` is a class library with no WinUI. It references World,
Schemas, Data, `Oxygen.Editor.Documents`, Projects, Scripting, ContentPipeline,
`Oxygen.Managed.Core`, `Oxygen.Managed.Assets`, TimeMachine, Storage and
Composition (decision 13). It holds:

- **The scene document kind** (`.oscene.json`, `SceneDocumentKind`): decode,
  capture, capture for a copy, finish edits, apply a relocation. Its content
  (`SceneContent`) holds the `Scene` and the document's `SceneAuthoring`. The
  decode takes the scene's name from its file and checks that node ids are
  present and unique and that the explorer layout names each folder once and
  only nodes of the scene, each once.
- **Scene edits** (5.6): the edit records, `SceneAuthoring`, results, coded
  diagnostics, descriptors and component rules. `SceneAuthoring` records into
  the history of the scene's document and reports every change on one stream
  (`Changed`).
- **Selection** (`ISceneSelectionService`), keyed by `DocumentId`, with its own
  event.
- **Node lock and visibility,** behind the port `ISceneNodeInteraction`, because
  edits obey it, and its implementation, `SceneNodeInteraction`, which keeps
  and persists the state per project, with the Explorer's category filters
  (`SceneCategories`).
- **The explorer layout** data (`ExplorerEntryData`, `FolderId`) and the
  clipboard model.
- **The ports between the scene parts** (`ISceneViews`, `ISceneAvailability`),
  `SceneOperationResults` and the scene-node drag payload `SceneNodeDragData`
  (decision 6).
- **The scene files** (`SceneFiles`): the project's scenes as the asset catalog
  lists them (5.5).

Scenes knows no engine (decision 7). Creating a scene and the scene rules of the
relocation service are not in Scenes: they live in ContentPipeline, beside the
other kinds' (5.4).

### 5.3 A scene is a document like any other

Opening, saving, reloading, missing and changed on disk, Keep Mine, save a copy
and following a relocation are `Document`'s common behavior. The scene's views
listen to the document: when a reload replaces the content, they show the new
scene and LivePreview takes the new content.

The scene's content tells the document when an edit is in progress, such as a
gesture. A file that changes on disk during a drag then marks the scene changed
on disk instead of reloading it under the drag. Hiding a node in the editor is a
history entry that keeps the history's position, so it does not make the scene
dirty.

Projects holds no scene members, scene snapshots or scene versions; the document
holds the scene's versions and gives its history.

The scene's id stays inside the file. It keys what is about the scene rather
than the file: the engine's scene, the explorer's layout, viewport state, and
the project's start scene.

### 5.4 Scene assets are assets

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

ContentPipeline applies them, in the reference format that reads and rewrites
scene files for a relocation. New Scene lives in ContentPipeline's authoring
service, beside New Script and New Input File, because the Content Explorer,
which offers it, does not depend on Scenes. There is no scene-specific asset
command, rename operation or browser refresh.

### 5.5 The scene list comes from the files

The project keeps no scene list and no active scene. The scenes of a project are
the scene files that the asset catalog lists (`SceneFiles`). Where a list or a
scene is needed:

- the default name of a new scene asks the catalog for a free one;
- the cook reads each scene's id from its file and its name from the file name;
- the start scene is the scene file whose id is the project's start scene id, in
  any folder;
- the workspace remembers the last opened scene by its id;
- the open scene is the document that is open.

### 5.6 Scene edits

Every change to a scene is an edit executed by the scene's authoring object
(`ExecuteAsync`), which owns the thread, lock checks, atomicity, undo and the
change stream. Multi-step actions are atomic batches that roll back
([scene-edits.md](scene-edits.md#56-atomic-batches)); TimeMachine change sets
have no rollback and never group a scene action. The edit model is specified in
[scene-edits.md](scene-edits.md). These rules are mandatory for every scene
edit:

1. Edits are serializable records of ids and values only. No `SceneNode`,
   `SceneNodeData` or other live object goes into an edit.
2. Ids are typed: `DocumentId`, `SceneId`, `NodeId`.
3. Edits are explicit: targets and data are carried in the edit. Delete and
   paste take ids and data; the UI passes its selection and clipboard in.
4. `ExecuteAsync` is callable from any thread; the authoring object marshals
   onto the scene's thread.

### 5.7 Decisions

1. **A scene's name is its file name, everywhere.** The decode takes the name
   from the file, the save writes it into the file, and the cook takes it from
   the file name, as it does for the cooked path. Renaming or moving a scene
   needs no scene-specific rewrite.
2. **Scenes live in any folder of an authoring mount,** as materials and
   textures do, and are addressed by their files, not by name. New Scene creates
   the scene in the folder the Content Explorer shows.
3. **Renaming, moving, duplicating and deleting a scene go through the
   relocation service (5.4):** one path for every asset, with global undo, file
   reports and followers.
4. **The project has no scene list (5.5),** and the open scene is the open
   document.
5. **Scenes are documents (5.3).** A window opens scenes through its document
   area, as it opens every document, and the scene editor saves, reloads and
   follows through the scene's document. The Shell's document service places
   and draws the scene's tab from the document's title and state, as for every
   document
   ([editor-documents.md](editor-documents.md#51-decisions) decision 2).
6. **Scenes reports through events, not messages.** Scene changes come from
   `SceneAuthoring.Changed` and selection changes from the selection service's
   event. The views follow the documents and these events; they exchange no
   messages, and each request of one scene part to another is a call on a port
   of Scenes:
   - **Selection.** Every part selects and reads the selection through
     `ISceneSelectionService` (`SetSelection`, `GetSelectedNodes`) and follows
     it through `SelectionChanged`. The Scene Explorer reveals what another
     part selects.
   - **Loading.** The Inspector follows the documents of its window's document
     area: it shows the active scene document and clears one that closed.
   - **Shortcuts.** Scene shortcuts are commands with stable ids and key
     gestures, contributed by the feature that runs them
     (`IFeatureModule.Commands`, such as the scene editor's `Scene.FrameAll` and
     the Scene Explorer's `Scene.Rename`). `Edit.Delete`, Undo and Redo are the
     Shell's, the viewport's included.
   - **Views.** `ISceneViews` frames nodes, runs camera commands and reports
     the camera state of the active viewport of the editor that shows a scene
     document, keyed by `DocumentId`. The scene editor implements it
     (`SceneEditorViews`).
   - **Availability.** `ISceneAvailability` reports that the scene a window was
     asked to show did not open (`SceneUnavailable`) and reopens the one it
     showed before (`ReloadPreviousAsync`). The window's scene document
     lifetime, `SceneDocumentLifetime` in the scene editor, implements it.
   - **Node lock and visibility** are `ISceneNodeInteraction`, implemented in
     Scenes by `SceneNodeInteraction` (decision 8), which publishes its
     failures through Scenes' `SceneOperationResults`.
   - **Drag and drop.** `SceneNodeDragData` is Scenes' scene-node drag
     payload: the Scene Explorer writes the dragged nodes, and the Inspector's
     script slots read them.
7. **Scenes knows no engine.** The engine port and its value types are not in
   Scenes. LivePreview previews the open scene documents by following
   `OpenDocuments` and each document's `SceneAuthoring.Changed`, and publishes
   the surface the views read through `IScenePreview`, keyed by `DocumentId`
   (readiness, pending fields, sky light) ([live-preview.md](live-preview.md)).
8. **Of the editing state, only node lock and visibility go to Scenes,** as
   the port `ISceneNodeInteraction` and its implementation,
   `SceneNodeInteraction`, because the Scene Explorer, the scene editor and the
   Inspector all read and change it. Preview, viewport and transform tool
   settings are the scene editor's view preferences and live with the scene
   editor.
9. **The script slot edits (`ScriptSlotEdits`) are in Scenes,** built on
   Scripting's `Script` with its declared parameters; the slot rules are
   Scripting's.
10. **Content demand is LivePreview's** and follows the scene change stream; it
    takes no window.
11. **Every multi-node action is an atomic scene edit batch**
    ([scene-edits.md](scene-edits.md#56-atomic-batches)), including the
    atmosphere lights reset and the camera move in the views.
12. **Scenes' behavior is tested in `Oxygen.Editor.Scenes.Tests`,** through its
    public API and without the WinUI host. The tests of the scene editor, the
    Scene Explorer and the Inspector keep the views, over a real
    `SceneAuthoring`; LivePreview's tests keep the engine projection.
13. **Scenes sits downstream of ContentPipeline.** ContentPipeline does not
    reference Scenes. Scenes uses its rules, not a port of them: the cook's
    post-process validation and canonical virtual paths (one rule for authoring
    and cooking), the relocation redirects contract (`IAssetRedirects`), and the
    cooked geometry's material slot inventory.
14. **A scene document kind belongs to one project session.**
    `ScenesSessionModule` registers it in the session's scope, with the
    session's node-interaction state and asset redirects, and it ends with the
    session ([scope-topology.md](scope-topology.md)).
15. **The scene list is the asset catalog's scene files (5.5).** A scene's id is
    read from its file, as the cook reads it.

## 6. Ownership

| Part                                              | Owner                                          |
| ------------------------------------------------- | ---------------------------------------------- |
| Scene document kind, `SceneContent`, `SceneFiles` | Scenes                                         |
| `SceneAuthoring`, edits, results, change stream   | Scenes ([scene-edits.md](scene-edits.md))      |
| Selection, explorer layout data, clipboard model  | Scenes                                         |
| `ISceneNodeInteraction`, `SceneNodeInteraction`   | Scenes                                         |
| `ISceneViews`, `ISceneAvailability`               | Scenes (ports); the scene editor implements    |
| `SceneOperationResults`, `SceneNodeDragData`      | Scenes                                         |
| Script slot edits                                 | Scenes; slot rules in Scripting                |
| Scene relocation rules, New Scene                 | ContentPipeline                                |
| Engine preview of open scenes, content demand     | LivePreview                                    |
| Preview, viewport and transform tool settings     | the scene editor                               |
| Document lifetime, versions, history              | `Oxygen.Editor.Documents`, placed by the Shell |

## 7. Data Contracts

- The scene file (`.oscene.json`): the `Scene` with its id inside the file, and
  the explorer layout. The name is the file name (decision 1).
- `SceneContent`, `SceneAuthoring` and the edit contracts of
  [scene-edits.md](scene-edits.md).
- `ISceneSelectionService` and its event, keyed by `DocumentId`.
- `ISceneNodeInteraction` (5.2).
- `ISceneViews`, a session service keyed by `DocumentId`, and
  `ISceneAvailability`, a service of the workspace window (decision 6).
- `SceneNodeDragData.Format` (`application/vnd.oxygen.scene-nodes`): the
  dragged node ids, separated by semicolons.

## 8. Commands, Services, Or Adapters

- `SceneDocumentKind`, one per project session (decision 14).
- `SceneAuthoring`, one per open scene document.
- `ISceneSelectionService`.
- `SceneFiles`, over the asset catalog.

## 9. UI Surfaces

None in Scenes. The scene editor, Scene Explorer and Inspector are its callers
([scene-edits.md](scene-edits.md#517-callers)).

## 10. Persistence And Round Trip

The scene document kind decodes and captures the scene file; save, save a copy,
reload and relocation are the common document behavior (5.3). A copy saved or
duplicated takes a new scene id (5.4).

## 11. Live Sync / Cook / Runtime Behavior

LivePreview projects the open scene documents (decision 7). The cook reads saved
scene files only, reading each scene's id from its file and its name from the
file name (5.5).

## 12. Operation Results And Diagnostics

Scene edits report through `SceneEditResult` and coded diagnostics; they post
no notifications. The views publish operation results
([scene-edits.md](scene-edits.md#59-results)) through Scenes'
`SceneOperationResults`. The one service of Scenes that publishes results is
`SceneNodeInteraction`: a failure to load or save the lock and visibility state
is an operation result of the settings domain.

## 13. Dependency Rules

- Scenes references no WinUI, no Runtime and no LivePreview.
- ContentPipeline does not reference Scenes (decision 13).
- Projects holds nothing scene-specific.
- The Content Explorer does not depend on Scenes.

## 14. Validation Gates

- `Oxygen.Editor.Scenes.Tests`, without the WinUI host.
- The scene editor, Scene Explorer, Scene Inspector, Content Explorer,
  ContentPipeline, Projects, Documents and LivePreview tests that cover the
  callers.

## 15. Open Issues

None.
