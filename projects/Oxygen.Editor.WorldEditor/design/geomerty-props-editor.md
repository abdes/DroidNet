
# Geometry Component Property Editor (Design)

## Goal

Implement a **property editor entry** for the **Geometry** component in the World Editor Inspector.

The editor exposes a single editable property:

- **Asset**: a geometry asset reference.

The UX must match existing inspector patterns (e.g., Transform editor) and integrate with:

- Multi-selection (mixed value handling)
- Undo/redo
- Engine synchronization

This document is implementation guidance only (no code).

---

## UX Requirements

### Location

- The editor appears in the Inspector’s property editor list for a selected SceneNode **only if** the node has a Geometry component.
- Rendered by the existing Inspector editors list (the bottom-pane `ItemsRepeater`) using the same patterns as Transform (e.g., `PropertiesExpander` + `PropertyCard`).

### Editor Header

- **Header**: `Geometry`
- **Description**: brief sentence describing that the component defines renderable geometry by referencing a mesh/geometry asset.

### Property: Asset

Single property row/card labeled `Asset`.

**Layout** (left to right):

1. **Thumbnail** of the currently selected geometry asset.
2. **SplitButton** showing the current asset name.

- Primary click opens the picker flyout (same as dropdown behavior).
- Secondary (split) click also opens the flyout.

**When no asset is selected**:

- Thumbnail shows an “empty” placeholder.
- SplitButton label shows a neutral placeholder (e.g., `None`).

**When multiple nodes are selected**:

- If all selected nodes have the same geometry asset: show that asset as normal.
- If geometry assets differ across selection: show an indeterminate/mixed display:
  - Thumbnail: indeterminate placeholder.
  - Button label: `Mixed`.

### Asset Picker Flyout

When the SplitButton is clicked, open a flyout containing an `ItemsView` that lists all pickable geometry assets.

**Grouping**

- Assets are presented in **two groups**, in this order:

 1. `Engine`
 2. `Content`

**Flat list inside each group**

- No nested folders UI.
- Each item layout (left → right):
  - **Thumbnail**: square preview at the left. Use an image preview when available, otherwise a themed placeholder. Selected items show the same highlight/border treatment used elsewhere in the UI.
  - **Text column**: two stacked lines to the right of the thumbnail:
    - **Primary (single-line)**: Asset name (prominent). Example: `SM_Cube`.
    - **Secondary (single-line, muted)**: asset type descriptor. Example: `Static Mesh`.

- The full path `/Engine/...` or `/Content/...` is available as a tooltip (not shown inline).

Examples:

- Left: thumbnail image
- Right (line 1): `SM_Cube`
- Right (line 2, muted): `Static Mesh`

**Selection behavior**

- Single selection.
- Selecting an item immediately applies it to the Geometry component on the current selection.
- Flyout closes on selection.

**Visual constraints**

- Use existing design system resources and theme primitives.
- Do not introduce new custom colors, fonts, or shadows.
- If a “thumbnail” cannot be a real preview image yet, use existing icon/placeholder approaches rather than inventing new styling.

---

## Data Model

### Asset Reference Value

The Geometry component stores the asset as an `AssetReference<GeometryAsset>` with a URI (`asset:///{MountPoint}/{Path}`).

The editor treats **Asset** as the editable value:

- `SelectedAssetUri: Uri?`

Derived display values:

- `SelectedAssetName: string`
- `SelectedAssetDisplayPath: string` (e.g., `/Engine/BasicShapes/Cube`)
- `IsMixed: bool` (multi-selection mixed values)

### Picker Item

Each list entry needs:

- `Name: string`
- `Uri: Uri`
- `Group: Engine | Content`
- `DisplayPath: string` (string shown in UI: `/Engine/...` or `/Content/...`)
- `ThumbnailModel: object?` (whatever the existing Thumbnail control binds to; can be a glyph/icon model until real previews exist)

### Grouped Source

The `ItemsView` should bind to a grouped view source:

- `Groups: IReadOnlyList<AssetGroup>`
  - `AssetGroup.Key: string` (e.g., `Engine`, `Content`)
  - `AssetGroup.Items: IReadOnlyList<GeometryAssetPickerItem>`

---

## Asset Sources

### Engine Group (Built-in)

Source of truth today is the generated geometry resolver.

Include these known built-in geometry URIs:

- `asset:///Engine/Generated/BasicShapes/Cube`
- `asset:///Engine/Generated/BasicShapes/Sphere`
- `asset:///Engine/Generated/BasicShapes/Plane`
- `asset:///Engine/Generated/BasicShapes/Cylinder`

Mapping to display path:

- `asset:///Engine/Generated/...` → `/Engine/...`

Rationale: the UX requirement says group is named `Engine`; internally the mount point is `Engine` and the path starts with `Generated`.

### Content Group (Project)

The formal `Content` asset resolver is currently stubbed, so the picker must rely on existing project indexing.

Preferred current source:

- Content browser indexing service (`AssetsIndexingService`) which produces `GameAsset` entries.

Filtering:

