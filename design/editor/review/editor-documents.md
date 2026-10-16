# Editor documents, history, saving and cooking

Status: **agreed design** (no code follows it yet)

Scope: what an open document is in the Oxygen Editor, who owns its undo history,
and how saving and cooking meet. Module boundaries are in
[editor-module-architecture.md](editor-module-architecture.md); the Shell is in
[editor-shell.md](editor-shell.md).

An open document is owned once, by the Shell; it has one undo history; and the
cook sees every change on disk the same way and reads saved files only.

## 1. Decisions

1. **One history per open document, and one global history.** The Shell's
   document service creates a document's history when it opens and drops it when
   it closes. Actions outside a single document (relocating, renaming or
   deleting assets, project settings) and actions that change several documents
   go to the global history; undoing a multi-document action asks for
   confirmation. Undo and Redo are Shell commands, routed to the active
   document's history, or to the global one when the user works in a part
   without a document. Domain code only records its changes, as TimeMachine
   changes and transactions, into the history it is given; it never creates,
   finds or clears one. Godot 4 made the same move from one global history to
   per-scene histories plus a global one.
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
   - **Change sets** are how the scene commands group multi-node actions today
     (`BeginChangeSet` in duplicate, move and restore). They have no rollback,
     so those actions become transactions; TimeMachine keeps change sets for
     grouping the changes recorded while a grouped entry is undone or redone.
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
     `DocumentService`; it replaces `IEditorDocumentService`. The Aura-facing
     contract in `DroidNet.Documents`, today also called `IDocumentService`, is
     renamed `IDocumentTabSource`, because it only feeds the tabs of a window,
     so the editor has one document service and one name for it.
3. **`Oxygen.Editor.World` is the one model of the world.** It knows what a
   scene, a material, a texture, an input mapping context or a script is, and
   nothing of documents, windows or editors. It is not split per editor.
   - **In World today:** `Scene`, with its node hierarchy, environment, play
     settings and scene references (scripts, input actions, physics sidecars);
     `SceneNode` and `GameObject`; the components (transform, geometry, cameras,
     lights, local fog volume, scripting with its script slots and values); the
     slots (materials, levels of detail, rendering, lighting).
   - **Material, to add.** The file format, `MaterialSource` and its reader and
     writer, stays in `Oxygen.Managed.Assets`, shared with the engine's tools.
     The editable material is today inside the material editor's document
     service; World gains `Material` as the editor's model over that format.
   - **Texture, to add.** Its descriptor is read and written by
     ContentPipeline's `TextureSourceAssetImporter`, with no model. World gains
     `Texture`: its source image, cube layout and settings.
   - **Input mapping context and input action, to add.** Today they are JSON
     files that script analysis checks against a schema, with no model. World
     gains `InputMappingContext` and `InputAction`, with their problems, which
     the input editors edit.
   - **Script, to add.** World holds only the references (`ScriptSlot`,
     `ScriptValue`); the declared parameters are read in the scene inspector.
     World gains `Script`: its source file, declared parameters and problems.
     ContentPipeline computes those facts (4.2).
   - **Not added:** geometry, which is imported rather than authored, and the
     physics scene sidecar, until a physics editor needs it.
4. **A cook reads saved files only.** Unsaved edits never reach cooked output.
5. **The cook sees every change on disk the same way.** A Save is one more file
   change; nothing tells the pipeline that a document was saved.
6. **The user decides when cooking happens:** with the cook-on-change setting,
   or by cooking when they choose.
7. **Each document type is a World model and an editor; Projects and Storage
   stay generic.** Storage's `IAtomicFileStore` reads a file with its
   `FileVersion` and writes it only if that version is still current, so an
   outside change surfaces as a conflict. Projects resolves an asset to its file
   under the project's authoring roots and back, and knows no formats, World
   types, document kinds or callers; its scene-specific parts
   (`SceneSourceVersion`, `GetSceneSourceVersion`, `SceneAssetRenameOperation`,
   scene staging) move to Scenes. World reads and writes each model's file
   format (for materials, through `MaterialSource` in `Oxygen.Managed.Assets`).
   An editor contributes its document type to the Shell (the file suffixes it
   opens and the World model it edits), edits that model, and records its
   changes into the history of its document. The Shell's document service opens,
   saves, reloads and closes every type the same way. A domain module exists
   only for work that several UI parts share, that runs outside the UI, or that
   needs the engine or native code: Scenes (one scene is edited by the scene
   editor, explorer and inspector), Assets, ContentPipeline and LivePreview.
   Materials, textures, input files and scripts have none. Neither Projects nor
   Storage references a domain module or an editor.

