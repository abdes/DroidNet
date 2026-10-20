# Editor Documents LLD

Status: `agreed`

Last Updated: 2026-10-20 01:32

## 1. Purpose

Define what an open document is in the Oxygen Editor, who owns its undo history,
and how saving and cooking meet. An open document is owned once, by the Shell;
it has one undo history; and the cook sees every change on disk the same way and
reads saved files only. Module boundaries are in
[editor module architecture](editor-module-architecture.md); the Shell's frame
is in [editor-shell.md](../review/editor-shell.md); the common behavior of a
document and the document kinds are in [document-kinds.md](document-kinds.md).

## 2. PRD Traceability

| ID        | Coverage                                                                                                |
| --------- | ------------------------------------------------------------------------------------------------------- |
| `REQ-006` | Every authoring change is recorded into one history, the document's or the global one, and marks dirty. |
| `REQ-018` | The cook reads saved files only and cooks them on change or on request.                                 |
| `REQ-022` | Save, Cook, Play and relocation that meet unsaved edits ask once, where the user acted.                 |
| `REQ-026` | Preview shows published content; a change is seen after it is saved and cooked.                         |
| `REQ-044` | Play saves on the user's confirmation and cooks the scene's stale inputs before the player starts.      |

## 3. Architecture Links

- [editor-module-architecture.md](editor-module-architecture.md): the layers and
  modules that own each part of this design.
- [document-kinds.md](document-kinds.md),
  [document-identity.md](document-identity.md): a document's common behavior,
  its kinds and its identity.
- [editor-shell.md](../review/editor-shell.md): the document service, prompts,
  commands and placement in the Shell.
- [asset-family-modules.md](asset-family-modules.md): the models each document
  kind edits and the checks the cook relies on.
- [content-cooking-workflows.md](content-cooking-workflows.md): cook requests,
  statuses and recovery.

## 4. Current Baseline

The steps that bring the code to this design, and what each has done, are S1.9
to S1.12 of the
[issue-069 plan](../plan/issue-069-di-topology-and-decomposition.md).

## 5. Target Design

### 5.1 Decisions

1. **One history per open document, and one global history.** A document's
   history is created when the document opens and dropped when it closes, by the
   open-once behavior of `Oxygen.Editor.Documents` (`OpenDocuments`), and handed
   to the document; the Shell's `IHistoryService` holds the global history.
   Actions outside a single document (relocating, renaming or deleting assets,
   project settings) and actions that change several documents go to the global
   history; undoing a multi-document action asks for confirmation. Undo and Redo
   are Shell commands, routed to the active document's history, or to the global
   one when the user works in a part without a document. Domain code only
   records its changes, as TimeMachine changes and transactions, into the
   history it is given; it never creates, finds or clears one. Godot 4 made the
   same move from one global history to per-scene histories plus a global one.
   - **A simple change** records one action that has one inverse: setting a
     property, renaming a node, toggling a flag. It is one `AddChange` with that
     inverse.
   - **A transaction** records one user action made of several changes, or one
     that can fail part way: moving, duplicating or pasting several nodes,
     applying a material to many slots, a drag that changes a value
     continuously. Begin it, add the changes, commit when the action succeeds
     (one entry, one label in the Undo menu), roll back when it fails, so a
     failure leaves no partial entry. An inner transaction joins its outer one.
   - **A multi-document action** (renaming an asset that open scenes reference)
     is one transaction in the global history; its changes ask each document
     kind to apply or revert its part.
   - **Change sets** have no rollback, so they never group a user action;
     TimeMachine keeps them only for grouping the changes recorded while a
     grouped entry is undone or redone. A scene edit records one
     `SceneHistoryEntry` per applied edit or batch.
   - **A History panel**, passive like Output and Problems, shows the active
     document's history and the global history: each entry's label, the undone
     entries after the current point, and which history it belongs to. Choosing
     an entry undoes or redoes everything up to it in one step, as Unity's Undo
     History window does. It reads the histories through the Shell's document
     service and changes nothing else; during development it is also how we see
     what each action recorded.
