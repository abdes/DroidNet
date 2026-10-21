# Workspace Title Bar LLD

Status: `approved 2026-10-21; delivered by ED-M09A`

Last Updated: 2026-10-21

## 1. Purpose

Give the workspace window a title bar that carries the editor's identity, its
commands and its project: the Oxygen logo, a collapsible menu bar, and a project
switcher. The menu bar rests as one menu button, expands in place into the full
bar, and collapses again after a command, a dismissal or its back button. Its
menus are built from what the feature modules contribute. The project switcher
names the open project and lets the user create, open or switch projects and
see the project's information without returning to the start window.

Mockups (dark theme; the implementation matches them, and where a mockup and
this text differ, this text wins):

- [mockups/title-bar.svg](mockups/title-bar.svg): the title bar, resting and
  expanded.
- [mockups/project-switcher.svg](mockups/project-switcher.svg): the project
  switcher's flyout.
- [mockups/project-information.svg](mockups/project-information.svg): the
  Project information dialog.

## 2. PRD Traceability

| ID        | Coverage                                                                                           |
| --------- | -------------------------------------------------------------------------------------------------- |
| `REQ-002` | Create and open projects, and switch to a recent one, from the workspace as from the start window. |
| `REQ-047` | New: every workspace panel can be shown again after it is closed, from the View menu.              |
| `REQ-048` | New: every contributed command can be found in the menu bar, with its shortcut, when it can run.   |

The PRD gains `REQ-047` and `REQ-048` with this design.

## 3. Architecture Links

- [editor-module-architecture.md](editor-module-architecture.md): section 6,
  the Shell owns menus, the panel registry, placement and command routing.
- [project-workspace-shell.md](project-workspace-shell.md): the workspace
  window, its dock layout and project activation.
- [history-panel.md](history-panel.md): the first panel built on the View menu.
- [application-density.md](application-density.md): `MenuOptions.IsCompact` and
  the title bar's fixed height.
- [input-authoring.md](input-authoring.md): F10 and Tab do not activate the
  menu bar.

## 4. User Scenarios

1. **Resting title bar.** The workspace opens on "Courtyard study". The title
   bar shows the Oxygen logo, the menu button, and a "Courtyard study" button
   with a chevron; its tooltip is the project's folder.
2. **Find and run a command.** The user clicks the menu button. It turns into a
   back arrow followed by File, Edit, View, Scene and Cook. With nodes selected
   in the Scene Explorer, the user chooses Edit > Delete; the nodes are deleted
   as with the Delete key there, and the bar collapses back to the menu button.
3. **Collapse without a command.** With the bar expanded, the user clicks the
   back arrow, presses Esc or clicks elsewhere; the bar collapses and focus
   returns where it was.
4. **See what cannot run.** With nothing to undo, Edit > Undo is disabled.
5. **Bring a panel back.** The user closed Problems. View > Problems recreates
   it in its place in the default layout and activates it; View > Output on an
   open but hidden Output brings it to the front.
6. **Save from anywhere.** File > Save (Ctrl+S) saves the active document,
   whichever panel has focus; File > Save all saves every dirty document of the
   window.
7. **Switch project.** The user opens the project switcher and picks "Material
   studies" under Recent projects. The editor asks to save the unsaved
   documents, as it does for any project activation, then the workspace shows
   "Material studies". Cancelling the prompt keeps the current project open.
8. **Create or open a project.** New project… and Open project… open the start
   window on its New or Open page; the workspace stays open until the new
   project activates, and closing the start window keeps the current project.
9. **Project information.** Project information… shows the project's name,
   folder, content roots and cooked content, with a button to open the folder
   and the Cook on change setting.
10. **A recent project is gone.** A recent project whose folder no longer
    validates is shown with a warning icon and its reason, and cannot be
    chosen.

## 5. Integration Baseline

What exists today:

- **Aura title bar** (`MainShellView`): one 32 px row with fixed columns: the
  app icon (`AppIcon`, from `TitleBarOptions.IconPath`, resolved inside Aura's
  assembly), the primary commands (shown only when the decoration has a menu: a
  `MenuBar`, or an `ExpandableMenuBar` when `MenuOptions.IsCompact`), an empty
  drag column, the gear settings menu, and the caption buttons' reserve. No
  application content can be placed in it, and no window title is shown.
