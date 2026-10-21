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
| `Windows`       | Placement of documents and panels, and the close of windows and the editor's exit      |
| `Layout`        | The docking workspace: its routed layout, dock views and the Docking placement adapter |
| `Desktop`       | Clipboard, File Explorer, Recycle Bin and the external code editor                     |

`ShellModule` registers the tabs of every window, the close of every window,
and the prompts, confirmations and input commits of documents in the editor
scope. Each takes the window it acts on with every call; an editor commits the
input of the window that shows its document.
`ShellSessionModule` registers the project session's document service, the
editors of its document kinds, its global history and its unsaved documents,
and creates the document service when the session opens.
`ShellWindowModule` registers each window's document area, the lending of
window services to editors, commands, placement and the external code editor.
The Shell knows no project: the composition root opens the session.
`DockingWindowModule` registers the window's docking.

## Documents

`Oxygen.Editor.Documents` says what a document is; the Shell decides where it
shows, how it closes and how it follows its file.

- `IDocumentService` is the project session's: it owns every open document's
  editor, created from its kind's `DocumentEditorDescriptor` when the
  document's tab opens in any window and disposed once the document has closed.
  It saves documents through their editors, saves before an operation after
  asking in the window that asked, and reloads, keeps or copies a document
  whose file changed on disk.
- It keeps the session's open documents in step with their files: a change the
  catalog reports makes the document check its file (a clean document reloads,
  an edited one is marked changed on disk, a deleted one missing); a relocation
  in the editor moves or rewrites the documents it touches, and closes those
  whose files it deleted, in whichever window has their tab. The project's side
  comes through two ports of `Oxygen.Editor.Documents`, `IDocumentFiles` and
  `IDocumentRelocations`.
- `IDocumentArea` is the document area of the window a part runs in: the
  documents with a tab there, the active one and its editor. It opens an
  asset's file once and shows its document there. A kind whose descriptor
  allows one document per area (scenes) replaces the shown one: its unsaved
  changes are asked about first, and a file that does not open keeps it. It
  saves all of its documents, and saves before an operation after asking in
  its window. `DocumentTab` is the tab of one `Document`.
- An editor that needs its window implements `IDocumentWindowEditor`: while its
  document has a tab in a window, `DocumentWindowAttachments` lends it that
  window's `DocumentWindowServices` (area, commands, placement, asset reveal),
  and withdraws them when the tab leaves. The editor reads them at call time
  and never keeps them past the withdrawal.
- `DocumentAreaView` shows the active document's editor under two banners: the
  file is missing on disk (Save, Close), or it changed on disk while the
  document has unsaved changes (Reload, Keep Mine, Save a Copy).
- `IHistoryService` holds the session's global history, which every window of
  the session undoes and redoes and which ends with the session; each document
  has its own.

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
  replacement of the project. `WindowDocumentsClose` prepares the window's
  documents when the window closes (see [Closing windows](#closing-windows)).
- Preparing raises `DocumentClosing`, whose handlers may veto, then hands the
  tabs to the session's one `DocumentCloseCoordinator`, which the document
  service registers with the tabs and which serves every window. Each editor
  takes part in the close as an `IDocumentCloseParticipant`.
- The coordinator asks `IDocumentClosePrompt`, in the window that closes them: _Save changes to 2
  documents?_ with Save, Don't Save and Cancel, each document listed with its
  asset and, when there are several, a check box. A document whose file changed
  on disk offers Reload and Save a Copy in the prompt. When a document is still
  dirty after a save that succeeded, it changed while saving, and the close is
  refused with a published failure.
- Committing closes each participant, then the tab. The area then closes the
  document in `OpenDocuments`; editors never do. The document service disposes
  the editor after the document closed.

### Events

| Event               | When raised                                      |
| ------------------- | ------------------------------------------------ |
| `DocumentOpened`    | A tab was added; the session creates its editor  |
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

## Closing windows

`CloseAndExit` is the only handler of Aura's `WindowClosing`. It runs every
`IWindowCloseParticipant` in its `WindowCloseLevel` order: the documents, the
window, the session, then the editor.

1. Prepare: each participant asks what must be asked and returns its
   `WindowCloseStep`, or refuses, which keeps the window open.
2. Commit: once the close is approved, each step commits its part; a step that
   refuses keeps the window open.
3. Finalize: once every step committed, each finalizes, before the window is
   destroyed.

`WindowClose.IsLastWindow` tells the participants that no other window is open.
The participants of the editor scope are registered by the composition root:
window placement, the project session (closing its last workspace window opens
the Start window, then closes the session) and the editor's exit (closing the
last window otherwise closes any session, then shuts the engine down). A
window's participants, such as `WindowDocumentsClose`, join while the window
lives.

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
