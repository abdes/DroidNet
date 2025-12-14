# DynamicTree Filtering Feature

## Overview

The DynamicTree control provides a filtering feature that allows dynamic, predicate-based filtering of tree items with automatic ancestor inclusion. The implementation provides:

- **Predicate-based filtering**: Define custom filter logic using `Predicate<ITreeItem>`
- **Hierarchical closure**: Matching items automatically include all ancestors in the filtered view
- **View-only projection**: Filtering never mutates `ShownItems`; all operations work on unfiltered data
- **Live updates**: Filtered view refreshes automatically when item properties change
- **Optional rendering**: Control-level toggle to render filtered or unfiltered view
- **Pre-order preservation**: Filtered items maintain tree traversal order

## Core Concepts

### Filtering vs. Shown Items

| Aspect | ShownItems | FilteredItems |
|--------|-----------|---------------|
| **Definition** | All expanded items in tree structure | Subset of `ShownItems` matching filter predicate |
| **Mutability** | Modified by expand/collapse/move operations | Read-only view; never mutated directly |
| **Selection** | All operations target `ShownItems` | Selection commands operate on `ShownItems` even when filtering enabled |
| **Indices** | Used for all move/reorder operations | Display-only; not used for mutation operations |
| **Events** | All tree events reference `ShownItems` | No filtering-specific events |

### Hierarchical Closure

When a filter predicate matches an item, the filtered view includes:

1. **The matching item itself**
2. **All ancestors** up to and including the root
3. **No siblings** unless they also match or are ancestors of matches
4. **No descendants** unless they also match the predicate

**Example Tree**:

```text
Root
├── Folder1
│   ├── File1.txt     ← matches filter
│   └── File2.md
└── Folder2
    └── File3.txt     ← matches filter
```

**Filtered Result** (predicate: `*.txt` files):

```text
Root              ← included (ancestor of matches)
├── Folder1       ← included (ancestor of File1.txt)
│   └── File1.txt ← included (matches)
└── Folder2       ← included (ancestor of File3.txt)
    └── File3.txt ← included (matches)
```

Note: `File2.md` and its parent's relationship to it are hidden.

## API Specification

### DynamicTreeViewModel Properties

#### FilterPredicate Property

```csharp
/// <summary>
/// Gets or sets the current filter predicate used by FilteredItems.
/// </summary>
/// <remarks>
/// When null, all items match.
/// </remarks>
public Predicate<ITreeItem>? FilterPredicate { get; set; }
```

#### Behavior

| Value | Effect |
|-------|--------|
| `null` (default) | All items pass filter; `FilteredItems` equals `ShownItems` |
| Non-null predicate | Only items where `predicate(item) == true` are included (with ancestors) |

#### Side Effects

- Setting `FilterPredicate` immediately calls `RefreshFiltering()`
- Raises `PropertyChanged` event
- Filtered view is recomputed lazily on next access to `FilteredItems`

#### FilteredItems Property

```csharp
/// <summary>
/// Gets a filtered view of ShownItems.
/// </summary>
/// <remarks>
/// This is a rendering/visibility projection only. It never mutates the underlying
/// ShownItems collection and must not affect operation semantics.
/// </remarks>
public IEnumerable<ITreeItem> FilteredItems { get; }
```

#### Characteristics

| Aspect | Details |
|--------|---------|
| **Type** | `IEnumerable<ITreeItem>` (backed by `FilteredObservableCollection<ITreeItem>`) |
| **Lazy creation** | First access creates the internal filtered collection |
| **Observable** | Implements `INotifyCollectionChanged`; can bind to UI |
| **Order** | Pre-order traversal (same as `ShownItems`) |
| **Thread-safety** | Not thread-safe; must access on UI thread |

### DynamicTreeViewModel Methods

#### RefreshFiltering Method

```csharp
/// <summary>
/// Recomputes the filter closure and refreshes FilteredItems.
/// </summary>
public void RefreshFiltering()
```

