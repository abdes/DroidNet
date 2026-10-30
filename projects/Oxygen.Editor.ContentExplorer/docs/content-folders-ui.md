# Content Folders UI (Mount Points)

This document specifies the **Content Folders** pane (left pane) UI and the
refactoring tasks needed to support **mount points** consistent with the
[virtual path rules](../../Oxygen.Engine/src/Oxygen/Content/Docs/virtual_paths.md).

## Scope

- The Content Folders pane uses `DroidNet.Controls.DynamicTree` with tree item
  adapters.
- The Content Folders pane adds a `DroidNet.Controls.ToolBar` **below** the tree for
  **mount / unmount / rename** actions.
- The mount UI offers exactly three choices: **Cooked**, **Build**, **Local
  Folder**. Cooked and Build are output folder nodes, view state that is never a
  mount.

Non-goals (out of scope for this doc): drag/drop,
filtering/search inside the tree, extra configuration pages.

## Goals

- Treat mount points as first-class nodes under the project root.
- Ensure selection of folders **within authoring mount points** can be
  represented as canonical **virtual paths** (not OS paths).
- Keep mount point identity **case-sensitive** and avoid silently rewriting it.
- Ensure ViewModels and services are platform-agnostic (no direct dependency on
  Windows App SDK / WinUI controls).

## Dialogs (Implementation Constraint)

- Implementers MUST NOT manually create or manage WinUI `ContentDialog`
  instances (including `XamlRoot` ownership).
- Any modal UI required by this spec (e.g., the Local Folder mount dialog,
  confirmation prompts, validation/error messages) MUST be shown via Aura's
  dialog service (`DroidNet.Aura.Dialogs.IDialogService`).
- ViewModels remain platform-agnostic:
  - ViewModels MUST NOT reference WinUI types.
  - UI-specific interactions (showing dialogs, picking folders, starting
    in-place edit) MUST be invoked through interfaces or services that can be
    mocked in unit tests.

## UI Layout

### Left pane composition

Content Folders is a vertical stack:

1. **Tree**: `DynamicTree` showing the project root, its non-hidden folders, and
   any mounted mount points.
2. **Toolbar**: `ToolBar` placed directly under the tree.

### Toolbar

Toolbar contains three primary actions:

Required layout:

- Left: static label `Virtual folders`
- Right (aligned): `Mount`, `Unmount`, and `Rename` buttons
- Iconography: `Mount` uses icon `[I]`, `Unmount` uses icon `[U]`

Button styling requirements:

- `Mount` and `Unmount` are **icon + text** buttons.
- `Rename` is **icon-only**.
- `Rename` is the **right-most** toolbar item.
- There is a **separator** between the `Unmount` button and the `Rename` button.

- **Mount** (button). When clicked, opens a menu with three choices:
  - Output folders:
    - Cooked
    - Build
  - (separator)
  - Local Folder
- **Unmount** (button). Unmounts the currently selected mount point.
- **Rename** (icon-only button). Starts in-place edit of the currently selected
  tree item.

Enable/disable rules:

- **Unmount** is enabled only when the current selection is a _mount point node_
  (not the project root; not a folder inside a mount).
- **Rename** is enabled only when the current selection is renameable (project
  root, folder, or mount point).

### Mount choices and display names

The Mount menu uses user-friendly labels, and the mounted node label is the
**mount point name** (not the backing folder name):

- Cooked → shows folder `.cooked` (node label: `Cooked`)
- Build → shows folder `.build` (node label: `Build`)
- Local Folder → user provides a mount point name + picks a folder path (node
  label: the provided mount point name)

## Tree Model and Behavior

### Hidden folders under project root

Every folder under the project root is shown in the tree **except** folders
whose name starts with `.`.

- Dot-prefixed folders are hidden by default.
- `.cooked` and `.build` are surfaced only through their output folder nodes;
  no output folder is ever a mount.

### Node types

The tree is composed of the following adapter types:

- **Project root node** (root adapter)
  - Label: `{ProjectName} (Project Root)`
  - Children:
    - Non-hidden project folders directly under the project root (excluding
      folders that are roots of authoring mount points)
    - Mounted mount point nodes (explicit)
- **Authoring mount point node** (new adapter)
  - Backing: a `Source` mount of `ProjectContext.Mounts`, the resolved table of
    `ProjectInfo.Mounts` (persisted in `Project.oxy`)
  - Label: mount point name (e.g. `Content`, `Engine`)
  - Root folder: the mount's resolved `Location`, inside or outside the project
  - Children: folders within that mount
- **Mount point node** (new adapter)
  - Note: this represents a **virtual folder mount** (internal/utility roots
    the `.cooked` or `.build` output folder node, or an arbitrary folder
    selected by the user).
  - Label: `Cooked`, `Build`, or the user-provided mount point name
  - Children: folders within that mounted folder
