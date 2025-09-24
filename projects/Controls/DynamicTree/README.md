# DroidNet DynamicTree

The DynamicTree is a flexible and customizable tree view control for WinUI 3.
It's designed to handle dynamic and lazy-loaded data structures efficiently and
provide a rich set of features for displaying and interacting with hierarchical
information.

## Key Features

- Dynamic Data Binding: Supports binding to dynamic data models, allowing for
  real-time updates and efficient rendering.
- Lazy / On-demand Loading: Child nodes can be loaded on demand when a parent is
  expanded to optimize memory and performance for large trees.
- Customizable Appearance: Offers extensive styling options through XAML
  templates and resource dictionaries. Template parts and visual states are
  exposed for full control.
- Selection Modes: Supports `None`, `Single`, and `Multiple` selection modes
  with an internal selection model and commands for common operations.
- Expand/Collapse Handling: Asynchronous expand/collapse operations with methods
  on the ViewModel such as `ExpandItemAsync` and `CollapseItemAsync`.
- Item Add/Remove Events: ViewModel events fire for `ItemBeingAdded`,
  `ItemAdded`, `ItemBeingRemoved`, and `ItemRemoved` so you can synchronize the
  underlying model.
- Item Renaming: Supports in-place renaming UI and visual states for validation.
- Thumbnails & Template Selectors: Per-item thumbnail area with
  `ThumbnailTemplateSelector` support.

## Installation

Install the package (if published) or add the project to your solution. From
dotnet CLI:

```shell
dotnet add package DroidNet.Controls.DynamicTree
```

(or reference the local project in your solution during development.)

## Basic Usage (WinUI 3)

1. Import the control namespace in your XAML page:

    ```xml
    <Page
        ...
        xmlns:dnc="using:DroidNet.Controls"
        ...>
    ```

2. Add a `DynamicTree` to your layout and bind `ItemsSource` to the ViewModel's
   shown items. Provide an `ItemTemplate` that uses `DynamicTreeItem`:

    ```xml
    <dnc:DynamicTree x:Name="treeView"
                    ItemsSource="{Binding ShownItems}"
                    SelectionMode="Multiple"
                    ThumbnailTemplateSelector="{StaticResource MyThumbnailSelector}">
        <dnc:DynamicTree.ItemTemplate>
            <DataTemplate x:DataType="local:TreeItemAdapter">
                <dnc:DynamicTreeItem ItemAdapter="{Binding}" />
            </DataTemplate>
        </dnc:DynamicTree.ItemTemplate>
    </dnc:DynamicTree>
    ```

> **Notes:**
>
> - `ItemsSource` should be the `ShownItems` collection exposed by your
>   `DynamicTreeViewModel` (or a derived concrete implementation).
> - Each item in `ShownItems` should implement `ITreeItem` (or use
>   `TreeItemAdapter`/derived class provided in the control implementation).

## ViewModel and Item Adapter Example

The control ships with an abstract `DynamicTreeViewModel` that manages shown
items, expansion and collapse, selection state, and provides events. Derive from
it and implement your concrete item adapter.

C# (ViewModel and adapter sketch):

```csharp
public class MyDynamicTreeViewModel : DynamicTreeViewModel
{
    public MyDynamicTreeViewModel()
    {
        var root = new MyTreeItemAdapter("Root");
        _ = InitializeRootAsync(root, skipRoot: true);
    }
}

public class MyTreeItemAdapter : TreeItemAdapter
{
    public MyTreeItemAdapter(string label)
    {
        Label = label;
    }

    public override string Label { get; set; }

    protected override int DoGetChildrenCount()
    {
        // return cached/known children count
    }

    protected override async Task LoadChildren()
    {
        // load children on-demand (async), add them to internal collection
    }
}
```

> **Important points:**
>
> - Implement `LoadChildren` to perform any async work required to fetch or
>   build children. The `DynamicTreeViewModel` will call this when expanding
>   nodes.
> - Use `InitializeRootAsync` to set up the root and build the initial
>   `ShownItems` collection.

## Selection