- **ExpandableMenuBar** (Controls.Menus): the hamburger button and a `MenuBar`
  swapped in place by its `Collapsed` and `Expanded` states; it collapses after
  a command, Esc or a click outside, and focuses the hamburger after collapsing.
  The expanded state has no button to collapse it.
- **Menus.** An `IMenuSource` of observable `MenuItemData` (text, command, icon,
  accelerator text, mnemonic, enabled, checkable, separators, submenus).
  `AcceleratorText` is display text only.
- **Aura menus per window.** `WindowDecorationOptions.Menu = MenuOptions {
MenuProviderId, IsCompact }`; at window creation the `IMenuProvider` with that
  id makes the window's `IMenuSource`, which the title bar shows.
- **Decoration defaults.** A category's decoration comes from persisted user
  overrides, then Aura's built-in categories, then `System`. The editor's
  `wnd-we` and `wnd-pe` categories have no code default, and the editor declares
  no menu.
- **Shell contributions.** `IFeatureModule` contributes `PanelDescriptor`s and
  `CommandDescriptor`s (`Id`, `Label`, `Category`, `KeyGestures`); each
  workspace window has a `ContributionRegistry` of the Shell's commands and the
  contributed ones. `ICommandService.CanExecute` and `ExecuteAsync(id,
context)` route to the handlers of the view models around the context
  element, then the active editor, then the registered handlers.
- **Commands today.** Shell: Undo, Redo, Cut, Copy, Paste and Delete (`Edit`).
  Scene Editor: the Select, Move, Rotate and Scale tools, Frame Selection,
  Frame All, Duplicate, Play and Stop. Scene Explorer: Rename and Find in Scene
  Explorer. Those two modules' commands are in the default category `General`.
- **Actions that are not commands today.** Save is a Scene Editor toolbar button
  with its own Ctrl+S accelerator; documents close from their tab's close
  button; New scene, Import and Cook project are Content Explorer buttons and
  menus; Cook scene is a Scene Editor button. Save all, Close all and Exit do
  not exist as user actions.
- **Panels.** `IPlacementService.ShowPanelAsync` pins and activates a panel that
  exists. Closing a panel destroys it, so `ShowPanelAsync` returns false
  afterwards. Panels are created from their workspace routes by the initial
  layout only.
- **Projects.** `IProjectActivationCoordinator.ActivateAsync` opens or creates
  a project: it validates and stages it, asks to save the unsaved documents of
  every window (a refusal cancels), activates it and navigates the workspace
  window. Only the start window calls it. Recent projects come from
  `IRecentProjectAdapter` (`RecentProjectEntry`: name, location, last use,
  validation). `ProjectContext` exposes the name, root, category, mounts and
  cooked content order; `ProjectSettings` holds Cook on change. No view shows a
  project's information.

## 6. Decisions

### Title bar

1. **Layout.** From the left: the Oxygen logo, the collapsible menu bar, the
   project switcher, the drag area, the gear settings menu, the caption buttons.
   The start window keeps today's title bar, without menu or switcher.
2. **The menu bar is collapsible, always.** The workspace decoration declares
   `MenuOptions("Oxygen.Editor.MainMenu", IsCompact: true)`. Resting, it is the
   menu button; expanded, a back arrow then the menus, in the same place; the
   project switcher moves right to make room and stays visible.
3. **The expanded bar has a back arrow.** `ExpandableMenuBar` gains a back
   button (Segoe Fluent `E72B`) before the `MenuBar` in its `Expanded` state,
   which collapses the bar and focuses the menu button.
4. **Aura hosts application content in the title bar.** `TitleBarOptions` gains
   `ContentProviderId`; Aura resolves the registered `ITitleBarContentProvider`
   with that id when the window is created, as it does for menus, and shows its
   content in a new column after the primary commands. The minimum window width
   includes it. The editor registers the project switcher as that content.
5. **The logo is the editor's, as a vector.** The editor ships
   `Assets/OxygenLogo.svg` (the owner's logo, with the dark fill that hid the
   ring under the small circle replaced by a gap in the ring, so it reads the
   same on light and dark title bars). `TitleBarOptions.IconPath` accepts an
   `ms-appx:///` URI, used as is, and Aura shows an `.svg` path through an
   `ImageIconSource` over an `SvgImageSource`, any other through today's
   `BitmapIconSource`; a relative path keeps resolving inside Aura.