- Include only geometry/mesh assets recognized by the system (currently `AssetType.Mesh` based on file extension like `.MESH`).

URI mapping:

- Convert a `GameAsset` into an asset URI under `Content`.
- The exact mapping must be consistent with how the rest of the editor expects `asset:///Content/...` URIs to be formatted.

Display path:

- Content asset at relative path `Models/Hero.mesh` (example) displays as `/Content/Models/Hero` (extension handling depends on URI convention).

Open question (must be decided during implementation):

- Whether `asset:///Content/...` paths should include extensions or not.

---

## Apply Semantics

### Applying a Selection

When the user selects an asset from the flyout:

1. For each selected SceneNode:

- Find its `GeometryComponent`.
- Update its `Geometry.Uri`.

1. Trigger engine sync so the runtime reflects the new geometry.

### Multi-Selection

Apply is broadcast to all selected nodes.

### Mixed Values Handling

On selection change (or when the editor receives `UpdateValues`):

- Determine if the selected nodes’ geometry URIs are identical.
- If identical: display that URI.
- If not identical: set `IsMixed = true` and use mixed display.

---

## Undo/Redo Design

The Transform editor establishes the pattern:

- Editor applies changes immediately to model.
- Editor emits a message containing old and new snapshots.
- Inspector host creates undo operations and performs engine sync.

The Geometry editor should follow the same architecture.

### Message

Introduce a message analogous to `SceneNodeTransformAppliedMessage`:

- `SceneNodeGeometryAppliedMessage`
  - `Nodes: IList<SceneNode>`
  - `OldValues: IList<GeometrySnapshot>`
  - `NewValues: IList<GeometrySnapshot>`
  - `Property: string` (likely constant `Asset`)

Where `GeometrySnapshot` minimally contains:

- `UriString: string?` (or `Uri?`, but serializable snapshot is safer)

### Undo Operations

Inspector host (`SceneNodeEditorViewModel`) should:

- Begin a change set labeled like `Geometry edit (Asset)`.
- For each node:
  - Add change: `Restore Geometry ({node.Name})`.
- On restore:
  - Set `GeometryComponent.Geometry.Uri` to old URI.
  - Sync engine.
  - Push opposite redo change (`Reapply Geometry`).

### Engine Sync

The existing engine sync API supports geometry attachment:

- `ISceneEngineSync.AttachGeometryAsync(SceneNode, GeometryComponent)`

Call this after applying the new geometry and after undo/redo restores.

---

## Engine Sync Expectations / Constraints

Current engine sync derives mesh type by parsing the last segment of the geometry URI path.

Implications:

- URIs selected by the picker must end with a segment matching an engine-recognized primitive/mesh type.
- For Engine group primitives, this aligns with `Cube`, `Sphere`, etc.
- For Content assets, if the engine expects a different identifier than the file name, additional mapping may be required.

Open question:

- How Content meshes are represented in the engine today (primitive-only vs. import pipeline). If only primitives are supported, Content group may be informational until runtime support exists.

---

## Thumbnail Strategy

The requirement mandates thumbnails, but real geometry previews may not be available yet.

Phased approach:

1. **MVP**: Use existing thumbnail control with a template that renders a glyph/icon placeholder per item.

- Must remain within theme resources.

1. **Future**: Replace thumbnail content with actual preview rendering once asset previews exist.

The design should keep the thumbnail binding abstract (`ThumbnailModel`) so implementation can swap rendering later.

---

## DI / Composition Integration

The workspace uses a child container and a viewmodel-to-view converter.

Requirements for integration:

- Register the new Geometry editor ViewModel and View in the workspace child container.
- Add Geometry component type to the inspector’s property editor factory map so it appears when applicable.

---

## Error Handling & Edge Cases

- If a selected node has a Geometry component but its `Geometry.Uri` is null: treat as `None`.
- If the picker cannot enumerate Content assets (no project loaded, indexing unavailable):
  - Still show Engine group.
  - Content group can be empty.
- If an asset URI is malformed or unsupported:
  - Display name/path safely (fallback text).
  - Do not crash; selection should be ignored or treated as `None`.

---

## Accessibility

- SplitButton must be keyboard navigable.
- Flyout list must support arrow navigation and selection.
- Ensure readable text sizes via existing theme styles.

---

## Implementation Checklist (Non-Code)

- UI: PropertiesExpander section `Geometry` with a single `PropertyCard` `Asset`.
- ViewModel: multi-selection mixed value support, selected asset binding, command to apply a picked item.
- Asset list: build grouped items source with Engine + Content.
- Messaging: emit geometry-applied message with old/new snapshots.
- Host: handle message to record UndoRedo ops and call engine sync.
- DI: register view and viewmodel in the world editor workspace container.

---

## Open Questions

1. Content URI convention: should `.mesh` extension be kept or stripped?
=> Kept

2. Runtime support: can the engine attach Content meshes today, or is it currently primitives-only?
=> Only Engine basic shapes can be attached

3. Thumbnail fidelity: is there an existing asset thumbnail provider for meshes, or should we standardize a glyph placeholder until previews exist?
=> Placeholder for now.

