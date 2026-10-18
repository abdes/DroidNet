# Document identity

Status: **agreed design**

Scope: how an open document is identified, shown as a tab, found, and reached by
the parts that work on it. It sits between
[document-kinds.md](document-kinds.md) (what a document is, without windows) and
[editor-shell.md](editor-shell.md) (the frame that places documents). It brings
forward the document part of plan slice S1.10 and the material kind of S1.8 of
[issue-069-di-topology-and-decomposition.md](../plan/issue-069-di-topology-and-decomposition.md),
and runs before task 3 of [scene-edits.md](scene-edits.md).

## 1. Scenarios

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
7. **One scene per window.** Opening a second scene retires the first, as today.
8. **Drag a tab out of the strip.** Until the Shell has document windows
   (S1.10), the tab snaps back; the document never loses its tab.

## 2. What happens today

Measured on the tree at `aa11fde89`.

| Concern              | Today                                                                                                                                                                                                                                                                                                                                               |
| -------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Identity             | Two ids for one open document: Aura's tab key `IDocumentMetadata.DocumentId` (`Guid`) and `Document.Id` (`DocumentId`, with a JSON converter nothing uses). A scene tab reuses `document.Id.Value` by convention (`SceneDocumentMetadata.For`); a material tab and the cooked inspection make a fresh `Guid`.                                       |
| Bridges              | `SceneDocumentTabs.Active`, `DocumentHostViewModel.FindTab(Uri)` (a switch over metadata types), `SceneDocumentMetadata.SceneDocumentId`, `DocumentManager.FilePathOf` (another switch), material lookup by URI string ignoring case.                                                                                                               |
| Tab state            | A mutable copy. `SceneDocumentMetadata.Refresh()` copies title, dirty and missing from the `Document`; `SceneEditorViewModel`, `MaterialEditorViewModel` and `DocumentManager.Relocation` push it with `UpdateMetadataAsync`. The cook registrar reads the tab's change counters. `SceneDocumentMetadata.Layout` is set by nothing.                 |
| Active document      | The Scene Explorer, Inspector, content demand and the scene editor ask `IDocumentService` with a `WindowId`, then cast the metadata. The Cooking panel and Play save through `WorkspaceViewModel` and `DocumentHostViewModel`, which find tabs by metadata type.                                                                                    |
| Editors              | `DocumentHostViewModel.CreateEditor` switches over three metadata types; each factory resolves its editor's dependencies from the container. The host handles `DocumentOpened`, `DocumentClosed` and `DocumentActivated` only.                                                                                                                      |
| Tear-out             | Dragging a tab out of the strip raises `TabDetachRequested`; the presenter calls `DetachDocumentAsync`, the tab goes and `DocumentDetached` is raised. Nothing in the editor handles `DocumentDetached` or `DocumentAttached`, and nothing handles `TabTearOutRequested`: the editor stays in the host without a tab and the `Document` stays open. |
| Scopes               | `EditorDocumentService` and `DocumentCloseCoordinator` are process singletons keyed by `WindowId`. `OpenDocuments`, `DocumentManager` and `DocumentHostViewModel` live in the workspace window's container.                                                                                                                                         |
| Threads              | `Document` runs every operation on the caller's context and raises `StateChanged` there; the editors dispatch catalog and relocation callbacks to their window's dispatcher first. The presenter marshals every service event through its `DispatcherQueue`.                                                                                        |
| Close                | The close coordinator keys participants by `(WindowId, Guid)`, and its "changed while saving" check compares `BaseDocumentMetadata.ChangeVersion`. The scene editor's `CloseAsync` closes the `Document` in `OpenDocuments` itself. The prompts and the input committer take a `WindowId` and `IDocumentMetadata`.                                  |
| Material             | Not a `Document`. `MaterialDocumentService` keeps its own documents, ids, histories, file versions, save gates, conflicts, presence, revision counters and cook registrations. New Material writes a default source through the service, closes it and opens the file.                                                                              |
| Cooked inspection    | A tab with no file, keyed by project, scope, asset and cooked root, requested from the Content Explorer and from a cook run.                                                                                                                                                                                                                        |
| Cook inputs          | `CookDocumentState` carries `Revision` and `SavedRevision`. The only reader is `MaterialDocumentService.CookAsync`; the pipeline reads `IsDirty` and `SavedContentHash`.                                                                                                                                                                            |
| One scene per window | Twice: `DocumentManager.RetireOpenScenesAsync`, and `EditorDocumentService.EnsureSingleNonClosableDocumentAsync`, which matches `BaseDocumentMetadata.DocumentType` strings.                                                                                                                                                                        |