2. **Three layers for documents.**
   - Aura and `DroidNet.Documents` draw the tabs of one window. Only the Shell
     feeds them; features never see them.
   - `Oxygen.Editor.Documents` says what a document is, independent of where it
     shows: identity, the file it edits, its kind, its saved and current
     versions, whether it is dirty, missing or changed on disk, and the contract
     a document kind implements. No windows, no tabs.
   - The Shell owns the one document service: when and where documents open,
     save, reload and close; placing the document, projecting it to tabs,
     prompts, the missing and conflict banner, and routing Undo and Redo to the
     histories. How a document opens once per file, saves against the version it
     read, reloads and follows a relocated file is common behavior of
     `Oxygen.Editor.Documents`, without windows
     ([document-kinds.md](document-kinds.md)). `EditorDocumentService`,
     `DocumentHostViewModel` and `DocumentManager` fold into the Shell's
     service.

     Its name is `IDocumentService` in `Oxygen.Editor.Shell`, implemented by
     `DocumentService`. The Aura-facing contract in `DroidNet.Documents` is
     `IDocumentTabSource`, because it only feeds the tabs of a window, so the
     editor has one document service and one name for it.
3. **`Oxygen.Editor.World` is the scene model; each authored asset family has
   its own module.** World knows what a scene is, and nothing of documents,
   windows, editors or other families' rules: `Scene`, with its node hierarchy,
   environment, play settings and scene references; `SceneNode` and
   `GameObject`; the components (transform, geometry, cameras, lights, local fog
   volume, scripting with its script slots and values); the slots (materials,
   levels of detail, rendering, lighting); and the value types a scene stores
   when it references another family. The models of the other families are in
   their family modules ([asset family modules](asset-family-modules.md)):
   `Material` in Materials, `Texture` in Textures, `InputContainer` with its
   `InputAction` and `InputMappingContext` in Input, and `Script` in Scripting.
   Their file formats stay in `Oxygen.Managed.Assets`, shared with the engine's
   tools. Geometry, which is imported rather than authored, and the physics
   scene sidecar have no model until an editor needs one.
4. **A cook reads saved files only.** Unsaved edits never reach cooked output.
5. **The cook sees every change on disk the same way.** A Save is one more file
   change; nothing tells the pipeline that a document was saved.
6. **The user decides when cooking happens:** with the cook-on-change setting,
   or by cooking when they choose.
7. **Each document type is a family model and an editor; Projects and Storage
   stay generic.** Storage's `IAtomicFileStore` reads a file with its
   `FileVersion` and writes it only if that version is still current, so an
   outside change surfaces as a conflict. Projects resolves an asset to its file
   under the project's authoring roots and back, and knows no formats, World
   types, document kinds or callers; its scene-specific parts
   (`SceneSourceVersion`, `GetSceneSourceVersion`, `SceneAssetRenameOperation`,
   scene staging) move to Scenes. Each family module reads and writes its
   model's file format through `Oxygen.Managed.Assets` (for materials,
   `MaterialSource`). An editor contributes its document type to the Shell (the
   file suffixes it opens and the family model it edits), edits that model, and
   records its changes into the history of its document. The Shell's document
   service opens, saves, reloads and closes every type the same way. A domain
   module exists for work that several UI parts share, that runs outside the UI,
   or that needs the engine or native code: Scenes (one scene is edited by the
   scene editor, explorer and inspector), Assets, ContentPipeline and
   LivePreview, and one family module for each authored asset family (Scripting,
   Input, Textures, Materials), each of which meets that rule
   ([asset family modules](asset-family-modules.md)). Neither Projects nor
   Storage references a domain module or an editor.

### 5.2 Saving and cooking: the scenarios

Everything here acts on one of two things: the files on disk, which are all a
cook ever reads, and the edits that live only in an open editor until they are
saved. The actors are the user; the open documents (a scene, a material, and
soon scripts and input mapping contexts), any of which may hold unsaved edits or
an edit in progress; tools outside the editor (a code editor, a paint or DCC
tool, source control); the cook; and the engine, which only ever runs cooked
content. Every scenario answers the user's one question: _is what the engine
shows me what I am looking at?_

#### 5.2.1 Save with cook on change

The user tweaks a material's roughness and presses Ctrl+S. Save returns the
moment the file is written; nothing about cooking can slow it down or make it
fail. The cook notices the changed file, waits out a short quiet window in case
more changes follow (5.3.6), then cooks the material and everything that depends
on it. Wherever the material appears (the Content Explorer, an inspector picker,
the material editor itself) it moves from "needs cooking" to "cooking" to
"cooked".

