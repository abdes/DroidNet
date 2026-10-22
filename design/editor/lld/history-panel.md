# History Panel LLD

Status: `approved 2026-10-21; delivered by ED-M10A`

Last Updated: 2026-10-21

## 1. Purpose

Show the user what each action recorded and let them walk back and forth through
it without leaving a mess. The History panel is passive, as Output and Problems
are: it shows the active document's history and the project's (global) history,
and it changes them only by moving one of them to an entry the user chooses. It
is decision 1 of [editor-documents.md](editor-documents.md), and the module
`Oxygen.Editor.History` of
[editor-module-architecture.md](editor-module-architecture.md).

## 2. PRD Traceability

| ID        | Coverage                                                                                                                                                                      |
| --------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `REQ-006` | The panel changes a document only by replaying its recorded entries, the path Undo and Redo take, so the document's dirty state follows the move as it follows Undo and Redo. |
| `REQ-022` | A refused undo or redo produces a visible operation result.                                                                                                                   |

## 3. Architecture Links

- [editor-documents.md](editor-documents.md): decision 1, the histories, who
  records into them and who owns them.
- [document-kinds.md](document-kinds.md): a document and the history it owns.
- [editor-module-architecture.md](editor-module-architecture.md): section 6,
  the Shell's ownership of the panel registry, placement and command routing.
  No live LLD details them yet; the Shell's design is the review record
  [editor-shell.md](editor-shell.md).
- [diagnostics-operation-results.md](diagnostics-operation-results.md): how a
  failed operation reaches the user.
- [workspace-title-bar.md](workspace-title-bar.md): the workspace title bar and its menu bar, whose View menu lists
  every contributed panel, the History panel among them.

## 4. User Scenarios

1. **See what an action recorded.** The user moves three nodes and renames one
   in a scene. The panel's document section shows "Opened", then the move and
   the rename, each by the label Undo would undo, with the current point at the
   rename.
2. **Go back several steps at once.** The user clicks the first move. The scene
   returns to the state after it in one operation; the later entries stay in
   the list, dimmed, after the current point.
3. **Go forward again.** The user clicks the dimmed rename. Every entry up to it
   is redone, and nothing is dimmed.
4. **Undo everything.** The user clicks "Opened". Every entry of that history
   is undone; the other history does not move.
5. **Switch documents.** The user selects a material's tab. The document section
   shows the material's history; the scene's history keeps its own position and
   is shown again when its tab returns.
6. **Project actions.** The user renames an asset in the Content Explorer. The
   rename appears in the project section, whatever document is active, and
   clicking above it undoes it like Undo with no document active would.
7. **A step is refused.** The user clicks "Project opened", and undoing a rename
   is refused because its old name is now taken. The walk stops there: the
   entries after it are undone, the refused rename is still the current point
   and carries a warning icon whose tooltip gives the reason, and the
   operation-results notification says what was refused and why. Clicking again
   retries from there.
8. **Nothing to show.** With no document open, the document section reads "No
   document open" with a one-line hint; the project section still shows its
   history.
9. **Know what is saved.** The user saves after the move, then renames. The move
   carries a "Saved" tag and the section header shows the unsaved dot. Clicking
   the move returns the scene to what the file holds, and the dot clears.
10. **Undo a project action while editing a scene.** Ctrl+Z goes to the active
    scene, so the user undoes the asset move with the project section's own Undo
    button, without leaving the scene.
11. **Bring the panel back.** The user closes the History tab, then chooses View
    > History in the menu bar; the panel returns to its place in the default
    > layout.

## 5. Integration Baseline

What exists today, which the panel reads and does not change:

- `IDocumentArea` (Shell, per window): `ActiveDocument` and
  `ActiveDocumentChanged`, raised on the UI thread.
- `Document` (`Oxygen.Editor.Documents`): `History`, a `HistoryKeeper` the
  document owns for its lifetime, `Title`, `Kind`, `IsDirty` and
  `StateChanged`. The position the file was saved at is private
  (`savedPosition`).
- `IHistoryService` (Shell, per session): `GlobalHistory`, the history of the
  actions outside any one document (relocating, renaming or deleting assets).
