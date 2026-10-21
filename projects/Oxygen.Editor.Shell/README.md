# Oxygen.Editor.Shell

The editor's application frame: the services every editor part shares. Features
plug into it and never reference Aura windowing, Docking, window routing or a
`WindowId` themselves. The Shell references foundation modules only.

Design: [editor-shell.md](../../design/editor/review/editor-shell.md),
[document-identity.md](../../design/editor/lld/document-identity.md) and
[editor-documents.md](../../design/editor/lld/editor-documents.md).

## Layout

| Folder          | Holds                                                                                  |
| --------------- | -------------------------------------------------------------------------------------- |
| `Contributions` | Feature modules, panel descriptors, editor providers and the registry that checks them |
| `Documents`     | The document service, each window's document area and tabs, closing, prompts, banners  |
| `Commands`      | Command descriptors, the window's command service and its key bindings                 |
| `Windows`       | Placement of documents and panels                                                      |
| `Layout`        | The docking workspace: its routed layout, dock views and the Docking placement adapter |
| `Desktop`       | Clipboard, File Explorer, Recycle Bin and the external code editor                     |
| `Scopes`        | The window and panel layers of the editor's scope chain                                |

## Documents

`Oxygen.Editor.Documents` says what a document is; the Shell decides where it
shows, how it closes and how it follows its file.

- `IDocumentService` opens an asset's file once and shows its document in the
  window's area. A kind whose descriptor allows one document per area (scenes)
  replaces the shown one: its unsaved changes are asked about first, and a file
  that does not open keeps it. The service creates each document's editor from
  its kind's `DocumentEditorDescriptor`, saves documents through their editors,
  and reloads, keeps or copies a document whose file changed on disk.
- It keeps open documents in step with their files: a change the catalog reports
  makes the document check its file (a clean document reloads, an edited one is
  marked changed on disk, a deleted one missing); a relocation in the editor
  moves or rewrites the documents it touches, and closes those whose files it
  deleted. The project's side comes through two ports of
  `Oxygen.Editor.Documents`, `IDocumentFiles` and `IDocumentRelocations`.
- `IDocumentArea` is the document area of the window a part runs in: the
  documents with a tab there and the active one. `DocumentTab` is the tab of one
  `Document`.
- `DocumentAreaView` shows the active document's editor under two banners: the
  file is missing on disk (Save, Close), or it changed on disk while the
  document has unsaved changes (Reload, Keep Mine, Save a Copy).
- `IHistoryService` holds the session's global history; each document has its
  own.

### Tabs

`DocumentTabs` implements Aura's `IDocumentTabSource` over the tabs of every
window. A tab is an `IDocumentMetadata`; in the editor it is the `DocumentTab`
of one `Document`, which raises `PropertyChanged` when the document changes, so
nothing pushes tab state to the tabs.

- A document has at most one tab: opening or attaching a tab for a document that
  has one, in any window, is refused. `FindWindow` says which window has it.
- Every detach is refused until the Shell has document windows to receive a
  torn-out tab; Aura leaves the tab in place.
- Features do not use the tabs. They reach their window's documents through
  `IDocumentArea`, which is built over them.

### Closing

A close is a transaction: it is prepared, then committed or dropped.

- `PrepareCloseDocumentsAsync` prepares closing some tabs of a window,
  `PrepareCloseAllAsync` a window's close, and `PrepareCloseAllWindowsAsync` the
  replacement of the project. `WindowCloseCoordinator` prepares the window's
  documents when Aura closes the window.
- Preparing raises `DocumentClosing`, whose handlers may veto, then hands the
  tabs to the window's `DocumentCloseCoordinator`, which the window's document
  service registers with the tabs. Each editor takes part in the close as an
  `IDocumentCloseParticipant`.
- The coordinator asks the window's `IDocumentClosePrompt`: _Save changes to 2
  documents?_ with Save, Don't Save and Cancel, each document listed with its
  asset and, when there are several, a check box. A document whose file changed
  on disk offers Reload and Save a Copy in the prompt. When a document is still
  dirty after a save that succeeded, it changed while saving, and the close is
  refused with a published failure.
- Committing closes each participant, then the tab. The area then closes the
  document in `OpenDocuments`; editors never do.

### Events

| Event               | When raised                                      |
| ------------------- | ------------------------------------------------ |
| `DocumentOpened`    | A tab was added                                  |
| `DocumentClosing`   | Before a close that is not forced; handlers veto |
| `DocumentClosed`    | A tab was removed                                |
| `DocumentActivated` | A tab was selected                               |
| `DocumentAttached`  | A tab was attached to a window                   |
| `DocumentDetached`  | Never: every detach is refused                   |

## Commands

The window's frame binds the key gestures of each `CommandDescriptor`: Undo,
Redo, Cut, Copy and Paste. A command goes to the first part around the control
the user last pressed or focused whose view model is an `ICommandHandler` that
runs it, then to the active document's editor. When that control has left the
window, such as a list's row, the views around it still name the part. Undo and Redo that no part runs go to the active
document's history, or to the global history when no document is active. A
control that handles a key itself keeps it, text entry keeps its own Undo, and a
menu or flyout with the focus keeps the keys while it is open.

## Placement and contributions

`IPlacementService` opens documents and shows panels for parts that never name a
window; its first implementation is the docking workspace's. Panels are named by
their `PanelDescriptor`, whose title the panel's tab shows; editors come from
each module's `IDocumentEditorProvider`. `ContributionRegistry` fails when two
panels or two commands share an identity.

## Desktop

`IDesktopIntegration` copies text, shows files in File Explorer and moves them
to the Recycle Bin. `ICodeEditorService` opens scripts and input files in the
user's code editor, the choice being a per-user setting; it opens the project's
folder, which it reads through `IProjectFolder`.
