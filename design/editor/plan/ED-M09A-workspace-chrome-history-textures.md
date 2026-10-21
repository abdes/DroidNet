# ED-M09A - Workspace Title Bar, History And Texture Editor

Status: `planned; starts when issue #69 is done`

## 1. Purpose

Deliver three features designed during issue #69 and kept out of it, so that #69
stays a refactoring: the workspace title bar with its collapsible menu bar and
project switcher, the History panel, and the Texture Editor. They build on the
modules #69 creates (Shell, feature modules, ProjectExplorer, Textures).

## 2. PRD Traceability

| ID        | Delivered by                                                                                 |
| --------- | -------------------------------------------------------------------------------------------- |
| `REQ-002` | The project switcher creates, opens and switches projects from the workspace.                |
| `REQ-006` | The History panel moves a document only by replaying its recorded entries.                   |
| `REQ-022` | Refused history moves, failed panel reopens and project folder failures are visible results. |
| `REQ-047` | The View menu shows every panel again after it is closed.                                    |
| `REQ-048` | Every contributed command is in the menu bar with its shortcut.                              |
| Texture   | The Texture Editor's requirement is added to the PRD with its design (M09A.5).               |

## 3. Required LLDs

- [workspace-title-bar.md](../lld/workspace-title-bar.md), approved
  2026-10-21, with its mockups.
- [history-panel.md](../lld/history-panel.md), approved 2026-10-21, with its
  mockup.
- `texture-editor.md`, written and approved in M09A.5 before any Texture Editor
  code.

## 4. Scope

- The Aura and Controls.Menus capabilities the title bar needs.
- The menu bar, the commands it makes of existing actions, and the reopening of
  closed panels.
- The project switcher and the Project information dialog.
- The History panel, with `HistoryKeeper.ReplayToAsync` and `PositionAfter`,
  `Document.SavedPosition` and the Documents test kit.
- The Texture Editor's design, then its implementation.

## 5. Non-Scope

- A command palette and user key bindings.
- A settings page; the gear's Settings item stays as it is.
- Anything issue #69 owns, including the Shell's window kinds (S5.3), which
  the title bar's start-window navigation uses as they are when it lands.

## 6. Implementation Sequence

The title bar comes first: the History panel is reached from its View menu.
Each slice hands its visible changes to the owner for acceptance in the
running editor before it is committed.

| Slice  | Content                                                                                                                                                                                                                                            | Gate                                                                   |
| ------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------- |
| M09A.1 | Aura: application decoration defaults, the title bar content provider, `ms-appx` and SVG icons. Controls.Menus: the expanded bar's back button, `IMenuSource.Opening`. Title bar design section 7.                                                 | Aura and Controls.Menus tests                                          |
| M09A.2 | The menu bar: `MainMenuProvider`, `MainMenuPresenter`, `CommandMenuCommand`, `WorkspaceFocus`, `GestureText`; the commands of decision 15 with their handlers and the buttons bound to them; the categories; the logo and the `wnd-we` decoration. | Shell tests; owner's acceptance of scenarios 1 to 6 in the editor      |
| M09A.3 | Reopening closed panels through `ShowPanelAsync`, sharing the initial layout's code (decision 13).                                                                                                                                                 | Shell placement tests; owner's acceptance of scenario 5                |
| M09A.4 | The project switcher and the Project information dialog (decisions 17 to 21).                                                                                                                                                                      | ProjectExplorer tests; owner's acceptance of scenarios 7 to 10         |
| M09A.5 | Texture Editor design: `texture-editor.md` and its mockups, from the engine's texture facts and the research on other engines' texture editors; its PRD requirement; this plan's Texture Editor slices.                                            | Owner's approval of the design                                         |
| M09A.6 | History panel: TimeMachine `ReplayToAsync` and `PositionAfter`, `Document.SavedPosition`, `Oxygen.Editor.Documents.Testing`, the `Oxygen.Editor.History` module and view.                                                                          | TimeMachine, Documents and History tests; owner's acceptance in editor |
| M09A.7 | Texture Editor implementation, in the slices M09A.5 defines.                                                                                                                                                                                       | Set by M09A.5                                                          |

M09A.5 can run while M09A.1 to M09A.4 are implemented. The History code written
before its design was approved is kept on the local branch
`wip/history-panel-code`; M09A.6 takes from it only what matches the approved
design, and the branch is deleted when M09A.6 lands.

## 7. Project And File Touch Points

- `projects/Aura/src`: `Decoration/`, `Settings/WindowDecorationSettingsService.cs`,
  `MainShellView.xaml(.cs)`, `MainShellViewModel.cs`.
- `projects/Controls/Menus/src`: `ExpandableMenuBar*`, `IMenuSource.cs`,
  `MenuSource.cs`, `MenuBar*`.
- `projects/Oxygen.Editor/src`: `Program.cs` (decorations, providers),
  `Assets/OxygenLogo.svg`.
- `projects/Oxygen.Editor.Shell/src`: `Menus/` (new), `Commands/`,
  `Layout/DockingPlacementService.cs`, `Layout/DockingWorkspaceViewModel.cs`.
- `projects/Oxygen.Editor.ProjectExplorer/src`: the switcher and the
  information dialog.
- `projects/Oxygen.Editor.ContentExplorer`, `SceneEditor`, `SceneExplorer`:
  command categories, new commands and their buttons.
- `projects/TimeMachine/src/HistoryKeeper.cs`,
  `projects/Oxygen.Editor.Documents/src/Document.cs` and `testing/` (new),
  `projects/Oxygen.Editor.History` (new).
- `projects/Oxygen.Editor.TextureEditor` (new) and `projects/Oxygen.Editor.Textures`,
  as M09A.5 defines.

## 8. Dependency And Execution Risks

| Risk                                                                           | Handling                                                                                         |
| ------------------------------------------------------------------------------ | ------------------------------------------------------------------------------------------------ |
| A user's persisted decoration override for `wnd-we` hides the menu             | The override wins by design; the owner clears it once if one exists                              |
| Menu commands run without the context their shortcut has                       | `WorkspaceFocus` supplies the focused element; tests cover a command that depends on its context |
| Recreating a closed panel depends on Docking's route-based creation            | M09A.3 reuses the initial layout's code; Docking tests cover a reopen after close                |
| The Texture Editor's preview needs engine capabilities that do not exist today | M09A.5 states each gap explicitly for the owner's decision; nothing is designed as if it existed |

## 9. Validation Gates

- Every slice: its module tests, run by the owner, and the owner's editor build.
- Visible changes: the owner's acceptance in the running editor against the
  LLDs' scenarios and mockups.
- Milestone exit: every scenario of the three LLDs accepted in the editor.

## 10. Status Ledger Hook

[IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) records progress and
evidence per slice.

| Slice  | Status        |
| ------ | ------------- |
| M09A.1 | `not started` |
| M09A.2 | `not started` |
| M09A.3 | `not started` |
| M09A.4 | `not started` |
| M09A.5 | `not started` |
| M09A.6 | `not started` |
| M09A.7 | `not started` |