- `HistoryKeeper` (`DroidNet.TimeMachine`): `UndoStack`, oldest entry first and
  the next undo last; `RedoStack`, the next redo first; both observable
  collections changed on the caller's context. `UndoAsync` and `RedoAsync`
  replay one entry; a change that throws goes back to its stack at its position
  (unless it recorded part of its inverse) and the exception propagates.
  `IsBusy` is true while a replay or a change set runs.
- An entry's label is `IChange.Key`: scene edits key `SceneHistoryEntry` by its
  label, relocations key their action by the operation's label ("Rename",
  "Move").
- The Shell's `CommandService` routes Undo and Redo to the active document's
  history, or to the global history when no document is active.
- `IOperationResultPublisher` (`Oxygen.Managed.Core.Diagnostics`) publishes
  `OperationResult`s, which the editor's operation-results notification and
  Output show.
- `IFeatureModule` (Shell) contributes panels (`PanelDescriptor`) and commands
  (`CommandDescriptor`) to each workspace window.
- The editor's menu bar ([workspace-title-bar.md](workspace-title-bar.md)), built before this panel,
  lists every contributed panel in its View menu.

## 6. Decisions

1. **Two sections, one history each.** The active document's history on top,
   the project's below. The two histories are independent stacks; a merged list
   would make one click cross both and undo actions of a history the user did
   not choose. Each section is one `HistoryKeeper`.
2. **Rows.** Each section lists a start row ("Opened" for a document,
   "Project opened" for the project), then the done entries oldest first, then
   the undone entries in redo order. A marker shows the current point, the row
   whose state the history is in; undone rows are dimmed (section 10).
3. **Choosing a row moves only its history,** to the state after that row: the
   done rows after it are undone, the undone rows up to it are redone, in one
   operation of the history (decision 4). Choosing the current row does
   nothing; so does choosing while that history is busy.
4. **One replay-to operation in TimeMachine.** `HistoryKeeper` gains
   `ReplayToAsync` (section 7). The panel and any other caller move a history
   with it and never loop over `UndoAsync` or `RedoAsync`, so no other undo or
   redo can interleave and the history is busy for the whole walk.
5. **A refused change stops the walk** at a consistent position: the refused
   change stays on its stack, the entries replayed before it stay replayed, and
   the refusal is reported (decision 6). The user retries by choosing a row
   again.
6. **Refusals go to the operation-results notification,** as a failed
   `OperationResult` (section 9), and the row the walk stopped at carries a
   warning icon with the reason in its tooltip until that history moves or
   records again, so the user sees where it stopped.
7. **The panel reads the histories through `IDocumentArea` and
   `IHistoryService`.** `IDocumentService` does not expose them, and the panel
   needs no other Shell service.
8. **Identity and placement.** Panel id `history`, title "History". The scene
   workspace's dock layout places it with Output and Problems, below the
   Content Explorer, as one more tab.
9. **Reached from the View menu.** The menu bar lists every contributed panel in
   its View menu ([workspace-title-bar.md](workspace-title-bar.md)); the History panel needs no
   command of its own and contributes none.
10. **Each section has its own Undo and Redo.** Ctrl+Z and Ctrl+Y keep going to
    the active document; the section buttons move one step of their own history
    through `ReplayToAsync`, so the project history can be walked without
    closing documents.
11. **The saved state is shown.** A document section tags the row whose
    position the file was saved at, and its header shows the unsaved dot while
    `IsDirty`. The project history has no file and no tag.

## 7. HistoryKeeper Change

```csharp
/// Undoes or redoes entries, as one operation, until the undo stack holds
/// undoCount entries: the state after the oldest undoCount entries.
public ValueTask ReplayToAsync(int undoCount, CancellationToken cancellationToken = default);
```

Semantics:

- `undoCount` below `UndoStack.Count` undoes, above it redoes, equal does
  nothing. `0` undoes everything; `UndoStack.Count + RedoStack.Count` redoes
  everything. Outside that range it throws `ArgumentOutOfRangeException` before
  anything runs.
- The history is busy (`IsBusy`) from the first entry to the end. `UndoAsync`,
  `RedoAsync`, `Undo`, `Redo` and another `ReplayToAsync` called meanwhile do
  nothing, as they do while one undo runs today; `Clear` throws, as it does
  outside the idle state.
- Each entry replays as `UndoAsync` or `RedoAsync` replays it, so a change set
  stays one entry and the recorded inverse goes to the other stack.