#### 5.2.2 Save without cook on change

The same Save with cook on change turned off. The material shows "needs cooking"
and nothing runs. The user cooks when it suits them: that asset, a folder, or
everything that needs it.

#### 5.2.3 A change outside the editor

The user saves a script in their code editor, re-exports a texture source from a
paint tool, or pulls a branch that rewrites a scene. To the cook these are
indistinguishable from a Save in the editor, and they follow 5.2.1 or 5.2.2. If
the file is open, the document service reacts once, the same way for every kind:
a document without unsaved edits quietly reloads; a dirty one shows the conflict
banner (Reload, Keep Mine, Save a Copy); one whose file was deleted or moved
stays open, marked missing on disk, until the user saves it again or closes it.

#### 5.2.4 Play with unsaved edits

The user presses Play while two of the scene's inputs have unsaved edits. The
question comes where they pressed Play: _"Save 2 documents and play?"_, with
**Save and Play** or **Cancel**. Save and Play commits any edit in progress,
saves the two documents, cooks whatever the scene needs that is stale, and
starts the player. With nothing unsaved, Play goes straight to cooking what is
stale. Once the player runs, any new edit marks the session stale.

#### 5.2.5 Cook with unsaved edits

The user cooks a folder while one of its materials has unsaved edits. Same
question, same place: _"Save 1 document and cook?"_, with **Save and Cook** or
**Cancel**. A cook never sits in a queue waiting for someone to save.

#### 5.2.6 Cook on change while a document is dirty again

The user saves a scene, which starts a cook on change, and goes on editing. The
cook cooks the file as saved, because that is what changed on disk; it neither
asks nor waits. The scene shows "cooked" with "unsaved changes" beside it, and
the next Save cooks again.

#### 5.2.7 An edit in progress

The user is halfway through dragging a light's intensity slider and presses
Ctrl+S, Play or Cook. The drag is committed first, as its own entry in the
history, and the operation carries on as if the slider had been released.

#### 5.2.8 Rename or move a referenced asset

The user renames a material that an open scene uses. The relocation rewrites the
references on disk, every open document follows its file, and the whole rename
is one entry in the global history that a single Undo reverses. The cook sees
the changed files like any others (5.2.1 or 5.2.2). The scene's preview keeps
showing the material as it was until the renamed material is cooked and mounted,
then shows it under its new name.

#### 5.2.9 Every operation that meets unsaved edits

Play and Cook are not the only operations that run into unsaved edits. Each of
them gets the same Shell confirmation, naming the affected documents, and each
commits edits in progress first.

| Operation                                                                      | The user is asked                            | Choices                                              |
| ------------------------------------------------------------------------------ | -------------------------------------------- | ---------------------------------------------------- |
| Close a document, close a window, exit the editor, close or switch the project | _"Save changes to 2 documents?"_             | **Save**, **Don't Save**, **Cancel**                 |
| Play, Cook                                                                     | _"Save 2 documents and play?"_               | **Save and Play** (or **Save and Cook**), **Cancel** |
| Rename or move an asset whose open document is dirty                           | _"Save 1 document and rename?"_              | **Save and Rename**, **Cancel**                      |
| Delete an asset whose document is open                                         | _"Delete Wood? It is open and will close."_  | **Delete**, **Cancel**                               |
| Reload a dirty document from disk after an outside change                      | _"Discard your changes to Wood and reload?"_ | **Reload**, **Keep Mine**, **Save a Copy**           |

Only closing offers Don't Save, because closing throws the edits away anyway,
and deleting discards them with the file it deletes; Play, Cook and a rename or
move need the saved files, so their only alternative is Cancel.

### 5.3 Behavior

#### 5.3.1 Where the cook hears of changes

There is no new stream. The cook listens to `IAssetCatalog.Changes`, the same
changes the Assets module already has: its `AssetCatalog` watches each authoring
root with a `FileSystemAssetCatalog` (rescanning after a watcher overflow) and
reports added, changed, removed and relocated files. `IAssetCatalog` is declared
in `Oxygen.Managed.Assets`, so ContentPipeline depends on that interface only,
never on the Assets module, and the composition root hands it the one
`AssetCatalog`. The cook keeps the changes under authoring roots and ignores
libraries and built-ins.