## 3. Design

### 3.1 One identity

`DocumentId` identifies an open document in the editor. A document has at most
one tab in the editor, and that tab's key is the document's id. There is no tab
id, no conversion and no lookup from one to the other.

`DocumentId` moves from `Oxygen.Editor.Documents` to `DroidNet.Documents`, the
library that holds Aura's document contract, so Aura and the editor share the
type. It keeps `Value`, `Empty`, `IsEmpty`, `New`, `FromValue` and `ToString`.
It loses `IGuidId<DocumentId>` and its JSON converter: both come from
`Oxygen.Managed.Core`, which a DroidNet library does not reference, and nothing
serializes it.

Two boundaries keep `Guid`, each converting in one place:

- Aura's `TabItem.ContentId`, a property of the generic tab strip control that
  drag and tear-out compare. `DocumentTabPresenter` sets it from the document's
  id and reads it back.
- The Runtime's surface ownership (`ReleaseDocumentSurfacesAsync`,
  `ViewportSurfaceKey.DocumentId`), as task 2 of scene-edits.md decided.
  `AffectedScope.DocumentId` stays the `Guid` it is.

Editor state that lives while a document is open (selection, engine sync,
viewports, history, cook registration) is keyed by `DocumentId`. A `DocumentId`
is new each time a file opens, so state persisted across sessions (the viewport
layout and cameras, the last opened scene) is keyed by the content's identity
(`SceneId`, the asset URI), and in-session state never is.

### 3.2 Aura's contract

`IDocumentMetadata` becomes what a tab shows, read-only and observable:

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

`IDocumentService` and `IDocumentServiceState` take and return `DocumentId`
everywhere, including `DocumentActivatedEventArgs` and `GetActiveDocumentId`.
`UpdateMetadataAsync` and `DocumentMetadataChanged` go: the presenter subscribes
to each open tab's `PropertyChanged` when it adds the tab, marshals the change
through its `DispatcherQueue` (a tab may raise on the thread that owns its
document), and unsubscribes when the tab closes or detaches and when the
presenter is disposed. `OpenDocumentAsync` returns `bool`, since the id is the
metadata's. `DetachDocumentAsync` returning `null` means the service refused: no
event is raised and the tab stays. The rename to `IDocumentTabSource` stays in
S1.10.

Aura's `MultiWindow` sample and the presenter's tests implement the new
contract.

### 3.3 The tab is a view of the document

`DocumentTab : IDocumentMetadata`, in `Oxygen.Editor.Shell`, wraps one
`Document`:

- `DocumentId` is `Document.Id`; `Title` is `Document.Title`; `IsDirty` is
  `Document.IsDirty` (missing on disk counts, as document-kinds 3.1 says).
- It raises `PropertyChanged` from `Document.StateChanged` and
  `ContentReplaced`, on the thread that raised them: the UI thread of the window
  that opened the document, where every `Document` operation runs.
- `IsClosable` and `IconUri` come from the kind's editor descriptor (3.6).
- It exposes `Document`, so the Shell reaches the model from a tab.
- The document area creates it in `ShowAsync` and disposes it when the tab
  closes; disposing unsubscribes from the document.

`BaseDocumentMetadata`, `SceneDocumentMetadata`, `MaterialDocumentMetadata` and
`CookedInspectionDocumentMetadata` go. What they carried moves to its owner:

- `MaterialUri` is `Document.File.AssetUri`.
- `SceneDocumentMetadata.Layout` goes with nothing to move: the viewport layout
  is in `ViewportStateService`, with the cameras, keyed by `SceneId`.
- `IsMissingOnDisk` and the change counters are the `Document`'s.
- The messages that carry a `SceneDocumentMetadata`
  (`InstallSceneRequestMessage`, `SceneAuthoringLoadedMessage`,
  `SceneReloadedMessage`) carry the `Document`, and the parts that compare
  metadata by reference compare `Document.Id`.

### 3.4 One registry

