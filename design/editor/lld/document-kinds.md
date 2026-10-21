# Document Kinds LLD

Status: `agreed`

Last Updated: 2026-10-20 01:32

## 1. Purpose

Define what every open document has in common, what a document kind adds, and
how a request to open an asset reaches the right kind. The design has no window
and no tab: a document's tab, its identity across windows and its editor are in
[document-identity.md](document-identity.md). Who owns a document and its
history, and how saving meets cooking, are in
[editor-documents.md](editor-documents.md).

One shape serves every kind. A new kind (a texture, an input mapping context)
writes only what needs its format or its model; opening once, saving against the
version read, dirty state, outside changes, relocation and Keep Mine are common
behavior, so they cannot differ between kinds.

## 2. PRD Traceability

| ID         | Coverage                                                                                                           |
| ---------- | ------------------------------------------------------------------------------------------------------------------ |
| `GOAL-004` | The material kind is the second kind of one shape that texture and input kinds join without new common code.       |
| `REQ-006`  | Every saved change is recorded in the document's history, and dirty follows the history's position.                |
| `REQ-010`  | A material asset opens as a document of the material kind.                                                         |
| `REQ-022`  | A file that does not decode, a refused capture and a save conflict reach the user as results, not silent losses.   |
| `REQ-038`  | A save writes only over the version the document read or last saved, and an edit made while it writes stays dirty. |

## 3. Architecture Links

- [editor-documents.md](editor-documents.md): one history per open document, the
  three document layers, and saving and cooking.
- [document-identity.md](document-identity.md): the document's id, its tab, the
  document area, editor descriptors and the material kind.
- [editor-shell.md](../review/editor-shell.md): the Shell's document service,
  placement, prompts and editor contributions.
- [asset-identity.md](asset-identity.md): `AssetPath` and the rules of a virtual
  path.
- [asset-family-modules.md](asset-family-modules.md): the family models each
  kind edits.
- [scene-edits.md](scene-edits.md): the scene kind's content, decoded with the
  document's identity and history.

## 4. Current Baseline

The work that brings the code to this design is tracked in the
[issue-069 plan](../plan/issue-069-di-topology-and-decomposition.md).

## 5. Target Design

### 5.1 Decisions

1. **The common behavior of 5.4 lives in `Oxygen.Editor.Documents`, without
   windows.** The Shell's document service uses it and adds the parts that need
   the UI: placement, tabs, prompts, the banner, commands and routing between
   histories. The Shell decides _when_ and _where_; Documents does _how_.
   `IUnsavedDocuments` is answered from Documents.
2. **Dirty follows the history's position (5.8).** Undoing back to the saved
   point makes a document clean. Every change to content that is saved is
   recorded in the history.
3. **The kind's contract has no "apply or revert a history change".** A history
   change carries its own inverse. The global history's multi-document entries
   reach documents through follow (5.4): undoing a rename runs the reverse
   relocation, and the documents follow it.
4. **Committing an edit in progress has two steps.** The Shell commits the
   focused control in the document's window (`IDocumentInputCommitter` is a
   window concern of the Shell). The document then asks its content to finish
   its edits in progress (5.5). Play, Cook and Close run both steps before they
   save.
5. **Cooked inspection is not a document.** It is the Cooked Inspector panel
   ([document-identity.md](document-identity.md), section 5.11). Documents
   covers only file-backed documents.