## 2. Saving and cooking: the scenarios

Everything here acts on one of two things: the files on disk, which are all a
cook ever reads, and the edits that live only in an open editor until they are
saved. The actors are the user; the open documents (a scene, a material, and
soon scripts and input mapping contexts), any of which may hold unsaved edits or
an edit in progress; tools outside the editor (a code editor, a paint or DCC
tool, source control); the cook; and the engine, which only ever runs cooked
content. Every scenario answers the user's one question: _is what the engine
shows me what I am looking at?_

### 2.1 Save with cook on change

The user tweaks a material's roughness and presses Ctrl+S. Save returns the
moment the file is written; nothing about cooking can slow it down or make it
fail. The cook notices the changed file, waits out a short quiet window in case
more changes follow (4.6), then cooks the material and everything that depends
on it. Wherever the material appears (the Content Explorer, an inspector picker,
the material editor itself) it moves from "needs cooking" to "cooking" to
"cooked".

### 2.2 Save without cook on change

The same Save with cook on change turned off. The material shows "needs cooking"
and nothing runs. The user cooks when it suits them: that asset, a folder, or
everything that needs it.

### 2.3 A change outside the editor

The user saves a script in their code editor, re-exports a texture source from a
paint tool, or pulls a branch that rewrites a scene. To the cook these are
indistinguishable from a Save in the editor, and they follow 2.1 or 2.2. If the
file is open, the document service reacts once, the same way for every kind: a
document without unsaved edits quietly reloads; a dirty one shows the conflict
banner (Reload, Keep Mine, Save a Copy); one whose file was deleted or moved
stays open, marked missing on disk, until the user saves it again or closes it.

### 2.4 Play with unsaved edits

The user presses Play while two of the scene's inputs have unsaved edits. The
question comes where they pressed Play: _"Save 2 documents and play?"_, with
**Save and Play** or **Cancel**. Save and Play commits any edit in progress,
saves the two documents, cooks whatever the scene needs that is stale, and
starts the player. With nothing unsaved, Play goes straight to cooking what is
stale. Once the player runs, any new edit marks the session stale, as it does
today.

### 2.5 Cook with unsaved edits

The user cooks a folder while one of its materials has unsaved edits. Same
question, same place: _"Save 1 document and cook?"_, with **Save and Cook** or
**Cancel**. A cook never sits in a queue waiting for someone to save.

### 2.6 Cook on change while a document is dirty again

The user saves a scene, which starts a cook on change, and goes on editing. The
cook cooks the file as saved, because that is what changed on disk; it neither
asks nor waits. The scene shows "cooked" with "unsaved changes" beside it, and
the next Save cooks again.

### 2.7 An edit in progress

The user is halfway through dragging a light's intensity slider and presses
Ctrl+S, Play or Cook. The drag is committed first, as its own entry in the
history, and the operation carries on as if the slider had been released.

### 2.8 Rename or move a referenced asset

The user renames a material that an open scene uses. The relocation rewrites the
references on disk, every open document follows its file, and the whole rename
is one entry in the global history that a single Undo reverses. The cook sees
the changed files like any others (2.1 or 2.2). The scene's preview keeps
showing the material as it was until the renamed material is cooked and mounted,
then shows it under its new name.

### 2.9 Every operation that meets unsaved edits

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
move need the saved files, so their only alternative is Cancel. Today closing
already asks, through `DocumentCloseCoordinator`; Play saves without asking,
Cook parks itself, and relocation refuses with "Save or discard the changes
first" (section 3).

## 3. What happens today