`OpenDocuments` is the only way to find an open document. It gains
`Find(DocumentId)` and `FindByAsset(Uri)` beside `Find(path)`. `FindByAsset`
compares `Document.File.AssetUri` by `Uri` equality; both sides are in the
catalog's form, because Projects writes every `DocumentFile` (document-kinds
3.4).

There is one `OpenDocuments` per project session, so a file opens once across
the session's windows. Today the workspace window's container is the only
session-scoped container, so it stays registered there until S1.10 introduces
the session scope of editor-shell.md 2.1.

`SceneDocumentTabs`, `FindTab`, `FilePathOf`, `SceneDocumentId` and the material
URI comparison go.

### 3.5 The document area

`IDocumentArea`, in `Oxygen.Editor.Shell`, is the document area of the window a
part runs in. It is registered in the workspace window's scope, so a part gets
the area of its own window without knowing a `WindowId`.

| Member                        | Meaning                                                                                                                  |
| ----------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `Documents`                   | The documents with a tab in this area                                                                                    |
| `ActiveDocument`              | The document whose tab is selected, or none                                                                              |
| `ActiveDocumentChanged`       | Raised after the selection changes, on the UI thread                                                                     |
| `ShowAsync(document)`         | Selects the document's tab and brings its window forward; adds the tab here first when the document has none             |
| `PrepareCloseAsync(docs)`     | The close transaction for the given documents of this area, without the workspace close's prompt                         |
| `CloseAsync(document, force)` | Prepares and commits the close of one document: its participants' `CloseAsync`, then the tab, then `OpenDocuments.Close` |

A document's tab is in at most one area. `ShowAsync` for a document whose tab is
in another window selects it there and brings that window forward. Moving a tab
between areas is Aura's detach and attach: a document window to receive a
torn-out tab is a window kind of S1.10 (editor-shell.md 2.2), so until then
`EditorDocumentService`, the tab service the area is built over, refuses every
detach and the tab snaps back (scenario 8). The workspace close keeps the
service's `PrepareCloseAllAsync`, whose prompt lists each document's asset. A
`Document` closes in `OpenDocuments` only through `CloseAsync`; editors no
longer call `OpenDocuments.Close`. The editor's `IDocumentCloseParticipant`
keeps its engine and view teardown.

It is implemented over `IEditorDocumentService`, the window's id and the window
manager's activation, which the composition root gives it. Features never see
any of them:

- The Scene Explorer, Inspector and content demand follow `ActiveDocument` when
  its content is a `SceneContent`.
- The scene editor asks whether its document is active by comparing
  `ActiveDocument` with its own.
- The Cooking panel and Play find documents through `OpenDocuments` and save
  them through the document host's save (3.6).

The `WindowId` that `IDocumentInputCommitter`, `IDocumentClosePrompt` and
`IDocumentConflictPrompt` take, and the window-scoped messages of the scene
editor (cameras, shortcuts, reload), stay until S1.10 moves them into the
Shell's part services (document-kinds decision 4). After this slice they are the
only `WindowId` an editor holds. The prompts, `DocumentCloseItem` and the close
coordinator take the tab as `IDocumentMetadata`: they live in
`Oxygen.Editor.Documents`, which cannot see the Shell, and the coordinator
receives its tabs from the tab service in that form. An editor passes its
`DocumentTab`; the close prompt shows a document's asset when the tab is a
`DocumentTab`. S1.10 moves them into the Shell, where they take the
`DocumentTab`.

`IDocumentArea` is the first part of the Shell's context service of
editor-shell.md section 4; the active part and the active window come with
S1.10.

### 3.6 Editors per kind

A kind's module registers a `DocumentEditorDescriptor` beside its
`IDocumentKind`:

- the kind it edits;
- whether its tab is closable (the scene's is not) and its icon;
- a factory that creates the editor for a document's `DocumentTab`, from the
  window scope. The tab gives the editor its `Document`, and it is the one
  `IDocumentMetadata` of the document, which the editor passes to the window's
  prompts (3.5); an editor never builds or looks up a tab.

This is the `DocumentEditorDescriptor` of editor-shell.md section 3, keyed by
the kind instead of a metadata type. `DocumentHostViewModel` picks the
descriptor by `Document.Kind`. Its switch, its three `Create*Editor` methods and
its metadata-type checks go. The editor contract stays what it is today
(`IAsyncSaveable`, `IDocumentCloseParticipant`, `IDocumentConflictParticipant`),
keyed by `DocumentId` in the host and the close coordinator. The coordinator's
check that a document changed while it was being saved reads the tab's
`IsDirty`, which is `Document.IsDirty`, after `SaveForCloseAsync`:
`Document.SaveAsync` records the history position its capture saw, so an edit
made during the write keeps the document dirty. `BaseDocumentMetadata` and its
change counters go.

The host's save by asset becomes: find the document in `OpenDocuments`, then
call its editor's `SaveAsync`, so the editor's prompts and input commit still
run. Save all saves the dirty documents of the area.

A check when the window scope is built fails when an `OpenDocuments` kind has no
descriptor or a descriptor names an unregistered kind. It joins the contribution
registry's validation in S1.10.

### 3.7 The material kind

Materials become documents of the material kind, the second kind after scenes.
The model is `Material` in `Oxygen.Editor.Materials`, over the immutable
`MaterialSource`.

- `MaterialDocumentKind` (Materials) opens `.omat.json` files: decode with
  `MaterialSourceReader`, then name the source from the file stem, as
  `Document.Title` does. A file that does not decode fails the open with the
  reader's message.
- `MaterialContent : IDocumentContent` holds the `Material` and
  the open gesture, if any. `HasEditsInProgress` is the open gesture;
  `FinishEditsAsync` commits it as one history entry. `Capture` validates
  against the engine schema (`MaterialSchemaValidator`; a refusal carries the
  validator's message) and serializes with `MaterialSourceWriter`; `CaptureCopy`
  renames the source to the copy's stem; `ApplyRelocation` maps texture
  references through `MapReference`.
- `MaterialEdits` applies scalar, texture and property-pipeline edits to a
  material `Document`: validate the edit (`MaterialDescriptors`, the texture
  channel and virtual path rules) and the edited source (engine and merged
  schema), replace the source, and record one change in `Document.History` whose
  inverse restores the previous source. A rejected edit is published as an
  operation result, as today. A gesture (`BeginEditSession`, previews, complete
  or cancel) previews without recording and commits as one entry. The gesture is
  a `MaterialGesture` held by the `MaterialContent`: the source before it began
  is its snapshot, and a material has at most one gesture, so the keyed sessions
  and property snapshots of `CommitGroupController` would only be converted back
  to a `MaterialSource`. A new gesture, an edit or a capture commits the open
  one first. Undo and redo validate the restored source first, and a source
  captured before a relocation resolves its texture references through
  `IAssetRedirects`, with today's warning for a texture that was deleted
  meanwhile. `IMaterialPropertyEditService` folds into it.
- Dirty follows the history's position (document-kinds decision 2), no longer a
  content comparison.
- The material's `MaterialGuid` is derived from `Document.File.AssetUri` where
  the editor shows it.
- The cook state the editor shows comes from the catalog status it already
  observes (`AssetStatusText` over `IAssetFeed`) and `Document.IsDirty`;
  `GetMaterialCookStateAsync` loses its caller. The Cook command calls
  `IMaterialCookService.CookMaterialAsync` directly, with the location Projects
  resolves for `Document.File.AssetUri`.
- Presence and outside changes: the editor subscribes to `IAssetCatalog.Changes`
  for its file and calls `Document.CheckFileAsync`, as the scene editor does.
- Relocation: the editor's relocation participant maps the change with
  `DocumentRelocations.Map` and calls `Document.FollowRelocationAsync`, as the
  scene editor does. `DocumentRelocations` moves from the World Editor to
  `Oxygen.Editor.ContentPipeline.Relocation`, where both editors reach it.
- New Material goes through ContentPipeline's asset authoring:
  `AuthoredAssetKind.Material` and `AssetTemplates.Material(name)`, the JSON
  `MaterialSourceWriter` writes for today's default source. The Content
  Explorer's `AuthoredAssetCreation` gains "New Material" beside "New Scene" and
  then sends `OpenMaterialRequestMessage`. `CreateMaterialRequestMessage` goes.

What `MaterialDocumentService` did that every document now does (file versions,
save against the version read, conflicts, Keep Mine, quiet reload, follow a
relocation, save a copy, dirty, the read lease, the cook registration) comes
from `Document`. `MaterialDocumentService`, `IMaterialDocumentService`,
`MaterialDocument`, `MaterialDocumentMetadata`, `IMaterialSourcePathResolver`,
`ProjectMaterialSourcePathResolver` and the per-document dictionaries go. The
editor's conflict handling (`IDocumentConflictParticipant` over
`Document.ReloadAsync` and `Document.SaveCopyAsync`), its rebinding on
`ContentReplaced`, and its `Activate`/`Deactivate` for the host stay in the
editor.

### 3.8 Cook inputs for every document

The cook's view of open documents (`ICookDocumentRegistry`) is fed by one
registrar for every open `Document`, in ContentPipeline, which now references
`Oxygen.Editor.Documents` (S1.11 already needs that reference for
`IUnsavedDocuments`). It subscribes to `OpenDocuments.Opened` and `Closed`,
registers each document's file, follows the file on relocation, and captures the
state from the document:

- dirty: `Document.IsDirty` or `Document.Content.HasEditsInProgress`;
- the version's hash.

`CookDocumentState.Revision` and `SavedRevision` go: after this slice nothing
reads them, and S1.11 removes the registry. `Document` gains no counters.

The read lease is `Document.AcquireReadLease()`, acquired on the document's
thread: the registrar captures the `SynchronizationContext` of the open and
posts the acquire through it, as `Document` already does for the content it
disposes. `SceneCookInputRegistrar` and the material's own registration go.
S1.11 removes the registrar with the rest of the cook's document view.

### 3.9 The Cooked Inspector panel

Cooked inspection is a panel, not a document (this replaces decision 5 of
document-kinds.md). The workspace has one `CookedInspectionViewModel` panel,
routed and docked with the Content Explorer like Cooking and Problems. An
inspection request (`OpenCookedInspectionRequestMessage`, from the Content
Explorer and from a cook run) sets the panel's scope (project, scope, asset,
cooked root, validate) and reveals the panel; a request for the scope it already
shows refreshes it. With no scope the panel says how to start an inspection. The
panel keeps its sections, search, Validate, Cancel and show-asset link; its
`IDocumentCloseParticipant` goes, and it cancels a running inspection when the
workspace disposes it. The rename to `CookedInspector` stays in S1.12.

