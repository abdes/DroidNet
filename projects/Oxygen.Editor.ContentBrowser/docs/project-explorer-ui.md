
# Project Explorer UI (Mount Points)

This document specifies the **Project Explorer** (left pane) UI and the refactoring tasks needed to support **mount points** consistent with [projects/Oxygen.Assets/docs/virtual-paths.md](../../Oxygen.Assets/docs/virtual-paths.md).

## Scope

- The Project Explorer uses `DroidNet.Controls.DynamicTree` with tree item adapters.
- The Project Explorer adds a `DroidNet.Controls.ToolBar` **below** the tree for **mount / unmount / rename** actions.
- The mount UI offers exactly four choices: **Cooked**, **Imported**, **Build**, **Local Folder**.

Non-goals (out of scope for this doc): context menus, drag/drop, filtering/search inside the tree, extra configuration pages.

## Goals

- Treat mount points as first-class nodes under the project root.
- Ensure selection of folders **within authoring mount points** can be represented as canonical **virtual paths** (not OS paths).
- Keep mount point identity **case-sensitive** and avoid silently rewriting it.
- Ensure ViewModels and services are platform-agnostic (no direct dependency on Windows App SDK / WinUI controls).

## Dialogs (Implementation Constraint)

- Implementers MUST NOT manually create or manage WinUI `ContentDialog` instances (including `XamlRoot` ownership).
- Any modal UI required by this spec (e.g., the Local Folder mount dialog, confirmation prompts, validation/error messages) MUST be shown via Aura's dialog service (`DroidNet.Aura.Dialogs.IDialogService`).
- ViewModels remain platform-agnostic:
  - ViewModels MUST NOT reference WinUI types.
  - UI-specific interactions (showing dialogs, picking folders, starting in-place edit) MUST be invoked through interfaces or services that can be mocked in unit tests.

## UI Layout

### Left pane composition

Project Explorer is a vertical stack:

1) **Tree**: `DynamicTree` showing the project root, its non-hidden folders, and any mounted mount points.
2) **Toolbar**: `ToolBar` placed directly under the tree.

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

- **Mount** (button). When clicked, opens a menu with four choices:
  - Known locations:
    - Cooked
    - Imported
    - Build
  - (separator)
  - Local Folder
- **Unmount** (button). Unmounts the currently selected mount point.
- **Rename** (icon-only button). Starts in-place edit of the currently selected tree item.

Enable/disable rules:

- **Unmount** is enabled only when the current selection is a *mount point node* (not the project root; not a folder inside a mount).
- **Rename** is enabled only when the current selection is renameable (project root, folder, or mount point).

### Mount choices and display names

The Mount menu uses user-friendly labels, and the mounted node label is the **mount point name** (not the backing folder name):

- Cooked → mounts folder `.cooked` (node label: `Cooked`)
- Imported → mounts folder `.imported` (node label: `Imported`)
- Build → mounts folder `.build` (node label: `Build`)
- Local Folder → user provides a mount point name + picks a folder path (node label: the provided mount point name)

## Tree Model and Behavior

### Hidden folders under project root

Every folder under the project root is shown in the tree **except** folders whose name starts with `.`.

- Dot-prefixed folders are hidden by default.
- They are surfaced only by explicitly mounting them (for example: `.cooked`, `.imported`, `.build`).

### Node types

The tree is composed of the following adapter types:

- **Project root node** (root adapter)
  - Label: `{ProjectName} (Project Root)`
  - Children:
    - Non-hidden project folders directly under the project root (excluding folders that are roots of authoring mount points)
    - Mounted mount point nodes (explicit)
- **Authoring mount point node** (new adapter)
  - Backing: `ProjectInfo.MountPoints` (persisted in `Project.oxy`)
  - Label: mount point name (e.g. `Content`, `Engine`)
  - Root folder: project-relative `RelativePath` from `ProjectMountPoint`
  - Children: folders within that mount