- **Folder node** (existing or revised adapter)
  - Label: folder name
  - Children: subfolders

The tree remains folder-only (no file items) unless a later spec adds file
nodes.

### Mount / unmount behavior

- **Mount** adds a mount point node as a direct child of the project root.
- For **Local Folder**, Mount first collects a mount point name + folder path
  (see “Local Folder mount dialog”).
- **Unmount** removes the selected mount point node from the tree.
- Unmounting never deletes data from disk; it only removes the mount from the
  Content Folders view.

### Output folder nodes

The Cooked and Build nodes show an output folder. They are view state, persisted
in `Project.oxy` as `OutputFolderViews`, never mounts: nothing is authored or
cooked from them, and the Player never receives them as source mounts. Their
names are reserved and cannot be changed (node → folder):

- `Cooked` → `.cooked`
- `Build` → `.build`

They do not need to be auto-shown just because the folders exist on disk; they
appear when explicitly shown.

Known mounts must be mountable even when the backing folder does not exist yet.

- When mounting a known location whose backing folder is missing, the mount node
  still appears.
- Its children are empty until the folder exists (or until content is generated
  by the pipeline).

Important: `.cooked` and `.build` are not shown as regular folders
under the project root even if they exist on disk, because they are dot-prefixed
and therefore hidden by default.

### Authoring mount points (persisted)

Authoring mount points are the `Source` mounts of `ProjectInfo.Mounts`
(persisted in `Project.oxy`), bound per machine by `.mounts.json`.

- Default behavior: if the project has no mount points, it has a single default
  mount point `Content` → `Content`.
- Authoring mount points should be shown as dedicated nodes so virtual path
  mapping is stable and does not depend on filesystem casing.
- A mount a `.mounts.json` binds on this machine shows its folder beside its
  name and a `This machine` badge.

### Mounts this machine cannot read

A declared mount that no `.mounts.json` binds, or whose folder or pak file is
missing, stays in the tree as an `UnavailableMountTreeItemAdapter`: dimmed, with
`Not bound` or `Folder missing` beside its name, a caution badge whose tooltip
names the committed path and the folder looked for, and no children. Opening
the project never fails because of it.

### Mount menu

Every mount row has a context menu after its folder actions:

- **Bind to a folder…** for an unbound mount, **Override folder…** for any
  other: picks a folder, or a pak file for a `Pak` mount, and writes it to the
  project's own `.mounts.json`.
- **Remove binding** when the project's own `.mounts.json` binds a mount that
  `Project.oxy` declares.
- **Open Mounts…** opens the Content mounts dialog.

The change applies through the same mount transaction as the dialog; a failed
change restores the bindings it replaced.

### Content mounts dialog

Each row shows the mount's name and what `Project.oxy` commits for it (its
relative path, or `Placeholder`), its kind (`Source`, `Cooked`, `Pak`,
`Output`), and where it is on this machine: `Committed`, `This machine` with
the bound folder, `Not bound` or `Folder missing` with a caution, or `Always
mounted` for the engine content. A row this machine cannot read offers
**Bind…** or **Override…**; a readable one offers to change its binding, and one
the project's own `.mounts.json` binds offers to remove it. A binding from a
`.mounts.json` above the project is shown and left as it is. Apply writes
`Project.oxy` and the project's own `.mounts.json` together.

### Local Folder mount dialog

When “Local Folder” is chosen, show a modal dialog (OK/Cancel) to collect a
stable mount definition.

Implementation note:

- This dialog MUST be hosted via `IDialogService` (a `DialogSpec` without
  content whose `ViewModel` the service resolves to its view) and MUST NOT be
  implemented by manually instantiating WinUI `ContentDialog`.
- The dialog ViewModel should be UI-agnostic; the View (WinUI) is supplied by
  the app's `VmToViewConverter` (as already used by Aura's dialog service).

Dialog fields:

- **Mount point name**: text box.
- **Mount point path**: read-only text label showing the selected folder path,
  plus a button to open a folder picker.