Only the Assets module's `AssetCatalog` watches files. Three parts listen to it,
each through `IAssetCatalog`: the asset feed (`IAssetFeed`), which turns it into
entries with their status for people to see; the cook, which turns the changes
into cook inputs (5.3.2, 5.3.6); and the Shell's document service, which keeps
open documents in step with their files (5.3.4). None of them watches files
itself, and the cook never reads the feed.

A Save, an external edit, a checkout and a relocation all arrive the same way.
`IAutomaticCookService.NotifySaved` and `NotifyRenamed` go, and document
services stop calling the pipeline. A change whose bytes match the last cooked
input (by content hash) cooks nothing, so a Save that changes nothing is free.

#### 5.3.2 Cook on change

A project setting, **Cook on change**, on by default. When on, the cook turns
the catalog's changes (5.3.1) into automatic runs, batched as 5.3.6 describes,
and cooks the changed inputs and their dependents, reading saved files only. It
never prompts and never waits for a Save: it cooks what is on disk (5.2.6). When
off, changed inputs show "needs cooking" and nothing runs (5.2.2). The setting
replaces the in-memory pause.

The family modules check their sources (scripts, input sources) and publish the
problems in their facts ([asset family modules](asset-family-modules.md)). Cook
on change skips an input whose facts report errors: they show in Problems and
beside the asset, the run that cooks the other changes names it, and the change
that fixes them cooks it. A saved script cooks on change like any other input.
Script analysis is native and needs the engine's type definitions, so Scripting
runs it through its `IScriptAnalyzer` port, which LivePreview implements.

A run of several changed inputs is named by their count ("3 changed assets"),
of kind **Changes**; Retry cooks the same inputs as one requested run. A run of
one input is named by that asset, as an explicit cook of it would be.

#### 5.3.3 Asking before Play and Cook

An explicit Cook or Play first asks which of its input files have unsaved edits.
The question is a fact about documents, so `Oxygen.Editor.Documents` declares it
as `IUnsavedDocuments`: given file paths, it returns the open documents that
edit them with unsaved changes. The Shell's document service implements it, and
ContentPipeline consumes it through a new reference to
`Oxygen.Editor.Documents`, which is foundation; neither references the other.
When there are any, the Shell shows one confirmation where the user acted
(5.2.4, 5.2.5). Save and continue commits edits in progress, saves those
documents and runs; Cancel does nothing. The parked `NeedsSave` state, the read
leases, the save gates and `CookDocumentRegistry` go.

#### 5.3.4 External changes to open documents

The Shell's document service listens to the same catalog changes (5.3.1) and
reacts once for every kind: reload silently, conflict banner, or missing on disk
(5.2.3). The document kind only reloads or retargets its content when the
service tells it to.

#### 5.3.5 Status the user sees

The asset feed remains the one source of an asset's status: needs cooking,
cooking, cooked, failed, plus "unsaved changes" when an open document edits it.
Every place that shows an asset shows that status. A status compares the files
on disk with the published content; it reads them without waiting for any
document or writer.

#### 5.3.6 Bursts of changes and requests

The project has one cook writer: every run waits its turn, and explicit, Play
and preview work goes ahead of automatic work.

1. **Quiet window.** A change opens a 500 ms quiet window, and each further
   change restarts it, up to 5 s after the first change, so a tool that keeps
   writing still gets its files cooked. Both values are constants of the cook,
   not settings. When it closes, one automatic run covers every input that
   changed in it, so a Save All or a checkout of many files is one run.
2. **One waiting automatic run.** While an automatic run waits for the writer,
   new changes join it; there is never a second automatic run queued behind it.
3. **A running automatic run is not interrupted by changes.** Changes that
   arrive while it runs collect into the next run, which starts when it ends.
   Incremental cooking reuses outputs whose inputs did not change, so the
   follow-up run only cooks what changed again.
4. **Foreground work goes first.** When the user cooks or plays while an
   automatic run is running, the automatic run stops at its next point where it
   can stop without publishing a partial result, and the foreground run takes
   the writer. After it, the stopped inputs that are still stale return to the
   waiting automatic run; those the foreground run covered are not cooked again.
5. **Repeated requests.** A request identical to a pending one joins it, and a
   request whose inputs a pending request already covers (cook an asset inside a
   folder already queued) joins that one. Neither joins a run that has started;
   it queues once behind it. When a request joins a pending automatic run, that
   run does the request's work.