- **Mount point node** (new adapter)
  - Note: this represents a **virtual folder mount** (internal/utility roots like `.cooked`, `.imported`, `.build`, or an arbitrary folder selected by the user).
  - Label: mount point name (e.g. `Cooked`, `Imported`, `Build`, or user-provided)
  - Children: folders within that mounted folder
- **Folder node** (existing or revised adapter)
  - Label: folder name
  - Children: subfolders

The tree remains folder-only (no file items) unless a later spec adds file nodes.

### Mount / unmount behavior

- **Mount** adds a mount point node as a direct child of the project root.
- For **Local Folder**, Mount first collects a mount point name + folder path (see “Local Folder mount dialog”).
- **Unmount** removes the selected mount point node from the tree.
- Unmounting never deletes data from disk; it only removes the mount from the Project Explorer view.

### Built-in mount points

The following mounts exist as explicit, user-controlled mount points (name → backing folder):

- `Cooked` → `.cooked`
- `Imported` → `.imported`
- `Build` → `.build`

They do not need to be auto-shown just because the folders exist on disk; they appear when explicitly mounted.

Known mounts must be mountable even when the backing folder does not exist yet.

- When mounting a known location whose backing folder is missing, the mount node still appears.
- Its children are empty until the folder exists (or until content is generated by the pipeline).

Important: `.cooked`, `.imported`, and `.build` are not shown as regular folders under the project root even if they exist on disk, because they are dot-prefixed and therefore hidden by default.

### Authoring mount points (persisted)

Authoring mount points come from `ProjectInfo.MountPoints` (persisted in `Project.oxy`).

- Default behavior: if the project has no mount points, it has a single default mount point `Content` → `Content`.
- Authoring mount points should be shown as dedicated nodes so virtual path mapping is stable and does not depend on filesystem casing.

### Local Folder mount dialog

When “Local Folder” is chosen, show a modal dialog (OK/Cancel) to collect a stable mount definition.

Implementation note:

- This dialog MUST be hosted via `IDialogService` (prefer `ShowAsync(ViewModelDialogSpec ...)`) and MUST NOT be implemented by manually instantiating WinUI `ContentDialog`.
- The dialog ViewModel should be UI-agnostic; the View (WinUI) is supplied by the app's `VmToViewConverter` (as already used by Aura's dialog service).

Dialog fields:

- **Mount point name**: text box.
- **Mount point path**: read-only text label showing the selected folder path, plus a button to open a folder picker.

Behavior:

- **Folder picker** selects a folder and updates the path label.
- **OK** is enabled only when:
  - Mount point name is non-empty, and
  - A folder path is selected.
- On OK, the dialog returns `(Name, Path)` and the mount point is added.
- On Cancel, nothing changes.

Collision policy:

- If the chosen mount point name matches an existing mount point name (case-sensitive), reject the operation (keep the dialog open and show a validation error).
- Do not auto-rename or case-normalize mount point names.

Validation/error UI note:

- Any validation feedback that requires a modal prompt MUST also use `IDialogService` (no ad-hoc WinUI dialogs). Prefer inline validation within the dialog UI when possible, but do not introduce additional UX beyond this spec.

## Virtual Path Mapping (Single Source of Truth)

Project Explorer selection and navigation must be expressible using canonical virtual paths.

This applies to **authoring mount points** (and their descendants). Project folders outside authoring mounts are still shown in the tree, but do not participate in asset-virtual-path selection.

### Canonical virtual path rules

All virtual paths must follow the rules in [projects/Oxygen.Assets/docs/virtual-paths.md](../../Oxygen.Assets/docs/virtual-paths.md):

- Must start with `/`
- Uses `/` separators
- No `//`
- No `.` or `..` segments
- Case-sensitive identity

### Mapping tree nodes → virtual paths