`DynamicTreeViewModel` exposes a `SelectionMode` property (enum: `None`,
`Single`, `Multiple`). When set, an internal `SelectionModel` is created and
synchronized with shown items. Useful commands exposed via the ViewModel:

- `SelectAll` (multiple selection only)
- `SelectNone`
- `InvertSelection` (multiple selection only)
- `ToggleSelectAll` (called by the control on Ctrl+A)

The control also supports keyboard modifiers:

- Ctrl+Click to toggle selection of a single item
- Shift+Click to extend the selection
- Ctrl+A to select all (when selection mode is multiple)
- Ctrl+Shift+I to invert selection

## Events

Subscribe to ViewModel events to keep your backing model in-sync or to validate
operations:

- `ItemBeingAdded` (cancellable via `TreeItemBeingAddedEventArgs.Proceed`)
- `ItemAdded`
- `ItemBeingRemoved` (cancellable via `TreeItemBeingRemovedEventArgs.Proceed`)
- `ItemRemoved`

These events allow you to approve or veto add/remove operations and perform
model updates before and after changes.

## Expand / Collapse and Lazy Loading

Use `ExpandItemAsync(ITreeItem)` and `CollapseItemAsync(ITreeItem)` on the
ViewModel to programmatically change expansion state. The `DynamicTreeItem` and
`DynamicTree` controls forward user expansion/collapse gestures to the ViewModel
which will invoke `LoadChildren` on adapters as needed.

Example: programmatically expand an item

```csharp
await myViewModel.ExpandItemAsync(myAdapter);
```

## Templates, Visual States and Customization

`DynamicTreeItem` exposes several template parts and visual states so you can
fully restyle the control:

Template parts include (names used in templates):

- `PartThumbnailPresenter` (thumbnail ContentPresenter)
- `PartExpander` (Expander control)
- `PartContentPresenter`
- `PartInPlaceRename` (Popup for rename)
- `PartItemName` / `PartItemNameEdit`
- `PartContentGrid` / `PartRootGrid`

Visual states (groups):

- Common states: `Normal`, `PointerOver`, `Selected`, `PointerOverSelected`
- Expansion states: `Expanded`, `Collapsed`
- Has children states: `WithChildren`, `NoChildren`
- Rename validation: `NameIsValid`, `NameIsInvalid`

Override the control template in XAML to change layout, icons, and animations.
The control uses an `ItemsRepeater` internally and updates item margins based on
item depth to provide indentation.

## Thumbnail Template Selector

The tree supports a `ThumbnailTemplateSelector` property at the `DynamicTree`
level. `DynamicTreeItem` will pick up the selector from its ancestor tree
control and apply it to thumbnails. When items are recycled, the control ensures
the selected template is re-applied.

## Example: Adding and Removing Items

Use `InsertItemAsync` and `RemoveItemAsync` on the ViewModel to manipulate the
tree. The ViewModel fires events you can hook to update your underlying model.
`InsertItemAsync` will:

- Validate index
- Ensure parent is expanded (loads children if needed)
- Fire `ItemBeingAdded` (which can cancel)
- Insert in parent and shown items
- Select the new item and fire `ItemAdded`

`RemoveItemAsync` will:

- Fire `ItemBeingRemoved` (which can cancel)
- Remove children and the item itself from shown items and parent
- Update selection accordingly
- Fire `ItemRemoved`

## Tips

- Implement `ITreeItem` or derive from `TreeItemAdapter` for convenience. The
  adapter includes depth, selection and async children handling.
- Prefer lazy-loading child nodes via `LoadChildren` rather than populating
  everything upfront for very large trees.
- Handle the ViewModel events when your underlying domain model must reflect the
  changes performed by the control.
- Use `ThumbnailTemplateSelector` to pick per-item thumbnail templates (icons,
  previews).

## Samples & Demo

Look at the repository demo apps and the control sources for concrete usage
patterns. The project contains XML/XAML comments and examples showing how to
derive `DynamicTreeViewModel` and `TreeItemAdapter`.

Happy coding! 🚀