#### When to Call

| Scenario | Automatic? | Manual Call Needed? |
|----------|-----------|---------------------|
| `FilterPredicate` changed | ✅ Yes | No |
| Item property changed | ✅ Yes (if `FilteredItems` accessed) | No |
| `ShownItems` collection changed | ✅ Yes (if `FilteredItems` accessed) | No |
| External data model changed without property notifications | ❌ No | ✅ Yes |

### DynamicTree Control Properties

#### IsFilteringEnabled Property

```csharp
/// <summary>
/// Gets or sets a value indicating whether the control renders FilteredItems
/// instead of ShownItems.
/// </summary>
public bool IsFilteringEnabled { get; set; }
```

#### Behavior

| Value | Rendered Items | Use Case |
|-------|---------------|----------|
| `false` (default) | `ShownItems` | Normal tree view; no filtering |
| `true` | `FilteredItems` | Filtered tree view; matches predicate with ancestors |

**Important**: Setting `IsFilteringEnabled = true` when `FilterPredicate == null` renders the same items as when disabled, but with minor performance overhead.

## Implementation Details

### Filtering Algorithm

The filtering implementation uses a single-pass algorithm to compute the hierarchical closure:

#### Algorithm: Hierarchical Closure with Ancestor Inclusion

```pseudocode
Input: ShownItems (pre-order list), FilterPredicate
Output: includedItems (set of items to display)

1. Initialize:
   - include = boolean array [ShownItems.Count]
   - pathIndices = list tracking current ancestor path (index per depth level)

2. For each item in ShownItems (pre-order):
   a. Update pathIndices to reflect current tree position based on item.Depth
   b. If FilterPredicate(item) == true:
      - Mark include[item_index] = true
      - For each ancestor in pathIndices[0..depth]:
          Mark include[ancestor_index] = true

3. Build includedItems set from all items where include[index] == true

Complexity: O(n × d) where n = ShownItems.Count, d = max depth
Typical: O(n) because d is usually small (< 10)
```

### Automatic Refresh Triggers

The filtered view automatically refreshes in response to:

| Trigger | Detection Method | Refresh Scope |
|---------|-----------------|---------------|
| **ShownItems collection changes** | Subscribes to `CollectionChanged` | Full recompute of closure |
| **Item property changes** | Subscribes to `PropertyChanged` on each item | Full recompute of closure |
| **FilterPredicate changes** | Property setter | Full recompute of closure |

**Performance Consideration**: Property changes trigger full re-evaluation because any property might affect the predicate. Predicate should be fast (avoid I/O, async, or expensive computations).

### Property Change Observation

When `FilteredItems` is first accessed:

1. View subscribes to `ShownItems.CollectionChanged`
2. View subscribes to `PropertyChanged` for each item in `ShownItems` (if item implements `INotifyPropertyChanged`)
3. As items are added/removed from `ShownItems`, subscriptions are added/removed dynamically

**Cleanup**: Subscriptions are automatically removed when:

- Items are removed from `ShownItems`
- ViewModel is disposed
- `FilteredItems` collection is disposed

## Usage Patterns

### Simple Label Filter

```csharp
// Filter to show only items containing "test" in their label
viewModel.FilterPredicate = item => item.Label.Contains("test", StringComparison.OrdinalIgnoreCase);

// Enable rendering of filtered view
tree.IsFilteringEnabled = true;
```

### Property-Based Filter

```csharp
// Show only selected items (useful for "show selection" feature)
viewModel.FilterPredicate = item => item.IsSelected;
tree.IsFilteringEnabled = true;
```

### Combined Conditions

```csharp
// Show locked items in a specific folder
viewModel.FilterPredicate = item =>
    item.IsLocked && item.Label.StartsWith("Important", StringComparison.Ordinal);
tree.IsFilteringEnabled = true;
```

### Type-Based Filter

```csharp
// Show only specific types (requires type information on items)
viewModel.FilterPredicate = item =>
    item is FileTreeItem file && file.Extension == ".cs";
tree.IsFilteringEnabled = true;
```