- Project root node virtual path: `/`
- Authoring mount point node virtual path: `/{MountPointName}` (from `ProjectMountPoint.Name`)
- Folder node under an authoring mount virtual path: `/{MountPointName}/{RelativePathWithinMount}`

Virtual folder mounts (`Cooked`, `Imported`, `Build`, Local Folder) do not define asset identity and are not required to map to canonical virtual paths.

Notes:

- The UI must not compare virtual paths using case-insensitive comparisons.
- UI can display OS paths (e.g. for Local Folder mounts) but must not use them as identity.

## Refactoring Tasks

This section lists concrete implementation tasks to bring Project Explorer to the target behavior.

### A) Introduce mount point adapters

- [x] Add a new adapter type `AuthoringMountPointTreeItemAdapter : TreeItemAdapter`.
  - Backed by `ProjectInfo.MountPoints` entries (`ProjectMountPoint`), and represents an authoring mount root.
  - Produces canonical virtual paths rooted at `/{ProjectMountPoint.Name}`.

  Implementation: `projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/AuthoringMountPointTreeItemAdapter.cs`.

- [x] Add a new adapter type `VirtualFolderMountTreeItemAdapter : TreeItemAdapter`.
  - Holds:
    - `MountPointName` (string, case-sensitive identity)
    - A backing root folder handle (e.g. `IFolder` from the storage provider, or an abstraction for non-project folders)
    - Backing folder relative path (for built-ins: `.cooked`, `.imported`, `.build`; for Local Folder: an absolute path)
  - Loads children by enumerating folders under the mount root.
  - Must tolerate a missing backing folder for known mounts (`.cooked`, `.imported`, `.build`) by showing an empty node (no children) rather than failing or disabling the mount.

  Implementation: `projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/VirtualFolderMountTreeItemAdapter.cs`.
  Notes:
  - The adapter records backing-path metadata via `BackingPath` + `BackingPathKind` (project-relative vs absolute).

- [x] Add a new root adapter `ProjectRootTreeItemAdapter : TreeItemAdapter`.
  - Children include:
    - Folder adapters for each non-hidden folder directly under the project root, excluding:
      - dot-prefixed folders
      - folders that are the root of an authoring mount point (`ProjectMountPoint.RelativePath`)
    - Authoring mount point adapters (from `ProjectInfo.MountPoints`)
    - Virtual folder mount adapters for explicitly mounted virtual folders
  - Responsible for:
    - enumerating the project root folder and filtering out dot-prefixed folder names
    - projecting `ProjectInfo.MountPoints` into authoring mount point nodes
    - maintaining the current mounted set
    - inserting/removing mount nodes on mount/unmount

  Implementation: `projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/ProjectRootTreeItemAdapter.cs`.

  Wiring:
  - The Project Explorer tree root is now `ProjectRootTreeItemAdapter` (see `projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/ProjectLayoutViewModel.cs`).

  UI note:
  - Because the tree uses typed `x:Bind` for thumbnail icons, separate thumbnail templates were added for the new adapter types (see `projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/ProjectLayoutView.xaml` and `projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/ThumbnailTemplateSelector.cs`).

### B) Update the Project Explorer view to include ToolBar

- [ ] Update [projects/Oxygen.Editor.ContentBrowser/src/Panes/ProjectExplorer/ProjectLayoutView.xaml](../src/Panes/ProjectExplorer/ProjectLayoutView.xaml) to include a `ToolBar` below the `DynamicTree`.
  - Do not add extra UI elements beyond tree + toolbar.

- [ ] Add `ToolBarButton` items for:
- [ ] Add `ToolBarButton` items for:
  - Mount (with a flyout/menu offering the four mount choices)
  - Unmount
  - Rename (icon-only, right-most, separated from Unmount)

- [ ] Ensure the Mount flyout menu has a separator between:
  - the known locations group (Cooked/Imported/Build), and
  - Local Folder.

### C) Add commands and state in the ViewModel

