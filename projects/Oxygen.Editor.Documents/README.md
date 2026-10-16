# Oxygen.Editor.Documents

What the editor edits, independent of the window that shows it: documents, the
files they read and write, and the kinds that decode them. Until the Shell's
document service replaces it, this module also holds the tab service the Aura
tabs run on and the close coordination with its prompts.

Design: [document-kinds.md](../../design/editor/review/document-kinds.md) and
[document-identity.md](../../design/editor/review/document-identity.md).

## Documents and document kinds

What a document is, independent of where it shows, is in `Document`,
`OpenDocuments` and the kind contract
([document-kinds.md](../../design/editor/review/document-kinds.md)). None of it
knows windows or tabs.

- A `Document` edits one file. It holds the `FileVersion` it read or last saved,
  and every save writes only over that version, through `IAtomicFileStore`, so
  an outside change surfaces as a conflict. It is dirty while its `History` is
  away from the position of its last save or load, so undoing back to that
  position makes it clean. When told its file changed (`CheckFileAsync`), it
  ignores its own save, reloads quietly when it has no edits, and otherwise
  marks itself changed on disk or missing. It also reloads, keeps its edits over
  an outside change (`KeepMineAsync`), saves a copy, and follows a relocation
  without a history entry.
- `OpenDocuments` opens each file once, with the `IDocumentKind` registered for
  its suffix (the longest suffix wins, and no two kinds share one).
- An `IDocumentKind` only decodes a file into its `IDocumentContent`. The
  content finishes its edits in progress, captures what a save writes (or
  refuses with a reason), and applies a relocation to its model. A kind never
  reads or writes a file and holds no version or history.

```csharp
var documents = new OpenDocuments([materialKind], atomicFileStore, loggerFactory);
var opened = await documents.OpenAsync(new DocumentFile(path, assetUri));
if (opened.Document is { } document)
{
    // Editors record their edits into document.History.
    var saved = await document.SaveAsync();
}
```

## Identity

A document has one id, its `Document.Id`, a `DocumentId` from
[DroidNet.Documents](../Documents/README.md). The tab that shows it is keyed by
the same id, and `OpenDocuments` finds a document by id (`Find(DocumentId)`), by
file (`Find(path)`) or by asset (`FindByAsset(Uri)`). Persisted view state is
keyed by what the document edits, never by `DocumentId`, which lives for one
session.

## Tabs

`EditorDocumentService` implements Aura's `IDocumentService` over the tabs of
every window. A tab is an `IDocumentMetadata`; in the editor it is the Shell's
`DocumentTab`, a view of one `Document` that raises `PropertyChanged` when the
document changes, so nothing pushes tab state to the service.

- A document has at most one tab: opening or attaching a tab for a document that
  has one, in any window, is refused. `FindWindow` says which window has it.
- Every detach is refused until the Shell has document windows to receive a
  torn-out tab; Aura leaves the tab in place.
- Features do not use the service. They reach their window's documents through
  the Shell's `IDocumentArea`, which is built over it.

## Closing

A close is a transaction: it is prepared, then committed or dropped.

- `PrepareCloseDocumentsAsync` prepares closing some tabs of a window,
  `PrepareCloseAllAsync` a window's workspace close, and
  `PrepareCloseAllWindowsAsync` the replacement of the project.
- Preparing raises `DocumentClosing`, whose handlers may veto, then hands the
  documents to the `DocumentCloseCoordinator`. Each editor registers an
  `IDocumentCloseParticipant` for its document with the coordinator, keyed by
  window and `DocumentId`.
- The coordinator asks `IDocumentClosePrompt` about unsaved documents. A
  document chosen for saving is saved with `SaveForCloseAsync`; when it is still
  dirty after a save that succeeded, it changed while saving, and the close is
  refused with a published failure.
- Committing closes each participant, then the tab. The Shell's area then closes
  the document in `OpenDocuments`; editors never do.

`IDocumentConflictPrompt` and `IDocumentConflictParticipant` offer reload or
save a copy when a save meets a newer file. The prompts, `DocumentCloseItem` and
`IDocumentInputCommitter` still take the window's `WindowId` and the tab as
`IDocumentMetadata`; they move into the Shell with the document service.

## Events

| Event               | When raised                                      |
| ------------------- | ------------------------------------------------ |
| `DocumentOpened`    | A tab was added                                  |
| `DocumentClosing`   | Before a close that is not forced; handlers veto |
| `DocumentClosed`    | A tab was removed                                |
| `DocumentActivated` | A tab was selected                               |
| `DocumentAttached`  | A tab was attached to a window                   |
| `DocumentDetached`  | Never: every detach is refused                   |

## Dependencies

- [DroidNet.Documents](../Documents/README.md): `DocumentId` and Aura's document
  contract.
- `DroidNet.Storage`: `IAtomicFileStore`, through which every file is read and
  written.
- `Microsoft.Extensions.Logging`.