### 3.10 One scene per window

The rule stays where decision 9 of document-kinds.md puts it, in the scene
editor's placement (`DocumentManager.RetireOpenScenesAsync`).
`EnsureSingleNonClosableDocumentAsync` and the `DocumentType` strings go;
closability comes from the descriptor (3.6).

## 4. Decisions

1. **`DocumentId` lives in `DroidNet.Documents`** and types Aura's document
   contract. This replaces task 2's "Aura stays `Guid`".
2. **Every tab is a real `Document`.** Cooked inspection is a panel (3.9).
3. **At most one tab per document in the editor;** the tab's key is the
   document's id. Splits happen inside an editor. A tab is in at most one area;
   detach is refused until document windows exist (S1.10).
4. **Materials are documents now** (3.7), as the second kind.
5. **Tabs read the document;** nothing pushes tab state (3.2, 3.3).
6. **`OpenDocuments` is the only lookup** (3.4), one per project session.
7. **Parts reach their window's documents through `IDocumentArea`** and never
   pass a `WindowId` (3.5). The prompts and the input committer keep theirs
   until S1.10.
8. **An editor descriptor per kind** replaces the host's switches (3.6).
9. **In-session editor state is keyed by `DocumentId`;** persisted view state by
   content identity (3.1).
10. **A document closes in `OpenDocuments` through the area's close** (3.5),
    never from an editor.
11. **No revision counters.** `CookDocumentState` loses `Revision` and
    `SavedRevision` (3.8).

## 5. Placement