6. **Applications give their categories code defaults.** Aura's decoration
   settings gain application defaults, registered at startup, used after the
   user's persisted overrides and before Aura's built-in defaults. The editor
   registers `wnd-we` (the `Main` decoration with the logo, the compact menu and
   the switcher) and `wnd-pe` (the `Main` decoration with the logo).

### Menus

7. **The menus come from the contributions.** The menu provider returns an
   empty menu source per window; the workspace's `MainMenuPresenter` fills it
   from the window's `ContributionRegistry` when the workspace activates and
   empties it when the workspace closes. No feature writes menu items.
8. **A command's category is its menu.** The menus are, in this order: File,
   Edit, View, Scene, Cook, then any other category in the order its first
   command was registered. Commands in `General` are in no menu.
9. **Items follow contribution order,** with a separator between the commands
   of different modules. The text is the command's `Label`, the accelerator
   text its first key gesture ("Ctrl+Shift+S"), and each top-level menu has a
   mnemonic (File, Edit, View, Scene, Cook).
10. **Menu items run commands through `ICommandService`, in context.** An item
    calls `CanExecute` and `ExecuteAsync` with the element that had focus in the
    workspace before the menu took it, so a command from the menu reaches the
    handler its shortcut would.
11. **Enabled state is re-evaluated when a menu opens.** Controls.Menus gains an
    `Opening` notification on `IMenuSource`; the presenter refreshes that menu's
    items then. Disabled items are shown, not hidden.
12. **View lists the panels.** One item per contributed panel of `Unique`
    multiplicity, in registration order, with its title and icon, showing and
    activating it through `ShowPanelAsync`. View never hides a panel.
13. **A closed panel is recreated** by `ShowPanelAsync` from its workspace
    route, docked with that route's docking information through the code the
    initial layout uses; when its anchor panel is closed too, it docks to the
    root at its position, as the initial layout does for a missing anchor.
14. **No keys open the bar.** F10 and Tab keep their meaning; once open, the bar
    is navigated with arrows, Enter, Esc and the mnemonics.

### Menu contents

15. **Existing actions become commands,** so they appear in the menu with their
    shortcuts and run the same way from a button, a shortcut or the menu. The
    buttons that run them today bind to the commands instead of their own
    handlers, and the Scene Editor's own Ctrl+S accelerator goes away.

| Menu  | Item (command id)                                  | Shortcut        | Handled by                                 |
| ----- | -------------------------------------------------- | --------------- | ------------------------------------------ |
| File  | New project… (`Project.New`)                       |                 | Shell: the start window's New page         |
|       | Open project… (`Project.Open`)                     |                 | Shell: the start window's Open page        |
|       | Save (`File.Save`)                                 | Ctrl+S          | Shell: the active document                 |
|       | Save all (`File.SaveAll`)                          | Ctrl+Shift+S    | Shell: every dirty document of the window  |
|       | Close (`File.Close`)                               | Ctrl+W, Ctrl+F4 | Shell: the active document, as its tab's X |
|       | Close all (`File.CloseAll`)                        |                 | Shell: every document of the window        |
|       | New scene… (`Content.NewScene`)                    |                 | Content Explorer, in its current folder    |
|       | Import source model… (`Content.ImportModel`)       |                 | Content Explorer                           |
|       | Import texture or image… (`Content.ImportTexture`) |                 | Content Explorer                           |
|       | Exit (`File.Exit`)                                 | Alt+F4          | Shell: closes the window, as its X         |
| Edit  | Undo, Redo                                         | Ctrl+Z, Ctrl+Y  | Shell (today)                              |
|       | Cut, Copy, Paste                                   | Ctrl+X, C, V    | Shell routing (today)                      |
|       | Duplicate (`Scene.Duplicate`)                      | Ctrl+D          | Scene Editor (today)                       |
|       | Rename (`Scene.Rename`)                            | F2              | Scene Explorer (today)                     |
|       | Delete                                             | Del             | Shell routing (today)                      |
| View  | One item per panel                                 |                 | Shell (decision 12)                        |
|       | Frame selection, Frame all                         | F, Shift+F      | Scene Editor (today)                       |
| Scene | Select, Move, Rotate, Scale tools                  | Q, W, E, R      | Scene Editor (today)                       |
|       | Find in Scene Explorer                             | Ctrl+F          | Scene Explorer (today)                     |
|       | Play, Stop                                         | F5, Shift+F5    | Scene Editor (today)                       |
| Cook  | Cook scene (`Scene.Cook`)                          |                 | Scene Editor: the active scene             |
|       | Cook project (`Content.CookProject`)               |                 | Content Explorer                           |

