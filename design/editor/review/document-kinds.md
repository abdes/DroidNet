# Document kinds

Status: **implemented**.

Scope: slice S1.5 of
[issue-069-di-topology-and-decomposition.md](../plan/issue-069-di-topology-and-decomposition.md).
It covers what every open document has in common, what a document kind adds, and
how a request to open an asset reaches the right kind. It works within
[editor-documents.md](editor-documents.md) (the "documents design") and
[editor-shell.md](editor-shell.md). There is no window or tab in it.

## 1. Scenarios

The actors:

- the user;
- an open document and the editor that shows it;
- the document service, which opens, saves, reloads and closes documents;
- tools outside the editor (a code editor, a paint tool, source control);
- the relocation workflow, which renames, moves and deletes assets;
- Play and Cook, which ask which of their inputs have unsaved edits.

Document kinds: scenes and materials. Coming next: textures, input mapping
contexts and input actions. Hair, water and physics are future families that
will need kinds of their own.

1. **Open an asset.** The user double-clicks a material in the Content Browser,
   or opens a scene from the Scene Explorer, from a new-material command or by
   reopening the previous scene. If that file is already open anywhere, its
   document comes forward. Otherwise the kind that edits the file opens it.
   Every request names the asset, never the kind.
2. **Edit, then undo back.** The user changes roughness twice and undoes both
   changes. The document is back to what is on disk, so it is not dirty.
3. **Save.** The user presses Ctrl+S, chooses Save All, or confirms a save
   before Play, Cook or Close. Any edit in progress is committed first. The file
   is written only if it is still the version the document read or last saved.
   Otherwise the save stops on a conflict and offers Reload, Keep Mine or Save a
   Copy.
4. **A change outside the editor.**
   - A clean document reloads quietly.
   - A dirty document shows the conflict banner.
   - A document whose file was deleted or moved outside the editor stays open,
     marked missing, until it is saved again or closed.
   - The editor's own save is not reported back to the document as an outside
     change.
5. **Rename or move in the editor.** The user renames a material that the open
   scene uses, or renames the open material itself. The relocation rewrites the
   files, and every open document follows: the renamed one takes its new file
   and title, and the scene takes the new reference. Both keep their undo
   history, and neither becomes dirty. One Undo in the global history reverses
   the whole rename, and the documents follow again.
6. **Delete in the editor.** The user deletes a material that is open in a tab.
7. **Save a copy.** Choosing Save a Copy writes the document's content as a new
   asset under a name the user picks. The original stays open and keeps its
   unsaved edits.
8. **Close.** The user closes a document, a window, the project or the editor,
   is asked about unsaved edits, and the document releases what it holds.
9. **Inspect cooked output.** The user opens the cooked package inspection for a
   folder. It shows in a tab, but it edits no file.

## 2. What happened before this design

The facts below were measured before S1.5.

| Concern                  | Scene                                                                                                         | Material                                                                                                                        | Cooked inspection                     |
| ------------------------ | ------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------- |
| Open request             | `OpenSceneRequestMessage` and direct `DocumentManager` calls                                                  | `OpenMaterialRequestMessage`                                                                                                    | `OpenCookedInspectionRequestMessage`  |
| Opened once by           | Scene id, and at most one scene per window                                                                    | URI string, ignoring case                                                                                                       | Project, scope, asset and cooked root |
| Identity                 | Tab id is the scene id                                                                                        | Tab id and the material service's id differ                                                                                     | Tab id                                |
| Who reads the file       | `ProjectManagerService` (stage, then accept)                                                                  | `MaterialDocumentService` through `IAtomicFileStore`                                                                            | The content pipeline's inspection     |
| Version it saves against | `ProjectManagerService`, kept by path and by root and scene id                                                | `MaterialDocumentService`, kept by its own document id                                                                          | None                                  |
| Writes queued by         | Project root                                                                                                  | File path                                                                                                                       | None                                  |
| Dirty                    | Change counter on the metadata (`MarkSaved`)                                                                  | A copied flag: each `true` increments the counter                                                                               | Never                                 |
| Undo back to saved       | Still dirty                                                                                                   | Clean (compares content)                                                                                                        | None                                  |
| History                  | `UndoRedo.GetHistory(sceneId)`, from four places                                                              | Private `HistoryKeeper` in the service                                                                                          | None                                  |
| Outside change           | `IAssetCatalog.Changes`, for missing only                                                                     | `IAssetFeed.Items`, for missing only                                                                                            | None                                  |
| Changed on disk          | Seen only when a save conflicts                                                                               | Seen only when a save conflicts                                                                                                 | None                                  |
| Follows a relocation     | Relocation participant: references rewritten in memory, no history entry; Projects adopts the written version | Relocation participant: path, URI and references; the service adopts the written version; `DocumentManager` refreshes the title | None                                  |
| Deleted in the editor    | Refused while open                                                                                            | Tab force-closed                                                                                                                | None                                  |
| Edit in progress         | The focused number box (`IDocumentInputCommitter`, by window), then the command service's edit sessions       | The focused number box, then its own number box sessions                                                                        | None                                  |

