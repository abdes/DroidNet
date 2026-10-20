# Oxygen.Editor.Shell

The editor's application frame: the services every editor part shares. Features
plug into it and never reference Aura windowing, Docking, window routing or a
`WindowId` themselves. The Shell references foundation modules only.

Design: [editor-shell.md](../../design/editor/review/editor-shell.md),
[document-identity.md](../../design/editor/lld/document-identity.md) and
[editor-documents.md](../../design/editor/lld/editor-documents.md).

## Documents

`Oxygen.Editor.Documents` says what a document is; the Shell decides where it
shows and how it closes.

- `IDocumentArea` is the document area of the window a part runs in: the
  documents with a tab there and the active one. `DocumentTab` is the tab of one
  `Document`, and `DocumentEditorDescriptor` creates the editor of a kind.
- `IHistoryService` holds the session's global history; each document has its
  own.

### Tabs

`EditorDocumentService` implements Aura's `IDocumentTabSource` over the tabs of
every window. A tab is an `IDocumentMetadata`; in the editor it is the
`DocumentTab`, a view of one `Document` that raises `PropertyChanged` when the
document changes, so nothing pushes tab state to the service.

- A document has at most one tab: opening or attaching a tab for a document that
  has one, in any window, is refused. `FindWindow` says which window has it.
- Every detach is refused until the Shell has document windows to receive a
  torn-out tab; Aura leaves the tab in place.
- Features do not use the service. They reach their window's documents through
  `IDocumentArea`, which is built over it.

### Closing

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
- Committing closes each participant, then the tab. The area then closes
  the document in `OpenDocuments`; editors never do.

`IDocumentConflictPrompt` and `IDocumentConflictParticipant` offer reload or
save a copy when a save meets a newer file. The prompts, `DocumentCloseItem` and
`IDocumentInputCommitter` still take the window's `WindowId` and the tab as
`IDocumentMetadata`.

### Events

| Event               | When raised                                      |
| ------------------- | ------------------------------------------------ |
| `DocumentOpened`    | A tab was added                                  |
| `DocumentClosing`   | Before a close that is not forced; handlers veto |
| `DocumentClosed`    | A tab was removed                                |
| `DocumentActivated` | A tab was selected                               |
| `DocumentAttached`  | A tab was attached to a window                   |
| `DocumentDetached`  | Never: every detach is refused                   |