- [ ] Extend `ProjectLayoutViewModel` with commands.

Command design decision (MVVM idioms):

- Prefer a single *parameterized* command for known mounts: `MountKnownLocationCommand(KnownVirtualFolderMount kind)`.
  - Pros:
    - Centralizes mount logic and validation.
    - Reduces the ViewModel surface area (fewer commands to test/mock/wire).
    - Matches existing patterns in the repo (parameterized `RelayCommand<T>` usage).
  - Cons:
    - Requires passing a parameter from the menu item (enum or value object).
    - Per-item enablement becomes parameter-dependent (still testable via `CanExecute(kind)`).
  - Decision: use the single parameterized command.

Concrete commands:

- `MountKnownLocationCommand` (argument: `Cooked | Imported | Build`)
- `MountLocalFolderCommand` (opens the Local Folder mount dialog)
- `UnmountSelectedItemCommand`
- `RenameSelectedItemCommand`

- [ ] Add selection-derived state in `ProjectLayoutViewModel` to drive enablement:
  - `CanUnmountSelectedItem` (true only when selected item is a mount adapter)
  - `CanRenameSelectedItem` (true when selected item is renameable)

Naming rule:

- Use “SelectedItem” terminology consistently because selection is always a tree item (root, mount, folder).

### D) Move selection identity to virtual paths

- [ ] Change Project Explorer selection emission so it produces canonical virtual paths (strings at first), not project-relative filesystem paths.
  - Near-term: keep `string` but ensure it always conforms to the canonical virtual-path rules.
  - Mid-term: migrate to a typed `VirtualPath` when it exists in `Oxygen.Assets.Model`.

- [ ] Remove case-insensitive comparisons used for matching selected items.
  - Example: any `StringComparison.OrdinalIgnoreCase` comparisons for selection matching should become `StringComparison.Ordinal`.

- [ ] Centralize virtual path creation by using the existing `Oxygen.Assets.Filesystem.VirtualPath` type.
  - Do not introduce a new “VirtualPath-like” abstraction in the editor.
  - Enhance `VirtualPath` (in Oxygen.Assets) if needed with a canonical virtual-path builder/validator appropriate for `/{MountPoint}/{RelativePath}`.

### E) Persistence (MUST)

- [ ] Use the existing persisted authoring mount points from `ProjectInfo.MountPoints` (`Project.oxy`) as the source of truth for authoring mounts.

- [ ] Persist the mounted set for **virtual folders** (Cooked/Imported/Build/Local Folder) per project so they can be restored.
  - Do not store these inside `ProjectInfo.MountPoints` (those are for authoring mounts / asset identity).
  - Persist stable identifiers (virtual folder kind for built-ins; name + absolute path for Local Folder).

### G) `.build` rename (required)

- [ ] Rename the project build output folder from `Build` to `.build` for consistent hidden-folder semantics.
  - Update Project Explorer built-in mount mapping: `Build` → `.build`.
  - Update `Oxygen.Assets` to write/read `.build` wherever it currently uses `Build` for packaged/platform outputs.
  - Update any docs/scripts/tests that reference `Build/` to `.build/`.

### F) Tests (MUST)

- [ ] Add unit tests for ViewModels and services.
  - ViewModels and services MUST NOT depend on Windows App SDK / WinUI controls.
  - Any UI interaction (folder picking, dialogs, in-place edit initiation) must be abstracted behind interfaces so it can be tested.

- [ ] Add unit tests for:
  - Canonical virtual path rules (leading `/`, no `//`, no `.` or `..`)
  - Mount name case-sensitivity
  - Mount/unmount updates of the root adapter’s children
  - Rename behavior routing by selected item type (project root vs folder vs mount)

## Open Questions / Clarifications

- For “Local Folder” mounts: should the user be able to **rename** the mount point name, or is deriving it from the folder name sufficient for now?
- Should Local Folder mounts be allowed to point outside the project root (expected: yes)?