6. **Deleting, in the editor, an asset that is open closes its document as part
   of the delete.** The delete confirmation names the open documents ("Wood is
   open and will close"), and a dirty one is discarded. A delete moves the files
   to the Recycle Bin and has no undo entry. A delete outside the editor keeps
   the document open and marks it missing
   ([editor-documents.md](editor-documents.md#523-a-change-outside-the-editor)).
7. **A document edits exactly one file.** A kind that needs several files is a
   change to this design. Every known kind edits one file: a texture its
   descriptor, an input mapping context its JSON file; scene sidecars are files
   of their own.
8. **A document is opened once per file.** The key is the file's full path
   (`FileSystemPath`), compared as the file system compares names, ignoring case
   on Windows. A relocation changes the key. There is no separate rule per kind.
9. **One scene per window is a placement rule of the scene editor,** not part of
   a kind ([document-identity.md](document-identity.md), section 5.12).
10. **Every caller that maps between an asset and its file uses the one mapping
    of 5.7.** The cook-specific derivations (native virtual paths, descriptor
    suffixes, writable mounts) are built on it.

Why one shape: with a shape per kind, every concern (the version a save checks,
what dirty means, how outside changes arrive, what "opened once" keys on) exists
once per kind and differently, and each new family copies one of them with its
own differences. The common behavior is written and tested once, over a fake
kind.

### 5.2 Scenarios

The actors: the user; an open document and the editor that shows it; the Shell's
document service, which opens, saves, reloads and closes documents; tools
outside the editor (a code editor, a paint tool, source control); the relocation
workflow, which renames, moves and deletes assets; Play and Cook, which ask
which of their inputs have unsaved edits.

1. **Open an asset.** The user double-clicks a material in the Content Explorer,
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
6. **Delete in the editor.** The user deletes a material that is open in a tab
   (decision 6).
7. **Save a copy.** Choosing Save a Copy writes the document's content as a new
   asset under a name the user picks. The original stays open and keeps its
   unsaved edits.
8. **Close.** The user closes a document, a window, the project or the editor,
   is asked about unsaved edits, and the document releases what it holds.
9. **Inspect cooked output.** The user inspects a folder's cooked packages. The
   Cooked Inspector panel shows them; no document opens and no file is edited
   (decision 5).

### 5.3 Families with kinds

Scenes and materials have kinds. Textures, input mapping contexts and input
actions get kinds of their own when their editors arrive
([asset-family-modules.md](asset-family-modules.md)). Hair, water and physics
are future families that will need kinds of their own. Each kind's type lives in
the module that owns its model: `SceneDocumentKind` in `Oxygen.Editor.Scenes`,
`MaterialDocumentKind` in `Oxygen.Editor.Materials`.

### 5.4 What every document has

`Document` has the same state for every kind:

- **Identity:** `Id`, a `DocumentId`, which is also its tab's id. It has no
  second id.
- **The file it edits:** `File`, a `DocumentFile` with its path and its
  `AssetPath`, which change when it follows a relocation. `Title` is the file
  name without its kind's suffix.
- **Its kind:** `Kind`.
- **The version on disk it last read or saved:** `Version`, a `FileVersion`.
  Every save is checked against it.
- **Dirty:** `IsDirty`, true while the history's position differs from the
  position at the last save or load, or while the file is missing. Undoing back
  to the saved position makes the document clean again.
- **Missing on disk** (`IsMissingOnDisk`), and **changed on disk**
  (`IsChangedOnDisk`): its file changed outside the editor while the document
  had edits.
- **Its history:** `History`, given to it when it opens. It never creates, finds
  or clears one ([editor-documents.md](editor-documents.md#51-decisions),
  decision 1), except that a reload clears the entries that refer to the
  replaced content.
- **Its content:** `Content`, the kind's `IDocumentContent` over its model, such
  as World's `Scene` or Materials' `Material`.

`StateChanged` reports a change of dirty, missing, changed on disk or file;
`ContentReplaced` reports a reload.

The same behavior, over that state:

- **Open** (`OpenDocuments.OpenAsync`): read the file and its version, create
  the document's id and history, then ask the kind to decode. A file that does
  not decode fails the open with the kind's message.
- **Save** (`SaveAsync`): finish edits in progress, have the content capture
  what to write, then write against the version the document holds. A conflict
  changes nothing and marks the document changed on disk. A success records the
  new version and the history position the capture saw, so an edit made while
  the file was being written stays dirty. A capture that refuses fails the save
  with its reason.
- **Save a copy** (`SaveCopyAsync`): the same capture, made for the new asset's
  file and identity, written only where no file exists. The original is
  unchanged.
- **Hearing that its file changed** (`CheckFileAsync`):
  - If the file still has the version the document holds, the change was the
    document's own save, and nothing happens.
  - A clean document reloads.
  - A dirty document, or one with an edit in progress such as a drag, is marked
    changed on disk.
  - A document whose file is gone is marked missing.
- **Reload** (`ReloadAsync`): decode the file again and replace the content.
  Clear the history, because its entries refer to the content that was replaced.
- **Follow a relocation** (`FollowRelocationAsync`):
  - Take the new file, asset and title.
  - Adopt the version the relocation wrote.
  - Have the content apply the rename.

  No history entry is recorded and the dirty state does not change, because the
  relocation only rewrites files of documents that are saved
  ([editor-documents.md](editor-documents.md#529-every-operation-that-meets-unsaved-edits)).

### 5.5 What a kind adds

A kind does only the work that needs to know its format or its model. It is
split in two: the kind, which opens files, and the content it decodes, which the
document edits.

`IDocumentKind`:

- **`Name`**, as people read it ("Material").
- **`FileSuffixes`**, each starting with a dot. No two kinds open the same
  suffix; the longest suffix that ends a file's name chooses its kind.
- **`DecodeAsync`:** turn the file's bytes into the content, or say why it
  cannot. Decoding receives a `DocumentDecodeContext` with the document's id and
  history, which `OpenDocuments` creates first, so content that records edits is
  complete when it is decoded ([scene-edits.md](scene-edits.md)).

`IDocumentContent`:

- **`Capture`:** take what to save, as a `DocumentCapture` that encodes it into
  bytes. Capturing is quick and runs on the thread that edits the model;
  encoding can run anywhere. A capture may refuse with a reason
  (`DocumentCapture.Refused`), as a material's schema validation does.
- **`CaptureCopy`:** the same, for a copy's `DocumentFile`, because a file may
  hold its own identity (a scene file holds its id; a material is named from its
  file).
- **`HasEditsInProgress`** and **`FinishEditsAsync`:** whether an edit such as
  an open drag is in progress, and finish each as one history entry.
- **`ApplyRelocation`:** apply a `DocumentRelocation` to the model: the
  document's new file and version, and `MapAsset`, which gives each moved
  reference its new `AssetPath`. A mapping, not a list of assets, because a
  moved folder moves every reference inside it.

Neither holds a file version or a history of its own. Neither reads or writes a
file.

### 5.6 How an open request reaches a kind

A request names an asset by its `AssetPath`. The caller resolves it to a file
through Projects (5.7), keeping only files on an authoring mount, and asks
`OpenDocuments.OpenAsync` for that `DocumentFile`:

- If a document already edits that file, `OpenAsync` returns it.
- Otherwise it picks the kind for the file's suffix (`FindKind`) and opens a new
  document with it. A file no kind opens fails the open.

The Shell then places the document and creates its editor from the editor
contribution for that kind
([editor-shell.md](../review/editor-shell.md#3-contributions),
[document-identity.md](document-identity.md), section 5.8).

The kind's contract is in `Oxygen.Editor.Documents`, so a domain module
implements it without referencing the Shell. The workspace composition registers
each kind with `OpenDocuments`, beside its editor descriptor.

### 5.7 Projects resolves assets to files

`ProjectAssetFiles` in `Oxygen.Editor.Projects` is the one mapping, in both
directions, between an `AssetPath` and a file or folder under one of the
project's mounts: its authoring mounts and its local folder mounts. A resolved
`ProjectAssetLocation` says which kind of mount holds it (`MountKind`), so a
caller that only works on authoring mounts keeps that rule.

- `Find(AssetPath)`: the location of a virtual path.
- `FindSource(identity)`: the asset's existing source file, looked up through
  the project's file system (`ProjectContext.FileSystem`).
- `LocateSource(identity)`: its existing source, or where its most specific
  source would be, so a reader reports a missing file rather than an unknown
  asset; it reads through the project's file system too.
- `FindByFilePath(FileSystemPath)`: the virtual path of a file; when mounts
  nest, the innermost holds it.

It refuses an unknown mount, a path that leaves its mount, and any name the
virtual path rules refuse, `.` and `..` included
([asset-identity.md](asset-identity.md#57-names-and-virtual-paths)). An
`AssetPath` is in one form wherever it is computed, so the asset of a file is
the same value for the catalog, the cook and a document.

### 5.8 The history's position

A history's position (`HistoryKeeper.Position`, a `HistoryPosition`) names the
state its document is in. A new entry moves it to a new position; undoing an
entry returns it to the position before the entry, and redoing the entry returns
it to the position after. Undo and redo record a new inverse change each time,
so a position cannot be the identity of the entry on top of the stack:
TimeMachine's `HistoryKeeper` keeps the positions with its stacks and exposes
the current one. Clearing a history moves it to a new position. An entry that
changes only how the document is shown (a change whose `ChangesState` is false),
such as hiding a node in the scene editor, keeps the position, so it does not
make the document dirty.

### 5.9 Keep Mine

The conflict banner's **Keep Mine** (`KeepMineAsync`) keeps the document's edits
and takes the file's current version as the one its next save writes against, so
that save replaces the outside change. The document stays dirty, and it is no
longer marked changed on disk.

## 6. Ownership

| Owner                     | Owns                                                                                                         |
| ------------------------- | ------------------------------------------------------------------------------------------------------------ |
| `Oxygen.Editor.Documents` | `Document`, `OpenDocuments`, the kind and content contracts, the common behavior of 5.4, `IUnsavedDocuments` |
| `DroidNet.TimeMachine`    | `HistoryKeeper` and its `HistoryPosition`                                                                    |
| `Oxygen.Editor.Projects`  | `ProjectAssetFiles`: the one mapping between assets and files                                                |
| `Oxygen.Editor.Scenes`    | `SceneDocumentKind` and `SceneContent`                                                                       |
| `Oxygen.Editor.Materials` | `MaterialDocumentKind` and `MaterialContent`                                                                 |
| `Oxygen.Editor.Shell`     | When and where documents open, save, reload and close; prompts, the banner, input commit, Undo/Redo routing  |
| `Storage`                 | `IAtomicFileStore`: read with a `FileVersion`, write only over a current version                             |

## 7. Data Contracts

- `IDocumentKind`: `Name`, `FileSuffixes`,
  `DecodeAsync(DocumentFile, ReadOnlyMemory<byte>, DocumentDecodeContext, CancellationToken)`
  returning a `DocumentDecodeResult` (content or problem).
- `IDocumentContent`: `HasEditsInProgress`, `FinishEditsAsync`, `Capture`,
  `CaptureCopy(DocumentFile)`, `ApplyRelocation(DocumentRelocation)`.
- `DocumentFile(FileSystemPath Path, AssetPath Asset)`.
- `DocumentDecodeContext(DocumentId Document, HistoryKeeper History)`.
- `DocumentCapture`: `Of(encode)` or `Refused(reason)`; `Refusal`.
- `DocumentRelocation(DocumentFile File, FileVersion Version, Func<AssetPath, AssetPath?> MapAsset)`.
- `HistoryPosition`: an opaque value; `HistoryKeeper.Position` is the current
  one.
- `ProjectAssetLocation(AssetPath Path, FileSystemPath File, FileSystemPath MountRoot, ProjectMountKind MountKind)`,
  with `Identity` and `IsMountRoot`.

## 8. Commands, Services, Or Adapters

- `OpenDocuments`: `FindKind(path)`, `Find(path)`, `Find(DocumentId)`,
  `FindByAsset(AssetPath)`, `OpenAsync(DocumentFile)`, `Close(document)`,
  `Opened` and `Closed`. Opening a file already being opened joins that open.
- `Document`: `SaveAsync`, `SaveCopyAsync`, `CheckFileAsync`, `ReloadAsync`,
  `KeepMineAsync`, `FollowRelocationAsync`.
- `ProjectAssetFiles` (5.7).

## 9. UI Surfaces

None of its own. The conflict and missing banner, the save and delete prompts
and the tab are the Shell's ([editor-documents.md](editor-documents.md),
[document-identity.md](document-identity.md)).

## 10. Persistence And Round Trip

A document writes one file (decision 7) through `IAtomicFileStore`, only over
the version it read or last saved. A save that meets a changed file writes
nothing and marks the document changed on disk. A copy writes only where no file
exists.

## 11. Live Sync / Cook / Runtime Behavior

None. The cook reads saved files only and asks `IUnsavedDocuments` before an
explicit cook
([editor-documents.md](editor-documents.md#533-asking-before-play-and-cook)).

## 12. Operation Results And Diagnostics

`DocumentOpenResult`, `DocumentSaveResult`, `DocumentReloadResult` and
`DocumentFileState` carry the outcome: opened, already open, or the problem; a
save written, unchanged, refused by the capture, or a conflict. The Shell turns
them into operation results and the banner; a conflict is document state, not a
failure.

## 13. Dependency Rules

- `Oxygen.Editor.Documents` references `DroidNet.Documents` (for `DocumentId`),
  `Oxygen.Managed.Core`, `Storage` and `DroidNet.TimeMachine`, and nothing with
  windows or tabs.
- A kind and its content reference `Oxygen.Editor.Documents`, never the Shell.
- Projects knows no document kinds, formats or callers.

## 14. Validation Gates

- Documents tests over a fake kind: open once per file and the kind chosen by
  suffix; undo back to the saved point; an edit made while saving; a conflict
  and Keep Mine; the document's own save heard back; quiet reload, changed on
  disk, missing and restored; follow without dirtiness or a history entry; a
  copy for a new identity; a failed decode or capture.
- TimeMachine tests of the history's position, including view-only entries.
- Projects tests of `ProjectAssetFiles`, and the tests of the modules that use
  it.

## 15. Open Issues

None.