- A change that throws stops the walk: that change goes back to its stack at its
  position as after a failed `UndoAsync` or `RedoAsync`, the entries replayed
  before it stay replayed, the history is idle again, and the exception
  propagates.
- Cancellation is checked before each entry and throws
  `OperationCanceledException`, leaving the replayed entries replayed.
- Every real change records its inverse when it applies: scene edits record the
  opposite edit, material edits restore the previous source, relocations record
  the reverse moves. A change that applies without recording one is a defect of
  that change, and it would leave a redo walk unable to advance. The walk
  detects it after the entry, stops, and throws `InvalidOperationException`
  naming the entry, which the panel reports like any refusal.

The change is internal to `HistoryKeeper`: the bodies of `UndoAsync` and
`RedoAsync` become private steps that both they and `ReplayToAsync` call, and
the busy flag covers the whole walk. The TimeMachine README documents the
operation.

To tag the saved row, the panel needs each row's position:

```csharp
/// The position the history is at after its oldest undoCount entries, for
/// 0 to UndoStack.Count + RedoStack.Count.
public HistoryPosition PositionAfter(int undoCount);
```

It reads the positions the keeper already records for both stacks and the
empty position for `0`; outside the range it throws
`ArgumentOutOfRangeException`. `Document` gains a public `SavedPosition`, the
private field it has today, changed only by a save and announced by
`StateChanged` as today. A row is the saved row when
`PositionAfter(row.UndoCount) == SavedPosition`; when no row matches, the saved
state is no longer in the history and no row is tagged.

## 8. Module And Types

`Oxygen.Editor.History` (feature layer), referencing Shell, TimeMachine,
Managed.Core and Mvvm:

| Type               | Role                                                                                                                                                       |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `HistoryModule`    | `IFeatureModule`: the panel (`history`, "History"); registers the view model (one per window) and the view.                                                |
| `HistoryViewModel` | The panel: a `Document` section following `IDocumentArea.ActiveDocument` and a `Project` section over `IHistoryService.GlobalHistory`.                     |
| `HistorySection`   | One history: header facts, rows rebuilt when either stack changes, the move-to-row, Undo and Redo commands through `ReplayToAsync`, busy and the stop row. |
| `HistoryRow`       | Label, the undo count it stands for, undone, current, saved, and the refusal it stopped at, if any.                                                        |
| `HistoryView`      | The two sections of section 10; clicking or pressing Enter on a row runs its section's move.                                                               |

## 9. Operation Results And Diagnostics

A refused move publishes one failed `OperationResult`:

- Operation kind `History.Replay`.
- Title "Undo failed" when the chosen row was done, "Redo failed" when it was
  undone; the message names the chosen entry and the refusal's message.