### Clearing Filter

```csharp
// Remove filter (show all items)
viewModel.FilterPredicate = null;

// Or disable filtered rendering while keeping predicate
tree.IsFilteringEnabled = false;
```

### Search-As-You-Type

```csharp
// In a TextBox.TextChanged handler:
private void SearchBox_TextChanged(object sender, TextChangedEventArgs e)
{
    var searchText = ((TextBox)sender).Text;

    if (string.IsNullOrWhiteSpace(searchText))
    {
        viewModel.FilterPredicate = null;
        tree.IsFilteringEnabled = false;
    }
    else
    {
        viewModel.FilterPredicate = item =>
            item.Label.Contains(searchText, StringComparison.OrdinalIgnoreCase);
        tree.IsFilteringEnabled = true;
    }
}
```

## Important Constraints

### Operations Always Target ShownItems

**Critical Rule**: All tree mutation operations (move, remove, copy, cut, paste) operate on `ShownItems`, **not** `FilteredItems`.

| Operation | Target Collection | Example Impact |
|-----------|------------------|----------------|
| `MoveItemAsync` | `ShownItems` | Can move items not visible in filtered view |
| `RemoveItemAsync` | `ShownItems` | Can remove items not visible in filtered view |
| `SelectAllCommand` | `ShownItems` | Selects all items, including filtered-out ones |
| `ExpandItemAsync` | `ShownItems` | Expands items regardless of filter |

**Rationale**: Filtering is a **view-only projection**. The tree structure and operations remain consistent regardless of current filter state.

### Selection Behavior with Filtering

```csharp
// Setup: 10 items in ShownItems, 3 visible in FilteredItems
tree.IsFilteringEnabled = true;
viewModel.FilterPredicate = /* some predicate */;

// Select all
viewModel.SelectAllCommand.Execute(null);

// Result: All 10 items in ShownItems are selected, even though
// only 3 are visible in the filtered view.
Console.WriteLine(viewModel.SelectedItemsCount); // Output: 10
```

**UI Consideration**: When filtering is enabled, selected items may not be visible. This is by design to preserve selection state across filter changes.

### Index Semantics

Indices in `FilteredItems` are **not** valid for use in move/reorder operations:

```csharp
// WRONG: Using FilteredItems index
var filteredIndex = viewModel.FilteredItems.ToList().IndexOf(item);
await viewModel.MoveItemAsync(item, newParent, filteredIndex); // ❌ Wrong parent's child index

// CORRECT: Use parent's children collection
var children = await newParent.Children.ConfigureAwait(true);
var correctIndex = children.Count; // Insert at end
await viewModel.MoveItemAsync(item, newParent, correctIndex); // ✅ Correct
```

### Predicate Performance

The filter predicate is called frequently:

- Once per item during closure computation
- After every property change on any item (if filtering is active)
- After every collection change to `ShownItems`

**Best Practices**:

| Do | Don't |
|----|-------|
| Use simple property checks | Perform I/O operations |
| Cache expensive computations in item properties | Make network requests |
| Use string comparisons with `StringComparison` | Use regex without caching |
| Return quickly for non-matches | Perform deep tree traversals |

## Testing Coverage

| Test Category | Test File | Coverage |
|---------------|-----------|----------|
| **ViewModel filtering logic** | `ViewModelFilteringTests.cs` | Null predicate, matches with ancestors, no matches, property change refresh |
| **UI rendering** | `DynamicTreeFilteringTests.cs` | Disabled filtering, enabled filtering, pre-order preservation |
| **Selection with filtering** | `DynamicTreeFilteringTests.cs` | Select all operates on unfiltered items |

## Configuration & Extensibility

### FilteredObservableCollection Details

The filtering is implemented using `FilteredObservableCollection<T>` from the `DroidNet.Collections` library:

| Configuration | Value in DynamicTree | Reason |
|--------------|---------------------|---------|
| **Source collection** | `ShownItems` | Filter visible items only |
| **Filter predicate** | `item => includedItems.Contains(item)` | Closure computed separately for efficiency |
| **Relevant properties** | `null` | Manual refresh; view model controls all updates |
| **Observe source changes** | `false` | Manual subscription to `CollectionChanged` |
| **Observe item changes** | `false` | Manual subscription to `PropertyChanged` per item |

**Rationale for Manual Refresh**: Hierarchical closure must be recomputed globally, not incrementally, so automatic observation is disabled in favor of explicit control.

## Migration & Compatibility

### Upgrading Existing Code

If you have existing code binding to `ShownItems`:

**Before**:

```xml
<ItemsRepeater ItemsSource="{x:Bind ViewModel.ShownItems, Mode=OneWay}" />
```

**After** (with optional filtering):

```xml
<controls:DynamicTree
    ViewModel="{x:Bind ViewModel, Mode=OneWay}"
    IsFilteringEnabled="{x:Bind IsSearchActive, Mode=OneWay}" />
```

The control internally selects `ShownItems` or `FilteredItems` based on `IsFilteringEnabled`.

### Backward Compatibility

- `FilterPredicate` defaults to `null` (no filtering)
- `IsFilteringEnabled` defaults to `false` (render unfiltered)
- Existing bindings to `ShownItems` continue to work
- No breaking changes to mutation APIs

## Example: Search with Highlight

Combining filtering with custom item templates:

```csharp
// ViewModel
public string SearchText { get; set; } = string.Empty;

partial void OnSearchTextChanged(string value)
{
    if (string.IsNullOrWhiteSpace(value))
    {
        FilterPredicate = null;
    }
    else
    {
        FilterPredicate = item => item.Label.Contains(value, StringComparison.OrdinalIgnoreCase);
    }
}
```

```xml
<!-- XAML -->
<StackPanel>
    <TextBox Text="{x:Bind ViewModel.SearchText, Mode=TwoWay, UpdateSourceTrigger=PropertyChanged}"
             PlaceholderText="Search..." />

    <controls:DynamicTree
        ViewModel="{x:Bind ViewModel, Mode=OneWay}"
        IsFilteringEnabled="{x:Bind ViewModel.FilterPredicate, Mode=OneWay, Converter={StaticResource NullToBooleanConverter}}" />
</StackPanel>
```

## Performance Characteristics

| Metric | Complexity | Notes |
|--------|-----------|-------|
| **Closure computation** | O(n × d) | n = items, d = max depth; typically O(n) for shallow trees |
| **Property change refresh** | O(n × d) | Full recompute on any property change |
| **Collection change handling** | O(1) per item | Subscription management only |
| **Memory overhead** | O(n) | Two sets: `includedItems`, `observedItems` |
| **First access** | O(n) | Creates filtered collection, computes closure, subscribes to all items |

**Optimization Tip**: For large trees (>1000 items), consider debouncing property changes or using more specific property observation.

## Known Limitations

1. **No incremental updates**: Every refresh recomputes the entire closure. For very large trees, consider external filtering.

2. **Property changes are global**: Changing any property on any item triggers full refresh. No per-property filtering.

3. **No async predicates**: `Predicate<ITreeItem>` is synchronous. Async filtering requires external implementation.

4. **No descendant-only filtering**: Cannot filter to show "all descendants of matching items" without also showing ancestors.

5. **Filtering does not affect expansion state**: Filtered-out items remain expanded/collapsed; their state is preserved across filter changes.

## Future Enhancements

Potential improvements not currently implemented:

- **Per-property observation**: Specify which properties affect filter (reduce unnecessary refreshes)
- **Incremental closure updates**: Update closure incrementally for small changes
- **Filter statistics**: Expose counts of matching items, hidden items
- **Predicate compilation**: Cache and optimize predicate evaluation
- **Descendant expansion**: Option to auto-expand parents of matching items