What makes this stiff:

1. **Every concern exists twice, and differently.** A third kind (texture) would
   copy one of the two shapes and add a third set of differences.
2. **The version a save checks against lives in two services.** One is Projects,
   which decision 7 of the documents design wants generic. The other is a
   feature service.
3. **Dirty means two different things.** A scene stays dirty after its edits are
   undone; a material becomes clean.
4. **Changes outside the editor are not handled as the documents design says.**
   No clean document reloads quietly, scenes and materials listen to different
   sources, and a dirty document learns of an outside change only when its save
   conflicts.
5. **"Opened once" has three rules.** None of them is the file.
6. **The contracts in `Oxygen.Editor.Documents` know windows.** These are
   `IDocumentInputCommitter`, `IDocumentConflictPrompt` and
   `IDocumentClosePrompt`, plus `IDocumentMetadata`, the tab contract, which
   `BaseDocumentMetadata` implements. They are tab-level, and decision 2 of the
   documents design puts them in the Shell.
7. **The mapping between an asset URI and its file has several copies, each with
   its own checks:**
   - `ProjectMaterialSourcePathResolver`, which rejects paths that leave their
     mount;
   - `AssetFeed` and `AssetIdentityReducer`, which do not check;
   - `CookInputResolver.FindAuthoringSourceUri` and `IsAuthoringUri`;
   - `RelocationPaths.ToPhysical`;
   - the scene paths inside `ProjectManagerService`.
8. **`Oxygen.Editor.Runtime` references `Oxygen.Editor.Documents` but uses
   nothing from it.**

## 3. Proposal

### 3.1 What every document has

Every document has the same state. Its behavior is the same for every kind:

- **Identity:** the document's id, which is also its tab's id. It has no second
  id.
- **The file it edits:** its path and asset URI, which change when it follows a
  relocation.
- **Its kind.**
- **The version on disk it last read or saved.** Every save is checked against
  it.
- **Dirty:** the history's current position differs from the position at the
  last save or load. Undoing back to the saved position makes the document clean
  again. A document whose file is missing is dirty.
- **Missing on disk**, and **changed on disk**: its file now differs from that
  version, and the document has edits.
- **Its history,** given to it when it opens. It never creates, finds or clears
  one (decision 1 of the documents design).
- **Its content:** the kind's model, such as World's `Scene` or, later, its
  `Material`.

The same behavior, over that state:

- **Open:** read the file and its version, then ask the kind to decode it. A
  file that does not decode fails the open with the kind's message, as a scene
  does today.
- **Save:** commit edits in progress, have the kind capture what to write, then
  write against the version the document holds. A conflict changes nothing and
  marks the document changed on disk. A success records the new version and the
  history position the capture saw, so an edit made while the file was being
  written stays dirty.
- **Save a copy:** the same capture, made for the new asset's identity, written
  only where no file exists. The original is unchanged.
- **Hearing that its file changed:**
  - If the file still has the version the document holds, the change was the
    document's own save, and nothing happens.
  - A clean document reloads.
  - A dirty document, or one with an edit in progress such as a drag, is marked
    changed on disk.
  - A document whose file is gone is marked missing.
- **Reload:** decode the file again and replace the content. Clear the history,
  because its entries refer to the content that was replaced.
- **Follow a relocation:**
  - Take the new path, URI and title.
  - Adopt the versions the relocation wrote.
  - Have the kind apply the rename to the content.

  No history entry is recorded and the dirty state does not change, because the
  relocation only rewrites files of documents that are saved (scenario 2.9 of
  the documents design).

### 3.2 What a kind adds

A kind does only the work that needs to know its format or its model:

- **The file suffixes it opens.**
- **Decode:** turn the file's bytes into its model, or explain why it cannot.
  Decoding receives the document's id and history, which the document service
  creates first, so content that records edits is complete when it is decoded
  (section 3.13 of [scene-edits.md](scene-edits.md)).
- **Capture and encode:** take what to save, then turn it into bytes. Capturing
  is quick and runs on the thread that edits the model. Encoding can run
  anywhere. A capture may refuse with a message, as a material's validation does
  today. A capture for a copy takes the new asset's identity, because a scene
  file holds its own id.
- **Tell whether an edit is in progress,** and **finish its edits in progress,**
  such as an open drag, as one history entry each.
- **Apply a relocation to its model:** its new file, and a mapping that gives
  each moved reference its new value. A mapping, not a list of URIs, because a
  moved folder moves every reference inside it.

A kind holds no file version and no history. It never reads or writes a file
itself.

### 3.3 How an open request reaches a kind

A request names an asset URI. Projects resolves the URI to a file under the
project's authoring roots. The document service then does one of two things:

- If a document already edits that file, it brings that document forward.
- Otherwise it picks the kind registered for the file's suffix and opens a new
  document with it.

The Shell then places the document and creates its editor from the editor
contribution for that kind (shell design section 3). This placement is S1.10.

