# Document Identity LLD

Status: `agreed`

Last Updated: 2026-10-20 01:32

## 1. Purpose

Define how an open document is identified, shown as a tab, found, and reached by
the parts that work on it. It sits between
[document-kinds.md](document-kinds.md) (what a document is, without windows) and
[editor-shell.md](../review/editor-shell.md) (the frame that places documents).
Who owns a document and its history is in
[editor-documents.md](editor-documents.md).

An open document has one identity, one tab and one registry. A tab is a
read-only view of its document, panels reach the documents of their window
without a window id, and a new kind adds a kind and an editor descriptor and
changes nothing else.

## 2. PRD Traceability

| ID         | Coverage                                                                                                            |
| ---------- | ------------------------------------------------------------------------------------------------------------------- |
| `GOAL-004` | Materials are documents of the material kind, edited through `Document.History`, ready for texture and graph kinds. |
| `REQ-006`  | A tab's dirty state is the document's, which follows the history's position.                                        |
| `REQ-010`  | New Material creates a material through asset authoring and opens it as a document.                                 |
| `REQ-011`  | Material property edits are recorded as history entries of the material document.                                   |
| `REQ-028`  | Persisted view state (viewport layout and cameras) is keyed by the scene's identity, not the session's id.          |

## 3. Architecture Links

- [document-kinds.md](document-kinds.md): `Document`, `OpenDocuments`, the kind
  and content contracts.
- [editor-documents.md](editor-documents.md): the three document layers, the
  Shell's document service, `IUnsavedDocuments`.
- [editor-shell.md](../review/editor-shell.md): scopes, window kinds,
  contributions and the context service.
- [asset-family-modules.md](asset-family-modules.md): the `Material` model.
- [editor-module-architecture.md](editor-module-architecture.md): module
  placement and the `CookedInspector` feature.
- [scene-edits.md](scene-edits.md): the scene kind's content.

## 4. Current Baseline

The work that brings the code to this design is tracked in the
[issue-069 plan](../plan/issue-069-di-topology-and-decomposition.md).

## 5. Target Design

### 5.1 Decisions

1. **`DocumentId` lives in `DroidNet.Documents`** and types Aura's document
   contract, so Aura and the editor share the type.
2. **Every tab is a real `Document`.** Cooked inspection is a panel (5.11).
3. **At most one tab per document in the editor;** the tab's key is the
   document's id. Splits happen inside an editor. A tab is in at most one area;
   it moves between areas only by Aura's detach and attach into a document
   window (5.7).
4. **Materials are documents** (5.9), the second kind after scenes.
5. **Tabs read the document;** nothing pushes tab state (5.4, 5.5).
6. **`OpenDocuments` is the only lookup** (5.6), one per project session.
7. **Parts reach their window's documents through `IDocumentArea`** and never
   pass a `WindowId` (5.7). The input committer and the prompts are Shell
   services of the part's window and take the `DocumentTab`.
8. **An editor descriptor per kind** (5.8); the host has no switch over kinds.
9. **In-session editor state is keyed by `DocumentId`;** persisted view state by
   content identity (5.3).
10. **A document closes in `OpenDocuments` through the area's close** (5.7),
    never from an editor.
11. **No revision counters.** Dirty is the history's position; nothing counts
    revisions of a document (5.10).

### 5.2 Scenarios

The actors: the user; the document area of a workspace window and its tabs; an
editor (scene, material, and later texture, input, script); the panels that
follow the active document (Scene Explorer, Inspector, content demand, Play, the
Cooking panel); the relocation workflow; the cook.

1. **Open an asset that is already open.** The user double-clicks a material in
   the Content Explorer. Its tab comes forward, in whichever window holds it.
   The request names the asset; the lookup finds the document by its file, never
   by a tab id.
2. **Panels follow the active document.** The user switches from the scene tab
   to a material tab and back. The Scene Explorer and Inspector show the scene's
   nodes while the scene is active and keep its selection when it returns.
3. **A tab shows the document's state.** An edit makes the tab dirty; Undo back
   to the saved point makes it clean; a file deleted outside the editor makes it
   dirty and shows the missing banner; a rename in the editor changes its title.
   Nobody pushes these to the tab.
4. **Save from elsewhere.** The Cooking panel's "Save listed" saves the
   documents of the listed source files; Play and Cook save all. Each finds the
   document by its file and saves it through its editor, so the editor's input
   commit and prompts still run.
5. **A new kind.** The texture editor arrives. It registers a kind and an editor
   descriptor. Nothing else in the editor changes: no switch, no metadata type,
   no lookup helper.