6. **Play** is foreground and needs only the scene's inputs. Behind a running
   cook it queues like any request: that cook read its inputs before Play's
   save, so Play cooks after it, and incremental cooking reuses everything the
   running cook already produced.

## 6. Ownership

| Owner                           | Owns                                                                                                                                                                                        |
| ------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `Oxygen.Editor.Shell`           | The document service (`IDocumentService`): placement, tabs, prompts, the missing and conflict banner, Undo and Redo routing; the global history (`IHistoryService`)                         |
| `Oxygen.Editor.Documents`       | What a document is: identity, file, kind, versions, dirty, missing and changed on disk; open once per file, save against the version read, reload, follow a relocation; `IUnsavedDocuments` |
| Document kinds                  | Decoding, encoding and the content of their documents (Scenes, Materials)                                                                                                                   |
| `Oxygen.Editor.Assets`          | The only file watcher (`AssetCatalog`) and the asset feed's statuses                                                                                                                        |
| `Oxygen.Editor.ContentPipeline` | Cook on change, its batching, and asking before a cook through `IUnsavedDocuments`                                                                                                          |
| `Storage`                       | `IAtomicFileStore`: read with a `FileVersion`, write only if it is still current                                                                                                            |
| History panel                   | A passive view over the active document's and the global history                                                                                                                            |

## 7. Data Contracts

- `IDocumentService` (Shell, implemented by `DocumentService`) replaces
  `IEditorDocumentService`; the Aura-facing contract of `DroidNet.Documents` is
  `IDocumentTabSource` (5.1, decision 2).
- `IUnsavedDocuments` (`Oxygen.Editor.Documents`): given file paths, the open
  documents that edit them with unsaved changes (5.3.3).
- **Cook on change** is a project setting (`ProjectSettings.CookOnChange`), on
  by default (5.3.2).
- The quiet window is 500 ms, up to 5 s after the first change; both are
  constants of the cook, not settings (5.3.6).

## 8. Services And Adapters

- `IAssetCatalog.Changes` is the one stream of file changes. The asset feed, the
  cook (`AutomaticCookService`) and the Shell's document service listen to it;
  none watches files itself (5.3.1).
- The Shell's document service implements `IUnsavedDocuments`; ContentPipeline
  consumes it through `Oxygen.Editor.Documents`, which is foundation (5.3.3).
- Domain code records into the history it is given, as TimeMachine changes and
  transactions; it never creates, finds or clears a history (decision 1).

## 9. UI Surfaces

- The confirmations of 5.2.9, shown by the Shell where the user acted.
- The missing and conflict banner of a document (Reload, Keep Mine, Save a
  Copy).
- The History panel (decision 1).
- Every place that shows an asset shows the feed's status, with "unsaved
  changes" beside it when an open document edits it (5.3.5).

## 10. Persistence And Round Trip

Save writes through the atomic file store against the version the document read,
so an outside change surfaces as a conflict rather than being overwritten. The
cook reads saved files only; unsaved edits never reach cooked output.

## 11. Cook / Mount / Runtime Behavior

The rules of 5.3.6 order automatic, explicit, Play and preview work over the one
cook writer. Play is foreground, needs only the scene's inputs and starts the
player once they are cooked; a new edit while it runs marks the session stale.

## 12. Operation Results And Diagnostics

A cook skipped for input problems leaves the problems in Problems and beside the
asset; the asset keeps "needs cooking" until the change that fixes them cooks
it. A refused save against a changed file is a conflict on the document, not an
operation failure.

## 13. Dependency Rules

- Only the Shell feeds Aura's tabs; features never see them (decision 2).
- `Oxygen.Editor.Documents` holds no windows and no tabs.
- Domain modules never create, find or clear a history.
- Projects and Storage reference no domain module and no editor (decision 7).
- Only the Assets module watches files.

## 14. Validation Gates

- The confirmations of 5.2.9 and the conflict banner in the running editor.
- Cook on change: one run per quiet window, changes during a run joining the
  next, foreground work first, a skipped input with problems (ContentPipeline
  and LivePreview tests).
- History: per-document and global entries, transactions committing as one entry
  and rolling back without a partial entry, Undo and Redo routing (Shell and
  Documents tests).

## 15. Open Issues

None.