| Type                                                       | Project                                     |
| ---------------------------------------------------------- | ------------------------------------------- |
| `DocumentId`, `IDocumentMetadata`, `IDocumentService`      | `DroidNet.Documents`                        |
| `Document`, `OpenDocuments`, kinds and contents            | `Oxygen.Editor.Documents`                   |
| `DocumentTab`, `IDocumentArea`, `DocumentEditorDescriptor` | `Oxygen.Editor.Shell`                       |
| `EditorDocumentService`, close coordinator, prompts        | `Oxygen.Editor.Documents` until S1.10       |
| `MaterialDocumentKind`, `MaterialContent`, `MaterialEdits` | `Oxygen.Editor.Materials`                   |
| `DocumentRelocations`, `AssetTemplates.Material`           | `Oxygen.Editor.ContentPipeline`             |
| Open-document cook registrar                               | `Oxygen.Editor.ContentPipeline` until S1.11 |
| Cooked Inspector panel                                     | `Oxygen.Editor.WorldEditor` until S1.12     |

References, all new edges pointing down: `Oxygen.Editor.Shell` gains
`Oxygen.Editor.Documents` and `DroidNet.Documents`;
`Oxygen.Editor.ContentPipeline` gains `Oxygen.Editor.Documents`; the Material
Editor gains `Oxygen.Editor.Shell` for `IDocumentArea` (the Scene Explorer,
Inspector and scene editor already reach it through the World Editor). Nothing
below them references them, so there is no cycle. `Oxygen.Editor.Documents`
keeps its reference to `DroidNet.Documents` after S1.10 for `DocumentId` alone.
The Shell does not reference Aura: `IDocumentService` and `WindowId` come from
`DroidNet.Documents` and the Windows App SDK.

## 6. Order of work

Each step builds and passes its tests; the World Editor test projects (register
item D4 of scenes-module.md) are migrated in the same pass instead of later.

1. **Identity:** `DocumentId` moves; Aura's contract is typed and observable;
   `EditorDocumentService`, the presenter, Aura's `MultiWindow` sample and tests
   follow.
2. **Tabs, registry and area:** `DocumentTab`, the `OpenDocuments` lookups,
   `IDocumentArea` with its refused detach and its close; the metadata types,
   bridges, `UpdateMetadataAsync` callers and the scene editor's
   `OpenDocuments.Close` go; the messages carry the `Document`; editors pass
   their `DocumentTab` to the prompts; the parts move to `IDocumentArea`.
3. **Cook inputs:** the registrar in ContentPipeline; the counters go;
   `SceneCookInputRegistrar` goes; `DocumentRelocations` moves.
4. **Editors and the material kind:** descriptors and the window-scope check;
   the material kind, content and edits; the Material Editor over a `Document`;
   New Material through asset authoring; `MaterialDocumentService` goes.
5. **Cooked Inspector panel.**

Then task 3 of scene-edits.md.

## 7. Tests

- Aura: the presenter keys tabs by `DocumentId`, updates a tab from
  `PropertyChanged` raised off the UI thread, stops listening to a closed or
  detached tab and on dispose, and leaves a tab in place when detach is refused.
- Documents: `Find(DocumentId)`, `FindByAsset` with a URI in catalog form.
- Shell: `DocumentTab` follows edits, undo to the saved point, missing, rename
  and reload, and unsubscribes on dispose; `IDocumentArea` follows selection,
  open and close, raises `ActiveDocumentChanged` once per change, shows a
  document whose tab is in another window there, prepares the close of a set of
  documents, and closes the document in `OpenDocuments` after its participants.
- Documents: the tab service refuses detach, and the close coordinator refuses
  to close a document edited while it was being saved.
- Material Editor: the kind decodes and refuses a bad file; scalar, texture and
  property edits, gestures, undo and redo through `Document.History`; a rejected
  edit leaves the source and history unchanged; capture refuses a schema
  failure; copy renames; relocation maps textures and redirects old history
  entries; the editor follows its `Document` through reload and relocation;
  dirty follows the position.
- ContentPipeline: the registrar registers on open, follows a relocation,
  reports dirty during a gesture, acquires its lease on the document's thread,
  and releases on close; `AuthoredAssetKind.Material` creates a file the kind
  opens.
- World Editor: the explorer, inspector and demand follow `IDocumentArea`; the
  host creates editors from descriptors, saves by asset and fails the scope
  check on a missing descriptor; the Cooked Inspector panel takes a scope,
  refreshes on a repeated request and starts empty.

## 8. State

This design is implemented. The Aura, Documents, Shell, Material Editor,
ContentPipeline and World Editor test suites build and pass, and the editor was
checked by hand. The next work is task 3 of scene-edits.md section 9.