6. **Inspect cooked output.** The user inspects a folder's cooked packages. The
   Cooked Inspector panel shows that scope; a second request replaces it.
7. **One scene per window.** Opening a second scene in a window retires the
   first.
8. **Drag a tab out of the strip.** The tab moves into a document window with
   its document; the document never loses its tab.

### 5.3 One identity

`DocumentId` (`readonly record struct DocumentId(Guid Value)`, in
`DroidNet.Documents`) identifies an open document in the editor. It has `Empty`,
`IsEmpty`, `New`, `FromValue` and `ToString`, and no JSON converter: nothing
serializes it, and `DroidNet.Documents` does not reference
`Oxygen.Managed.Core`. A document has at most one tab in the editor, and that
tab's key is the document's id. There is no tab id, no conversion and no lookup
from one to the other.

Two boundaries keep `Guid`, each converting in one place:

- Aura's `TabItem.ContentId`, a property of the generic tab strip control that
  drag and tear-out compare. `DocumentTabPresenter` sets it from the document's
  id and reads it back with `DocumentId.FromValue`.
- The Runtime's surface ownership: a document's viewports are owned by
  `SurfaceOwnerId(documentId.Value)`, and closing the document releases them
  with `ReleaseOwnerSurfacesAsync`.

Editor state that lives while a document is open (selection, engine sync,
viewports, history, cook state) is keyed by `DocumentId`. A `DocumentId` is new
each time a file opens, so state persisted across sessions (the viewport layout
and cameras, the last opened scene) is keyed by the content's identity
(`SceneId`, the `AssetPath`), and in-session state never is.

### 5.4 Aura's contract

`IDocumentMetadata` is what a tab shows, read-only and observable:

```csharp
public interface IDocumentMetadata : INotifyPropertyChanged
{
    DocumentId DocumentId { get; }
    string Title { get; }
    Uri? IconUri { get; }
    bool IsDirty { get; }
    bool IsClosable { get; }
    bool IsPinnedHint { get; }
}
```

The Aura-facing tab contract, `IDocumentTabSource` (with
`IDocumentServiceState`), takes and returns `DocumentId` everywhere, including
`DocumentActivatedEventArgs` and `GetActiveDocumentId`. It has no metadata
update call: the presenter subscribes to each open tab's `PropertyChanged` when
it adds the tab, marshals the change through its `DispatcherQueue` (a tab may
raise on the thread that owns its document), and unsubscribes when the tab
closes or detaches and when the presenter is disposed. `OpenDocumentAsync`
returns `bool`, since the id is the metadata's. `DetachDocumentAsync` returning
`null` means the service refused: no event is raised and the tab stays.

Aura's `MultiWindow` sample and the presenter's tests implement the contract.

### 5.5 The tab is a view of the document

`DocumentTab : IDocumentMetadata`, in `Oxygen.Editor.Shell`, wraps one
`Document` and its editor descriptor:

- `DocumentId` is `Document.Id`; `Title` is `Document.Title`; `IsDirty` is
  `Document.IsDirty` (missing on disk counts,
  [document-kinds.md](document-kinds.md), section 5.4).
- It raises `PropertyChanged` from `Document.StateChanged` and
  `ContentReplaced`, on the thread that raised them: the UI thread of the window
  that opened the document, where every `Document` operation runs.
- `IsClosable` and `IconUri` come from the kind's editor descriptor (5.8).
- It exposes `Document` and `Editor`, so the Shell reaches the model and its
  editor from a tab.
- The document area creates it in `ShowAsync` and disposes it when the tab
  closes; disposing unsubscribes from the document.

There is no other metadata type. What a tab once carried belongs to its owner:
the asset is `Document.File.Asset`; missing on disk and dirty are the
`Document`'s; the viewport layout is in `ViewportStateService`, with the
cameras, keyed by `SceneId`. Parts that compare documents compare `Document.Id`.

### 5.6 One registry

`OpenDocuments` is the only way to find an open document: `Find(DocumentId)`,
`FindByAsset(AssetPath)` and `Find(FileSystemPath)`. `FindByAsset` compares
`Document.File.Asset` by `AssetPath` equality; both sides are in one form,
because every `DocumentFile` is resolved through Projects
([document-kinds.md](document-kinds.md), section 5.7).