## Tasks

### 1) Create the Geometry property editor view (UI shell)

- Add a new **property editor view** `GeometryView` (rendered by the existing Inspector editors list) using the existing `PropertiesExpander` + single `PropertyCard` pattern.
- PropertyCard label: `Asset`.
- Content: left thumbnail + `SplitButton` that opens a flyout.

- **Status:** Completed

### 2) Create the Geometry editor view-model (selection + mixed values)

- Add `GeometryViewModel : ComponentPropertyEditor`.
- Implement `UpdateValues(ICollection<SceneNode>)` to:
  - Track the current selection (`selectedItems`).
  - Compute mixed vs. uniform values for `GeometryComponent.Geometry.Uri`.
  - Expose:
    - `SelectedAssetUri` (or string form)
    - `SelectedAssetName`
    - `IsMixed` and a mixed display label (`Mixed`).

    - **Status:** Completed

### 3) Build picker data model and grouped source

- Define a picker item model containing:
  - `Name`
  - `Uri`
  - `DisplayType` (e.g., `Static Mesh`)
  - `DisplayPath` (kept for tooltip/future details view)
  - `Group` (`Engine` or `Content`)
  - `IsEnabled` (needed because Content meshes are not attachable yet)

- Define a grouped collection source:
  - Group order: `Engine` first, then `Content`.

  - **Status:** Completed

### 4) Populate Engine group (attachable)

- Add the built-in engine basic shapes (attachable):
  - `asset:///Generated/BasicShapes/Cube`
  - `asset:///Generated/BasicShapes/Sphere`
  - `asset:///Generated/BasicShapes/Plane`
  - `asset:///Generated/BasicShapes/Cylinder`

- Display requirements per item:
  - Thumbnail placeholder (until previews exist)
  - Name (e.g., `SM_Cube` or `Cube` depending on naming convention)
  - Type line: `Static Mesh`

  - **Status:** Completed

### 5) Populate Content group (discoverable, not attachable yet)

- Use `AssetsIndexingService` output to enumerate project `AssetType.Mesh` entries.
- Create URIs in the `asset:///Content/...` form and **keep the `.mesh` extension**.
- Include them under the `Content` group, but mark items:
  - `IsEnabled = false`
  - Optional tooltip/reason text: `Not supported yet` (minimal, no extra UI beyond a tooltip).

  - **Status:** Completed (items added, marked disabled)

### 6) Implement flyout + item template layout (match screenshot)

- Flyout content is an `ItemsRepeater` using the grouped source.
- Item template matches the screenshot:
  - Left square thumbnail
  - Right text column:
    - Line 1: name
    - Line 2 (muted): `Static Mesh`
- Full path `/Engine/...` or `/Content/...` is available as tooltip (not inline).

- **Status:** Completed

### 7) Apply behavior (Engine items only)

- Selecting an enabled (Engine) item:
  - Applies it to **all** selected nodes’ `GeometryComponent.Geometry.Uri`.
  - Closes the flyout.
- Selecting a disabled (Content) item:
  - Does nothing.

- **Status:** Completed

### 8) Messaging for undo/redo (Geometry applied)

- Add a new message analogous to `SceneNodeTransformAppliedMessage`:
  - `SceneNodeGeometryAppliedMessage`
  - Include per-node old/new snapshots (store URI as `string?` in the snapshot).

- In `GeometryViewModel`, when applying an Engine selection:
  - Capture old snapshots
  - Apply model changes
  - Capture new snapshots
  - Send the message (property = `Asset`).

  - **Status:** Completed

### 9) Hook into the inspector host (SceneNodeEditorViewModel)

- Update [Oxygen.Editor.World/src/Inspector/SceneNodeEditorViewModel.cs](Oxygen.Editor.World/src/Inspector/SceneNodeEditorViewModel.cs):
  - Add `GeometryComponent` to `AllPropertyEditorFactories` so the section appears.
  - Register for `SceneNodeGeometryAppliedMessage` in the constructor (mirroring Transform).
  - Implement an `OnGeometryApplied(...)` handler.

  - **Status:** Completed

### 10) Undo/redo operations + engine sync

- In `OnGeometryApplied`:
  - Start a change set `Geometry edit (Asset)`.
  - Add per-node undo operation records storing old/new URIs.

- Implement undo/redo handlers (pattern-match Transform implementation):
  - Restore old URI → call `sceneEngineSync.AttachGeometryAsync(node, geometryComponent)`.
  - Reapply new URI → call `sceneEngineSync.AttachGeometryAsync(...)`.

  - **Status:** Completed

### 11) Register view + view-model in workspace DI

- Update the WorldEditor workspace container setup to register:
  - `GeometryViewModel`
  - `GeometryView`

  - **Status:** Completed

### 12) Validation

- Build the solution.
- Manual checks:
  - Geometry editor entry appears only when a Geometry component exists.
  - Mixed selection shows `Mixed` state.
  - Engine basic shapes apply and undo/redo works.
  - Content items appear but are disabled.

  - **Status:** In progress (solution builds; manual checks pending)