A kind is registered by the module that owns its model:

- Scenes, for the scene kind (S1.6);
- the Material Editor, for the material kind (S1.8);
- each later editor, for its own kind.

So the kind's contract is in `Oxygen.Editor.Documents`, where a domain module or
an editor can implement it without referencing the Shell.

### 3.4 Projects resolves assets to files

Projects gains the one mapping, in both directions, between an asset URI and a
file or folder under one of the project's mounts: its authoring mounts and its
local folder mounts. A resolved location says which kind of mount holds it, so a
caller that only works on authoring mounts keeps that rule. The mapping writes
URIs in the asset catalog's form, so the URI of a file is the same wherever it
is computed. It rejects:

- an unknown mount;
- a path that leaves its mount;
- `.` and `..` segments.

Every copy listed in stiffness 7 uses it, and the cook-specific derivations
(native virtual paths, descriptor suffixes, writable mounts) are built on it.

### 3.5 The history's position

A history's position names the state its document is in. A new entry moves it to
a new position; undoing an entry returns it to the position before the entry,
and redoing the entry returns it to the position after. Undo and redo record a
new inverse change each time, so a position cannot be the identity of the entry
on top of the stack: TimeMachine's `HistoryKeeper` keeps the positions with its
stacks and exposes the current one. Clearing a history moves it to a new
position. An entry that changes only how the document is shown, such as hiding a
node in the scene editor, keeps the position, so it does not make the document
dirty.

### 3.6 Keep Mine

The conflict banner's **Keep Mine** keeps the document's edits and takes the
file's current version as the one its next save writes against, so that save
replaces the outside change. The document stays dirty, and it is no longer
marked changed on disk.

## 4. Decisions

1. **The common behavior of 3.1 lives in `Oxygen.Editor.Documents`, without
   windows.** The Shell's document service (S1.10) uses it and adds the parts
   that need the UI: placement, tabs, prompts, the banner, commands and routing
   between histories. The Shell decides _when_ and _where_; Documents does
   _how_. `IUnsavedDocuments` (S1.11) is answered from Documents.
2. **Dirty follows the history's position (3.5).** Undoing back to the saved
   point makes a document clean. Every change to content that is saved is
   recorded in the history.
3. **The kind's contract has no "apply or revert a history change".** A history
   change carries its own inverse. The global history's multi-document entries
   reach documents through follow (3.1): undoing a rename runs the reverse
   relocation, and the documents follow it.
4. **Committing an edit in progress has two steps.** The Shell commits the
   focused control in the document's window; `IDocumentInputCommitter` is a
   window concern and moves to the Shell in S1.10. The document then asks its
   kind to finish its edits in progress (3.2). Play, Cook and Close run both
   steps before they save.
5. **Cooked inspection is not a document.** It is a view that the Shell places
   in the document area, opened once for each scope it inspects. Documents
   covers only file-backed documents.
6. **Deleting, in the editor, an asset that is open closes its document as part
   of the delete.** The delete confirmation names the open documents ("Wood is
   open and will close"), and a dirty one is discarded. A delete moves the files
   to the Recycle Bin and has no undo entry. A delete outside the editor keeps
   the document open and marks it missing (scenario 2.3 of the documents
   design).
7. **A document edits exactly one file.** A future kind that needs several files
   is a change to this design. Every kind known today edits one file: a texture
   its descriptor, an input mapping context its JSON file; scene sidecars are
   files of their own.
8. **A document is opened once per file.** The key is the file's full path,
   compared ignoring case. A relocation changes the key. There is no separate
   rule per kind.
9. **One scene per window is a placement rule of the scene editor (S1.10),** not
   part of a kind.
10. **Every copy in stiffness 7 uses the mapping of 3.4.**
11. **S1.5 removes `Oxygen.Editor.Runtime`'s unused reference to
    `Oxygen.Editor.Documents`.**

## 5. What S1.5 delivers

- In TimeMachine: the history's position (3.5).
- In `Oxygen.Editor.Documents`: the document's state, the kind's contract and
  the behavior of 3.1 over `IAtomicFileStore`, with tests over a fake kind:
  - open once per file, and the kind chosen by suffix;
  - undo back to the saved point;
  - an edit made while saving;
  - a conflict, and Keep Mine;
  - the document's own save heard back;
  - quiet reload, changed on disk, missing and restored;
  - follow without dirtiness or a history entry;
  - a copy for a new identity;
  - a failed decode or capture.
- In Projects: the asset-to-file mapping, with the copies of decision 10
  replaced, and their modules' tests.
- The unused reference of decision 11 removed.

Scenes implements the scene kind (S1.6) and the Material Editor the material
kind. `EditorDocumentService`, `BaseDocumentMetadata`, the close coordinator and
the prompts stay until the Shell's document service replaces them in S1.10 (plan
decision 9).

No adapter is written that a later slice has to remove.

The gate is the TimeMachine, Documents and Projects tests, plus the tests of the
modules whose copies decision 10 replaces.