There is one `OpenDocuments` per project session, so a file opens once across
the session's windows; it is registered in the session scope of
[editor-shell.md](../review/editor-shell.md#21-scopes).

### 5.7 The document area

`IDocumentArea`, in `Oxygen.Editor.Shell`, is the document area of the window a
part runs in. It is registered in the window's scope, so a part gets the area of
its own window without knowing a `WindowId`.

| Member                        | Meaning                                                                                                                                |
| ----------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `Documents`                   | The documents with a tab in this area                                                                                                  |
| `ActiveDocument`              | The document whose tab is selected, or none                                                                                            |
| `ActiveDocumentChanged`       | `EventHandler`, raised after the selection changes, on the UI thread                                                                   |
| `ShowAsync(document)`         | `Task<bool>`: selects the document's tab and brings its window forward; adds the tab here first when the document has none             |
| `PrepareCloseAsync(docs)`     | `Task<DocumentCloseTransaction?>`: the close transaction for the given documents of this area, or none when cancelled                  |
| `CloseAsync(document, force)` | `Task<bool>`: prepares and commits the close of one document: its participants' `CloseAsync`, then the tab, then `OpenDocuments.Close` |

A document's tab is in at most one area. `ShowAsync` for a document whose tab is
in another window selects it there and brings that window forward. Moving a tab
between areas is Aura's detach and attach: a torn-out tab is received by a
document window ([editor-shell.md](../review/editor-shell.md#22-window-kinds)),
and a detach that no window can receive is refused, leaving the tab in place.
The workspace close prepares every document of the window, and its prompt lists
each document's asset. A `Document` closes in `OpenDocuments` only through the
area's close; editors never call `OpenDocuments.Close`. An editor's
`IDocumentCloseParticipant` keeps its engine and view teardown.

The area is built over the Shell's tab service, the window's id and the window
manager's activation, which the composition root gives it. Features never see
any of them:

- The Scene Explorer, Inspector and content demand follow `ActiveDocument` when
  its content is a `SceneContent`.
- The scene editor asks whether its document is active by comparing
  `ActiveDocument` with its own.
- The Cooking panel and Play find documents through `OpenDocuments` and save
  them through the document service's save by asset (5.8).

The input committer (`IDocumentInputCommitter`), the close prompt,
`DocumentCloseItem` and the close coordinator are part of the Shell's document
service ([editor-documents.md](editor-documents.md#51-decisions), decision 2).
They act on the part's window, so they take no `WindowId`, and they take the
`DocumentTab`; the close prompt shows each document's asset. A document whose
file changed on disk is resolved by the document service for every kind
(Reload, Keep Mine, Save a Copy), from the document area's banner or from the
close prompt; editors have no conflict handling of their own.

`IDocumentArea` is the first part of the Shell's context service
([editor-shell.md](../review/editor-shell.md#4-services-offered-to-parts)),
beside the active part and the active window.

### 5.8 Editors per kind

Each kind has a
`DocumentEditorDescriptor(IDocumentKind kind, Func<DocumentTab, object> createEditor)`,
the editor contribution of
[editor-shell.md](../review/editor-shell.md#3-contributions), keyed by the kind:

- the kind it edits;
- `IsClosable` (the scene's tab is not) and `IconUri`;
- a factory that creates the editor for a document's `DocumentTab`, from the
  window scope. The tab gives the editor its `Document`, and it is the one
  `IDocumentMetadata` of the document, which the editor passes to the window's
  prompts (5.7); an editor never builds or looks up a tab. `CreateEditor`
  refuses a tab of another kind.

The workspace composition registers each kind with `OpenDocuments`, and the
module that edits the kind contributes its descriptor through an
`IDocumentEditorProvider` (`SceneEditorProvider`, `MaterialEditorProvider`). The
document service creates an editor from `tab.Editor`; it has no switch over
kinds and no `Create*Editor` per kind. The editor contract is `IAsyncSaveable`
and `IDocumentCloseParticipant`, keyed by `DocumentId` in the document service
and the close coordinator. The coordinator's check that a document changed while it was being
saved reads the tab's `IsDirty`, which is `Document.IsDirty`, after
`SaveForCloseAsync`: `Document.SaveAsync` records the history position its
capture saw, so an edit made during the write keeps the document dirty.

Save by asset finds the document in `OpenDocuments`, then calls its editor's
`SaveAsync`, so the editor's prompts and input commit still run. Save all saves
the dirty documents of the area.

The `DocumentArea` constructor validates the descriptors against
`OpenDocuments.Kinds`: it fails when a kind has no descriptor, a descriptor
names a kind no document opens with, or a kind has two descriptors. The check is
part of the contribution registry's validation.

### 5.9 The material kind

Materials are documents of the material kind. The model is `Material` in
`Oxygen.Editor.Materials`, over the immutable `MaterialSource`.

- `MaterialDocumentKind` (Materials) opens `.omat.json` files: decode with
  `MaterialSourceReader`, then name the source from the file stem, as
  `Document.Title` does. A file that does not decode fails the open with the
  reader's message.
- `MaterialContent : IDocumentContent` holds the `Material` and the open
  gesture, if any. `HasEditsInProgress` is the open gesture; `FinishEditsAsync`
  commits it as one history entry. `Capture` validates against the engine schema
  (`MaterialValidation` over `MaterialSchemaValidator`; a refusal carries the
  validator's message) and serializes with `MaterialSourceWriter`; `CaptureCopy`
  renames the source to the copy's stem; `ApplyRelocation` maps texture
  references through `MapAsset`.
- `MaterialEdits` applies scalar, texture and property-pipeline edits to a
  material `Document`: validate the edit (`MaterialDescriptors`, the texture
  channel rule; a texture is an `AssetPath`) and the edited source (engine and
  merged schema), replace the source, and record one change in
  `Document.History` whose inverse restores the previous source. A rejected edit
  is published as an operation result and leaves the source and history
  unchanged. A gesture (`BeginEditSession`, previews, complete or cancel)
  previews without recording and commits as one entry. The gesture is a
  `MaterialGesture` held by the `MaterialContent`: the source before it began is
  its snapshot, and a material has at most one gesture. A new gesture, an edit
  or a capture commits the open one first. Undo and redo validate the restored
  source first, and a source captured before a relocation resolves its texture
  references through `IAssetRedirects`, with a warning for a texture deleted
  meanwhile. `MaterialEdits` is the material's only edit service.
- Dirty follows the history's position
  ([document-kinds.md](document-kinds.md#51-decisions), decision 2).
- The material's GUID is derived from `Document.File.Asset` where the editor
  shows it.
- The cook state the editor shows comes from the catalog status it observes
  (`AssetStatusText` over `IAssetFeed`) and `Document.IsDirty`. The Cook command
  calls `IMaterialCookService.CookMaterialAsync` with the location Projects
  resolves for `Document.File.Asset`.
- Presence, outside changes and relocations are the Shell's document service's,
  for every kind: it checks the file on the catalog's changes and follows the
  relocations `DocumentRelocations.Map` maps
  (`Oxygen.Editor.ContentPipeline.Relocation`).
- New Material goes through ContentPipeline's asset authoring with
  `MaterialTemplates.Asset`, whose content is `MaterialTemplates.New(name)`.
  The Content Explorer's `AuthoredAssetCreation` offers "New Material" beside
  "New Scene", then opens it through the Shell's placement.

File versions, save against the version read, conflicts, Keep Mine, quiet
reload, follow a relocation, save a copy and dirty come from `Document`; the
material has no document service of its own. Its rebinding on
`ContentReplaced`, and its `Activate`/`Deactivate` when its view leaves the
document area, stay in the editor.

### 5.10 The cook's view of open documents

The cook knows open documents only through `IUnsavedDocuments`
([editor-documents.md](editor-documents.md#533-asking-before-play-and-cook)):
given file paths, the open documents that edit them with unsaved changes. A
document has unsaved changes when `Document.IsDirty` or
`Document.Content.HasEditsInProgress`, so a document in the middle of a drag
counts. The cook keeps no registration, read lease or revision counter per
document, and `Document` has no counters.

### 5.11 The Cooked Inspector panel

Cooked inspection is a panel, not a document. The workspace has one
`CookedInspector` panel, routed and docked like Cooking and Problems. An
inspection request (`OpenCookedInspectionRequestMessage`, from the Content
Explorer and from a cook run) sets the panel's scope (`CookedInspectionScope`:
project, folder or asset, the cooked source, validate) and reveals the panel; a
request for the scope it already shows inspects it again. With no scope the
panel says how to start an inspection. The panel keeps its sections, search,
Validate, Cancel and show-asset link; it is not a close participant, and it
cancels a running inspection when the workspace disposes it.

### 5.12 One scene per window

The rule is a placement rule of the scene editor
([document-kinds.md](document-kinds.md#51-decisions), decision 9): its
descriptor allows one document per area (`IsOnePerArea`), and the Shell's
document service applies it to any kind that asks for it. Opening a scene asks
about the open scene's unsaved edits (`PrepareCloseAsync`), reads the incoming
scene before the current one closes so a file that does not open keeps the
current scene, then commits the close and shows the new scene; a newer request
supersedes an older one that has not shown its scene. What only scenes need
stays with the scene editor (`SceneDocumentLifetime`): the project remembers
its last scene, and a scene that failed to replace the one it closed leaves the
window without a scene, from which the previous one opens again. Closability
comes from the descriptor (5.8), not from a document type string.

## 6. Ownership

| Type                                                                                                                          | Project                         |
| ----------------------------------------------------------------------------------------------------------------------------- | ------------------------------- |
| `DocumentId`, `IDocumentMetadata`, `IDocumentTabSource`                                                                       | `DroidNet.Documents`            |
| `Document`, `OpenDocuments`, kind and content contracts, `IUnsavedDocuments`                                                  | `Oxygen.Editor.Documents`       |
| `DocumentTab`, `IDocumentArea`, `DocumentEditorDescriptor`, the document service, input committer, prompts, close coordinator | `Oxygen.Editor.Shell`           |
| `MaterialDocumentKind`, `MaterialContent`, `MaterialEdits`, `MaterialTemplates`                                               | `Oxygen.Editor.Materials`       |
| `DocumentRelocations`                                                                                                         | `Oxygen.Editor.ContentPipeline` |
| Cooked Inspector panel                                                                                                        | `Oxygen.Editor.CookedInspector` |

## 7. Data Contracts

- `DocumentId(Guid Value)` (5.3).
- `IDocumentMetadata` (5.4) and `DocumentTab` (5.5).
- `IDocumentArea` (5.7).
- `DocumentEditorDescriptor(IDocumentKind kind, Func<DocumentTab, object> createEditor)`
  with `IsClosable`, `IconUri` and `CreateEditor(DocumentTab)` (5.8).
- `CookedInspectionScope` (5.11).

## 8. Commands, Services, Or Adapters

- `OpenDocuments` lookups (5.6).
- `IDocumentArea` per window (5.7).
- `MaterialEdits` (5.9).
- `DocumentRelocations.Map` turns a relocation change into a
  `DocumentRelocation` for one document (5.9).
- `IUnsavedDocuments` (5.10).

## 9. UI Surfaces

- Document tabs, which read `DocumentTab` (5.5).
- The Cooked Inspector panel (5.11).
- "New Material" in the Content Explorer (5.9).

## 10. Persistence And Round Trip

`DocumentId` is never persisted. Persisted view state is keyed by content
identity: the viewport layout and cameras by `SceneId`, the last opened scene by
its `AssetPath` (5.3).

## 11. Live Sync / Cook / Runtime Behavior

A document's viewports are owned by its `DocumentId` as a `SurfaceOwnerId` and
released when it closes (5.3). The cook sees open documents only through
`IUnsavedDocuments` (5.10).

## 12. Operation Results And Diagnostics

A rejected material edit is published as an operation result and changes nothing
(5.9). A descriptor set that does not match the kinds fails when the window
scope is built (5.8). A refused detach leaves the tab in place (5.4).

## 13. Dependency Rules

All edges point down:

- `Oxygen.Editor.Shell` references `Oxygen.Editor.Documents`,
  `DroidNet.Documents` and, for its windows, dialogs and docking, Aura and
  Docking; it references foundation modules only.
- `Oxygen.Editor.Documents` references `DroidNet.Documents` for `DocumentId`
  alone.
- `Oxygen.Editor.ContentPipeline` references `Oxygen.Editor.Documents`.
- The Material Editor references `Oxygen.Editor.Shell` for `IDocumentArea`.
- Nothing below them references them, so there is no cycle.

## 14. Validation Gates

- Aura: the presenter keys tabs by `DocumentId`, updates a tab from
  `PropertyChanged` raised off the UI thread, stops listening to a closed or
  detached tab and on dispose, and leaves a tab in place when detach is refused.
- Documents: `Find(DocumentId)`, `FindByAsset` by `AssetPath`.
- Shell: `DocumentTab` follows edits, undo to the saved point, missing, rename
  and reload, and unsubscribes on dispose; `IDocumentArea` follows selection,
  open and close, raises `ActiveDocumentChanged` once per change, shows a
  document whose tab is in another window there, prepares the close of a set of
  documents, and closes the document in `OpenDocuments` after its participants;
  the close coordinator refuses to close a document edited while it was being
  saved.
- Material Editor: the kind decodes and refuses a bad file; scalar, texture and
  property edits, gestures, undo and redo through `Document.History`; a rejected
  edit leaves the source and history unchanged; capture refuses a schema
  failure; copy renames; relocation maps textures and redirects old history
  entries; the editor follows its `Document` through reload and relocation;
  dirty follows the position.
- ContentPipeline: `MaterialTemplates.Asset` creates a file the kind opens;
  `IUnsavedDocuments` reports a document dirty during a gesture.
- World Editor: the explorer, inspector and demand follow `IDocumentArea`; the
  host creates editors from descriptors, saves by asset and fails the scope
  check on a missing descriptor; the Cooked Inspector panel takes a scope,
  inspects again on a repeated request and starts empty.

## 15. Open Issues

None.