- **Save the folder** (`MountPlacementView`, shared with the Content mounts
  dialog's add form): in `Project.oxy` as a path relative to the project,
  shared through the repository, or on this machine only, as a placeholder the
  project's own `.mounts.json` binds. A folder on the project's drive defaults
  to `Project.oxy`; one on another drive has no relative path, so it is saved
  on this machine only.

Behavior:

- **Folder picker** selects a folder and updates the path label.
- **OK** is enabled only when:
  - Mount point name is non-empty, and
  - A folder path is selected.
- On OK, the dialog returns `(Name, Path, CommittedPath)` and the mount point
  is added: a folder with a cooked index as a `Cooked` mount, any other folder
  as a `Source` mount.
- On Cancel, nothing changes.

Collision policy:

- If the chosen mount point name matches an existing mount point name
  (case-sensitive), reject the operation (keep the dialog open and show a
  validation error).
- Do not auto-rename or case-normalize mount point names.

Validation/error UI note:

- Any validation feedback that requires a modal prompt MUST also use
  `IDialogService` (no ad-hoc WinUI dialogs). Prefer inline validation within
  the dialog UI when possible, but do not introduce additional UX beyond this
  spec.

## Virtual Path Mapping (Single Source of Truth)

Content Folders selection and navigation must be expressible using canonical
virtual paths.

This applies to **authoring mount points** (and their descendants). Project
folders outside authoring mounts are still shown in the tree, but do not
participate in asset-virtual-path selection.

### Canonical virtual path rules

All virtual paths follow the
[virtual path rules](../../Oxygen.Engine/src/Oxygen/Content/Docs/virtual_paths.md),
checked through `Oxygen.Managed.Core`'s `VirtualPath`: mounts and folders are
named with its mount and segment rules, and identity is case-sensitive.

### Mapping tree nodes → virtual paths

- Project root node virtual path: `/`
- Authoring mount point node virtual path: `/{MountPointName}` (from
  `ProjectMountPoint.Name`)
- Folder node under an authoring mount virtual path:
  `/{MountPointName}/{RelativePathWithinMount}`

Virtual folder nodes (`Cooked`, `Build`, Local Folder) do not
define asset identity and are not required to map to canonical virtual paths.

Notes:

- The UI must not compare virtual paths using case-insensitive comparisons.
- UI can display OS paths (e.g. for Local Folder mounts) but must not use them
  as identity.

## Refactoring Tasks

This section lists concrete implementation tasks to bring Content Folders to
the target behavior.

### A) Introduce mount point adapters

- [x] Add a new adapter type
      `AuthoringMountPointTreeItemAdapter : TreeItemAdapter`.
  - Backed by `Source` entries of `ProjectContext.Mounts` (`ResolvedMount`), and
    represents an authoring mount root.
  - Produces canonical virtual paths rooted at `/{ResolvedMount.Name}`.

  Implementation:
  `projects/Oxygen.Editor.ContentExplorer/src/Panes/ContentFolders/AuthoringMountPointTreeItemAdapter.cs`.

- [x] Add a new adapter type
      `VirtualFolderMountTreeItemAdapter : TreeItemAdapter`.
  - Holds:
    - `MountPointName` (string, case-sensitive identity)
    - A backing root folder handle (e.g. `IFolder` from the storage provider, or
      an abstraction for non-project folders)
    - Backing folder relative path (for output folder nodes: `.cooked`,
      `.build`; for Local Folder: an absolute path)
  - Loads children by enumerating folders under the mount root.
  - Must tolerate a missing backing folder for output folder nodes (`.cooked`,
    `.build`) by showing an empty node (no children) rather than
    failing or disabling the mount.

  Implementation:
  `projects/Oxygen.Editor.ContentExplorer/src/Panes/ContentFolders/VirtualFolderMountTreeItemAdapter.cs`.
  Notes:
  - The adapter records backing-path metadata via `BackingPath` +
    `BackingPathKind` (project-relative vs absolute).

- [x] Add a new root adapter `ProjectRootTreeItemAdapter : TreeItemAdapter`.
  - Children include:
    - Folder adapters for each non-hidden folder directly under the project
      root, excluding:
      - dot-prefixed folders
      - folders that are the root of an authoring mount point
        (`ResolvedMount.Location`)
    - Authoring mount point adapters (from the `Source` mounts)
    - Virtual folder mount adapters for explicitly mounted virtual folders
  - Responsible for:
    - enumerating the project root folder and filtering out dot-prefixed folder
      names
    - projecting the `Source` mounts into authoring mount point nodes
    - maintaining the current mounted set
    - inserting/removing mount nodes on mount/unmount

  Implementation:
  `projects/Oxygen.Editor.ContentExplorer/src/Panes/ContentFolders/ProjectRootTreeItemAdapter.cs`.

  Wiring:
  - The Content Folders pane tree root is now `ProjectRootTreeItemAdapter` (see
    `projects/Oxygen.Editor.ContentExplorer/src/Panes/ContentFolders/ContentFoldersViewModel.cs`).

  UI note:
  - Because the tree uses typed `x:Bind` for thumbnail icons, separate thumbnail
    templates were added for the new adapter types (see
    `projects/Oxygen.Editor.ContentExplorer/src/Panes/ContentFolders/ContentFoldersView.xaml`
    and
    `projects/Oxygen.Editor.ContentExplorer/src/Panes/ContentFolders/ThumbnailTemplateSelector.cs`).

### B) Update the Content Folders pane view to include ToolBar

- [x] Update
      [projects/Oxygen.Editor.ContentExplorer/src/Panes/ContentFolders/ContentFoldersView.xaml](../src/Panes/ContentFolders/ContentFoldersView.xaml)
      to include a `ToolBar` below the `DynamicTree`.
  - Do not add extra UI elements beyond tree + toolbar.

- [x] Add `ToolBarButton` items for:
- [x] Add `ToolBarButton` items for:
  - Mount (with a flyout/menu offering the three mount choices)
  - Unmount
  - Rename (icon-only, right-most, separated from Unmount)

- [x] Ensure the Mount flyout menu has a separator between:
  - the output folder group (Cooked/Build), and
  - Local Folder.

### C) Add commands and state in the ViewModel

- [x] Extend `ContentFoldersViewModel` with commands.

Command design decision (MVVM idioms):

- Prefer a single _parameterized_ command for known mounts:
  `MountKnownLocationCommand(KnownVirtualFolderMount kind)`.
  - Pros:
    - Centralizes mount logic and validation.
    - Reduces the ViewModel surface area (fewer commands to test/mock/wire).
    - Matches existing patterns in the repo (parameterized `RelayCommand<T>`
      usage).
  - Cons:
    - Requires passing a parameter from the menu item (enum or value object).
    - Per-item enablement becomes parameter-dependent (still testable via
      `CanExecute(kind)`).
  - Decision: use the single parameterized command.

Concrete commands:

- [x] `MountKnownLocationCommand` (argument: `Cooked | Build`)
- [x] `MountLocalFolderCommand` (opens the Local Folder mount dialog)
- [x] `UnmountSelectedItemCommand`
- [x] `RenameSelectedItemCommand`

- [x] Add selection-derived state in `ContentFoldersViewModel` to drive
      enablement:
  - [x] `CanUnmountSelectedItem` (true only when selected item is a mount
        adapter)
  - [x] `CanRenameSelectedItem` (true when selected item is renameable)

Naming rule:

- Use “SelectedItem” terminology consistently because selection is always a tree
  item (root, mount, folder).

### D) Move selection identity to virtual paths

- [x] Change Content Folders selection emission so it produces canonical
      virtual paths (strings at first), not project-relative filesystem paths.
  - Near-term: keep `string` but ensure it always conforms to the canonical
    virtual-path rules.
  - Mid-term: migrate to a typed `VirtualPath` when it exists in
    `Oxygen.Managed.Assets.Model`.

- [x] Remove case-insensitive comparisons used for matching selected items.
  - Example: any `StringComparison.OrdinalIgnoreCase` comparisons for selection
    matching should become `StringComparison.Ordinal`.

- [x] Centralize virtual path creation by using the existing
      `Oxygen.Managed.Assets.Filesystem.VirtualPath` type.
  - Do not introduce a new “VirtualPath-like” abstraction in the editor.
  - Enhance `VirtualPath` (in Oxygen.Managed.Assets) if needed with a canonical
    virtual-path builder/validator appropriate for
    `/{MountPoint}/{RelativePath}`.

- [x] For “Local Folder” mounts: should the user be able to **rename** the mount
      point name, or is deriving it from the folder name sufficient for now?
      USER CAN RENAME MOUNT POINT (logical name not physical folder)
- [x] Should Local Folder mounts be allowed to point outside the project root
      (expected: yes)? YES

### E) Persistence (MUST)

- [x] Use the existing persisted authoring mount points from
      `ProjectInfo.Mounts` (`Project.oxy`) as the source of truth for
      authoring mounts.

- [x] Persist the mounted set for **virtual folders**
      (Cooked/Build/Local Folder) per project so they can be restored.
  - Do not store these inside `ProjectInfo.Mounts` (those are for
    authoring mounts / asset identity).
  - Persist stable identifiers (the output folder in `OutputFolderViews`;
    name + absolute path for Local Folder).

### G) `.build` rename (required)

- [x] Rename the project build output folder from `Build` to `.build` for
      consistent hidden-folder semantics.
  - [x] Update Content Folders built-in mount mapping: `Build` → `.build`.
  - [x] Update `Oxygen.Managed.Assets` to write/read `.build` wherever it
        currently uses `Build` for packaged/platform outputs.
  - [x] Update any docs/scripts/tests that reference `Build/` to `.build/`.

### F) Tests (MUST)

- [ ] Add unit tests for ViewModels and services.
  - ViewModels and services MUST NOT depend on Windows App SDK / WinUI controls.
  - Any UI interaction (folder picking, dialogs, in-place edit initiation) must
    be abstracted behind interfaces so it can be tested.

- [ ] Add unit tests for:
  - Canonical virtual path rules (leading `/`, no `//`, no `.` or `..`)
  - Mount name case-sensitivity
  - Mount/unmount updates of the root adapter’s children
  - Rename behavior routing by selected item type (project root vs folder vs
    mount)