Categories follow the table: Duplicate and Rename take `Edit`, Frame selection
and Frame all take `View`, the remaining Scene Editor and Scene Explorer
commands take `Scene`. Delete gains the shortcut Del. File's import items and
New scene are handled by the Content Explorer through a handler it registers
with the window's command service, so they run whatever has focus. Alt+F4
stays the system's; the menu only shows it.

16. **Save, Close and Exit use the existing flows.** Save and Save all go
    through the Shell's document service, with its conflict and refusal
    results; Close and Close all through the document tabs, with the usual
    unsaved prompt; Exit closes the workspace window, whose close coordinator
    prompts as the X button does.

### Project switcher

17. **The button** shows the active project's name and a chevron, uses the
    subtle button style at the title bar's height, and has the project's folder
    as its tooltip.
18. **The flyout** lists, top to bottom: New project…, Open project…; a
    separator; "Current project" with the project (cube icon, name, folder,
    check mark); "Recent projects" with up to five other recent projects (folder
    icon, name, folder), most recent first; a separator; Project information….
    Its design is in the switcher mockup.
19. **Choosing a recent project switches to it** through
    `IProjectActivationCoordinator.ActivateAsync` with a new source surface,
    `TitleBar`. The coordinator's prompts, cancellation and failure results
    apply unchanged. A recent project that does not validate shows a warning
    icon and its reason in a tooltip, and cannot be chosen.
20. **New project… and Open project…** run `Project.New` and `Project.Open`:
    they open the start window on `/pe/new` or `/pe/open` as a separate window.
    The workspace is replaced only if a project activates from there.
21. **Project information…** opens a dialog with the project's name, category,
    folder (with an "Open folder" button that opens it in File Explorer), its
    content roots, its cooked content in mount order, and the Cook on change
    setting as a toggle. It is the only project settings surface; the Content
    Explorer's Cook on change item stays and edits the same setting.

## 7. Aura And Controls Changes

- `IWindowDecorationSettings.SetApplicationDefault(WindowCategory,
WindowDecorationOptions)`: application defaults, not persisted, validated as
  overrides are; `GetEffectiveDecoration` resolves an override, then an
  application default, then Aura's default, then `System`.
- `TitleBarOptions.ContentProviderId` and `ITitleBarContentProvider { string
ProviderId; object CreateContent(IManagedWindow window); }`, registered like
  `IMenuProvider`; `MainShellView` shows the content in a column after the
  primary commands and counts it in the minimum width.
- `TitleBarOptions.IconPath` accepts an `ms-appx:///` URI, and an `.svg` icon
  is shown as an `SvgImageSource`.
- `ExpandableMenuBar` gains the back button of decision 3 (template part
  `PART_BackButton`, optional for existing templates).
- `IMenuSource` gains `event EventHandler<MenuOpeningEventArgs> Opening`,
  raised by `MenuBar` and `ExpandableMenuBar` before a top-level menu shows its
  items, with the root `MenuItemData` being opened.

Each change has its own tests in Aura or Controls.Menus, and each README
describes it.

## 8. Editor Types