- One `DiagnosticRecord`, code `OXE.DOCUMENT.HISTORY_REPLAY_FAILED`, with the
  exception's type and message, in the domain of the history it moved:
  - the document section: `Document` ("document workflow"), since the entries
    are the document's own edits;
  - the project section: `AssetIdentity` ("asset identity and catalog
    resolution"), since the project history holds the renames, moves and
    deletes of assets, and their refusals are identity refusals such as a taken
    name.

The refusal is also logged as a warning. Cancellation is not reported.

## 10. UI Design

The panel follows the density and styles of Output and Problems: 28 px rows,
Segoe Fluent icons, theme brushes only, no custom colors.

The approved mockup is [mockups/history-panel.svg](mockups/history-panel.svg)
(dark theme). The implementation matches it; where the mockup and this text
differ, this text wins.

**Layout.** Two sections stacked in one dock tab. The document section takes
the remaining height; the project section sits below it, a third of the height
by default, and collapses to its header. Each section scrolls on its own, and
its collapsed state is a per-user setting.

**Section header** (32 px): a collapse chevron, the kind's icon, the title in
body-strong (the document's `Title`, or "Project"), a secondary qualifier (the
document kind's name, or the project's name), the unsaved dot when the document
is dirty, then on the right a count, "5 of 7" (current step of all steps), and
Undo and Redo icon buttons with tooltips naming the step they would move ("Undo
Change light intensity"). The buttons are enabled only when there is a step to
move and the history is idle.

**Rows** (28 px), in a gutter-and-label layout:

| Row          | Gutter                    | Label                                         | Other                                       |
| ------------ | ------------------------- | --------------------------------------------- | ------------------------------------------- |
| Start        | the kind's or folder icon | "Opened" or "Project opened", secondary color | none                                        |
| Done         | empty                     | the entry's label, primary color              | "Saved" tag when it is the saved row        |
| Current      | accent dot                | body-strong                                   | accent bar on the left edge and accent wash |
| Undone       | redo arrow                | tertiary color                                | none                                        |
| Stopped here | warning icon              | as its state                                  | tooltip with the refusal's message          |

Labels trim with an ellipsis and show the full text in a tooltip. Every row's
tooltip also says what clicking it does: "Undo 3 steps to here", "Redo 2 steps
to here", or "Current state".

**Interaction.** A single click on a row moves its history there (decision 3);
there is no selection state, only focus. Up and Down move focus, Enter moves to
the focused row, Home focuses the start row. After any move the list scrolls to
keep the current row in view.

**Busy.** While a history moves, a thin indeterminate progress bar runs under
its header, its rows and buttons ignore input, and the other section stays
usable.

**Empty states.** No active document: the document section shows "No document
open" and "Open a scene or a material to see its changes here." A document with
no edits shows only its start row and the hint "Your changes appear here."

**Accessibility.** Each row's automation name reads its label and state ("Move 3
nodes, saved", "Change light intensity, current step", "Delete node Crate,
undone"). A completed move raises a polite announcement: "Undid 3 steps" or
"Redid 2 steps".

Other surfaces: the operation-results notification for a refusal (section 9).

## 11. Dependency Rules

- The module is a feature: it references Shell, TimeMachine, Managed.Core and
  Mvvm, and no domain module. The boundary test lists it as a feature.
- It records nothing into a history and owns none; it never clears one.

## 12. Test Strategy

- TimeMachine unit tests for `ReplayToAsync`: undo and redo across several
  entries and back; a refusal part way (the refused change stays on its stack,
  the earlier entries stay replayed, idle afterwards, a retry completes); busy
  for the whole walk (an undo meanwhile is ignored, `Clear` throws);
  cancellation between entries; a count outside the history.
- TimeMachine unit tests for `PositionAfter`: the start, done and undone
  positions match `Position` after replaying to each count; a count outside the
  history throws.
- History unit tests: the module contributes the panel; the rows of a history
  with done and undone entries, the current marker and the counts; choosing a
  row moves the history and the rows follow; the section Undo and Redo move one
  step and follow busy; a refusal publishes the failed result with its kind and
  code, marks the stop row, and the mark clears on the next move; the saved tag
  follows saves and disappears when the saved state leaves the history; the
  empty states; the panel shows the project's history with no document active
  and stops following the area once disposed.
- The document section follows the active document (scenario 5): two documents
  open through the Documents test kit below, the area's active document
  switches between them, and the section shows each one's history and keeps the
  other's position; with none active it reads "No document".

A `Document` exists only through `OpenDocuments` and a kind, and no test kit
offers one today; the Documents tests keep a note kind to themselves. The slice
adds `Oxygen.Editor.Documents.Testing` (`projects/Oxygen.Editor.Documents/testing`):

- `NoteKind` and `NoteContent` move into it from the Documents tests and become
  public: a text document kind whose content is one line, edited through its
  history.
- `TestDocuments`: an in-memory file system with the real
  `NativeAtomicFileStore` and an `OpenDocuments` over `NoteKind`;
  `OpenAsync(name, text)` writes the note and returns its open `Document`, and
  `Edit(document, text)` records an edit into the document's history.
- The Documents tests use the kit instead of their private copies; the History
  tests use it for scenario 5.
- Visible UI waits for the owner's acceptance in the editor.

## 13. Dependencies

- The menu bar of [workspace-title-bar.md](workspace-title-bar.md) is built first; its View menu
  lists this panel through its `PanelDescriptor`.
- `HistoryKeeper.ReplayToAsync` and `PositionAfter`, and
  `Document.SavedPosition`, land with this panel.

## 14. Design Corrections

- [editor-documents.md](editor-documents.md) decision 1 says the panel "reads
  the histories through the Shell's document service". It reads them through
  the window's document area (`IDocumentArea`) and the session's histories
  (`IHistoryService`); the decision's text and its section 6 row change to say
  so, and link this LLD.