| Scenario | Today                                                                                                                                                                                                                                                                                                                                                                                                                                                                           | Where it differs                                                                                          |
| -------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| 2.1, 2.2 | Cook on change is on by default. The scene command service, scene rename and the material document service call `IAutomaticCookService.NotifySaved` after each Save. "Pause automatic cooking" is a toggle in the Content Browser's cook menu, held in memory and reset when another project opens                                                                                                                                                                              | The pipeline is told about saves instead of seeing them; the user's choice is not a setting               |
| 2.3      | The asset catalog watches the project's files (`FileSystemAssetCatalog`, rescanning after a watcher overflow) and the asset feed updates statuses. Script and input files are cooked after script analysis finds a changed file without problems. A scene or material changed outside the editor is not cooked automatically. Missing on disk is handled separately by the material service, the scene command service, Projects, both editor view models and the document host | Two change paths; editor and external changes behave differently; the document reaction exists five times |
| 2.4      | `PlaySessionService` calls `SaveAllAsync` with no prompt, cooks the current scene, then starts the player; a dirty document marks a running session stale                                                                                                                                                                                                                                                                                                                       | Play saves every open document without asking                                                             |
| 2.5      | A cook with a dirty input parks in `NeedsSave` ("Waiting for participating documents to be saved"); an explicit cook reveals the Cooking panel, an automatic one waits silently; a Save resumes it                                                                                                                                                                                                                                                                              | The question is asked in another panel, as a parked job                                                   |
| 2.6      | A cook on change with a dirty input parks in `NeedsSave` until the next Save                                                                                                                                                                                                                                                                                                                                                                                                    | The saved change is not cooked                                                                            |
| 2.7      | The scene command service treats a gesture with a changed preview as unsaved when the cook reads under its save gate                                                                                                                                                                                                                                                                                                                                                            | Handled by locking the editor rather than committing the edit                                             |
| 2.8      | `AssetRelocationWorkflow` keeps a single-level undo of its own; open documents follow through `DocumentManager` and the material service separately                                                                                                                                                                                                                                                                                                                             | A history outside TimeMachine; following files exists twice                                               |

To make a cook read only saved files, editors register their open documents in
the pipeline's `CookDocumentRegistry` (the material service directly, scenes
through `SceneCookInputRegistrar`), with read leases that take each document's
save gate. Saves already write through the atomic file store, so a reader never
sees a partly written file.

## 4. Proposed behavior

### 4.1 Where the cook hears of changes

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
into cook inputs (4.2, 4.6); and the Shell's document service, which keeps open
documents in step with their files (4.4). None of them watches files itself, and
the cook never reads the feed.

A Save, an external edit, a checkout and a relocation all arrive the same way.
`IAutomaticCookService.NotifySaved` and `NotifyRenamed` go, and document
services stop calling the pipeline. A change whose bytes match the last cooked
input (by content hash) cooks nothing, so a Save that changes nothing is free.

### 4.2 Cook on change

A project setting, **Cook on change**, on by default. When on, the cook turns
the catalog's changes (4.1) into automatic runs, batched as 4.6 describes, and
cooks the changed inputs and their dependents, reading saved files only. It
never prompts and never waits for a Save: it cooks what is on disk (2.6). When
off, changed inputs show "needs cooking" and nothing runs (2.2). The setting
replaces the in-memory pause.

ContentPipeline checks scripts and input files before cooking them; the checks
produce the problems of their World models. Cook on change skips an input with
problems: they show in Problems and beside the asset, and the change that fixes
them cooks it. Script analysis is native and needs the engine's type
definitions, so ContentPipeline runs it through a port it owns, which
LivePreview implements, as it already reaches the native pipeline through
`IEngineContentPipelineApi`.

### 4.3 Asking before Play and Cook

An explicit Cook or Play first asks which of its input files have unsaved edits.
The question is a fact about documents, so `Oxygen.Editor.Documents` declares it
as `IUnsavedDocuments`: given file paths, it returns the open documents that
edit them with unsaved changes. The Shell's document service implements it, and
ContentPipeline consumes it through a new reference to
`Oxygen.Editor.Documents`, which is foundation; neither references the other.
When there are any, the Shell shows one confirmation where the user acted (2.4,
2.5). Save and continue commits edits in progress, saves those documents and
runs; Cancel does nothing. The parked `NeedsSave` state, the read leases, the
save gates and `CookDocumentRegistry` go.

### 4.4 External changes to open documents

The Shell's document service listens to the same catalog changes (4.1) and
reacts once for every kind: reload silently, conflict banner, or missing on disk
(2.3). The document kind only reloads or retargets its content when the service
tells it to.

### 4.5 Status the user sees

The asset feed remains the one source of an asset's status: needs cooking,
cooking, cooked, failed, plus "unsaved changes" when an open document edits it.
Every place that shows an asset shows that status. A status compares the files
on disk with the published content; it reads them without waiting for any
document or writer.

### 4.6 Bursts of changes and requests

Today the project has one cook writer: every run waits its turn, and explicit,
Play and preview work goes ahead of automatic work. Each asset has at most one
running and one pending automatic cook; the pending one keeps only the latest
saved content hash, and a running cook is never interrupted. Identical pending
scopes are shared by their callers. There is no quiet window, so a Save All
starts one automatic cook per file.

Proposed:

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