| Type                       | Module          | Role                                                                                             |
| -------------------------- | --------------- | ------------------------------------------------------------------------------------------------ |
| `MainMenuProvider`         | Shell           | `IMenuProvider` `Oxygen.Editor.MainMenu`: a new empty `MenuSource` per window.                   |
| `MainMenuPresenter`        | Shell           | Window service: builds and clears the menus (decisions 7 to 12).                                 |
| `CommandMenuCommand`       | Shell           | An item's `ICommand`, through `ICommandService` with the focus context.                          |
| `WorkspaceFocus`           | Shell           | Window service: the last element focused in the workspace content, the context of menu commands. |
| `GestureText`              | Shell           | Formats a `CommandKeyGesture` as accelerator text.                                               |
| `FileCommands`             | Shell           | The `File.*` and `Project.*` descriptors and their handlers (decision 16, 20).                   |
| `ProjectSwitcherProvider`  | ProjectExplorer | `ITitleBarContentProvider` `Oxygen.Editor.ProjectSwitcher`: the switcher's view and view model.  |
| `ProjectSwitcherViewModel` | ProjectExplorer | The active project, the recent projects, switching, and opening the information dialog.          |
| `ProjectInformationDialog` | ProjectExplorer | The dialog of decision 21 and its view model.                                                    |

The switcher lives in ProjectExplorer, the module that owns project choice,
recents and activation requests. `DockingPlacementService` and
`DockingWorkspaceViewModel` share the code that creates and docks a panel from
its route (decision 13). The Content Explorer, Scene Editor and Scene Explorer
change their descriptors' categories and bind their buttons to the commands of
decision 15.

## 9. Operation Results And Diagnostics

- A command keeps reporting through its handler, as from its shortcut or
  button.
- A reopen that cannot dock a panel logs a warning and publishes a failed
  `OperationResult`, kind `Workspace.ShowPanel`, code
  `OXE.WORKSPACE.PANEL_SHOW_FAILED`, domain `WorkspaceRestoration`, naming the
  panel.
- Switching projects reports through the activation coordinator's results, with
  source surface `TitleBar`.
- Opening the project folder that no longer exists publishes a failed result,
  kind `Project.OpenFolder`, code `OXE.PROJECT.FOLDER_MISSING`, domain
  `ProjectValidation`.

## 10. UI Design

- **Title bar** (32 px, the title bar's style): the 20 px logo; the 32 px menu
  button (Segoe Fluent `E700`); the project switcher, a subtle button with the
  project name in body-strong and a chevron (`E70D`). Expanded, the back arrow
  (`E72B`) and the menus replace the menu button in place, in the menu bar's
  default style.
- **Menus**: the control's cascaded surfaces: icon column, label, accelerator
  text right-aligned, disabled items dimmed, separators between modules.
- **Switcher flyout** (about 320 px wide): action rows with an icon and label;
  section labels in caption style; project rows of two lines (name in
  body-strong, folder in caption secondary); the current project on an accent
  wash with a check mark; the warning icon on an invalid recent.
- **Project information dialog**: a content dialog with label and value rows,
  the folder with its Open folder button, the content roots and cooked content
  as lists, the Cook on change toggle, and a Close button.

## 11. Dependency Rules

- The menu bar lives in the Shell; features only contribute commands and
  panels.
- The switcher and the information dialog live in ProjectExplorer and call the
  activation coordinator through its interface.
- Aura and Controls.Menus gain general capabilities and know nothing of the
  editor.

## 12. Test Strategy

- Aura unit tests: application defaults and their precedence; a title bar
  content provider is resolved by id, and a missing one is logged; an
  `ms-appx` icon path is used as is, and an `.svg` one becomes an
  `SvgImageSource`.
- Controls.Menus UI tests: the back button collapses the expanded bar and
  focuses the menu button; `Opening` is raised by both bars with the opened
  root.
- Shell unit tests: the menus built from a registry, their order, accelerator
  text, separators and the absence of `General`; View lists `Unique` panels;
  command items use the focus context and refresh on `Opening`; `GestureText`;
  the File commands drive the document service and tabs; the presenter empties
  the menu on close; `ShowPanelAsync` recreates a closed panel, restores a
  minimized one and activates an open one.
- ProjectExplorer unit tests: the switcher's sections, the limit of five, the
  invalid recent, a switch through the coordinator with `TitleBar` and its
  cancellation, New and Open navigating the start window; the information view
  model's facts and the Cook on change setting.
- The title bar in the running editor (scenarios 1 to 10) waits for the owner's
  acceptance.

## 13. Delivery Order

This title bar is built before the History panel, whose design depends on its
View menu: first the Aura and Controls changes, then the menu bar and the
commands of decision 15, then the panel reopen, then the project switcher and
the information dialog.
