# DynamicTree Tree Mutation Operations

## Overview

This document describes the tree mutation operations implemented in `DynamicTree`, including move/reorder, copy/cut/paste, and drag-drop functionality. The implementation provides:

- **Move operations**: Move one or multiple items to a new parent using programmatic APIs or drag-drop
- **Reorder operations**: Change item positions within the same parent
- **Clipboard operations**: Copy, cut, and paste items with visual cut state and validation
- **Drag-and-drop**: Intuitive UI-based relocation with visual drop indicators and hover-expansion
- **Validation**: Respects locked items, `CanAcceptChildren` constraints, and prevents cyclic relationships
- **Event-driven synchronization**: Before/after events allow hooking domain logic and implementing undo/redo

## Critical Constraints & Visibility Rules

### Shown vs. Not Shown

An item is **shown** if it appears in the `ShownItems` collection. This is the **central visibility constraint**:

| State | Meaning | Parent Status | Operations Allowed |
|-------|---------|---------------|-------------------|
| **Shown** | Item is in `ShownItems` | Parent is expanded AND all ancestors are expanded | Can be selected, dragged, moved |
| **Not Shown** | Item is NOT in `ShownItems` | Parent is collapsed OR ancestors are collapsed | Cannot be moved directly; invisible to user |

**Critical implication**: Move operations require that items being moved are shown (in `ShownItems`), as the selection model, UI feedback, and index calculations depend on visibility.

### Parent Expansion & Children Loading

The parent-child relationship has three distinct states:

| State | Parent.IsExpanded | Children Loaded | Parent Property | Behavior |
|-------|---|---|---|---|
| **Never expanded** | `false` | No | Children have `Parent == null` | Parent.Children await will load children on first access |
| **Expanded** | `true` | Yes | Children have `Parent == parent` | All children visible in tree; can be moved |
| **Collapsed after expansion** | `false` | Yes | Children have `Parent == parent` | Children exist in structure but not in `ShownItems`; can be moved if parent expands |

**Critical implication**: You must never assume a child's `Parent` property is already set before accessing it. Before any move operation, you must ensure the parent is expanded so children are loaded and `Parent` pointers are established.

### Selection Index Validity

The selection model stores indices into `ShownItems`. Any mutation to `ShownItems` (insert/remove/reorder) invalidates selection indices:

| Operation | Selection State | Action Required |
|-----------|---|---|
| **Single item insertion** | Any | Clear selection BEFORE computing new index; restore after insertion |
| **Single item removal** | Any | Clear selection BEFORE removal; update indices after |
| **Batch insertions** | Any | Save selected items (not indices); clear selection; recompute all indices; restore selection |
| **Batch removals** | Any | Save selected indices (descending order); clear selection; remove items; restore selection to valid index |

**Critical implication**: Never use old selection indices after mutating `ShownItems`. Always clear first, save what you need, mutate, recompute, restore.

## 1. Core Concepts

### 1.1 Move vs. Reorder

| Operation | Definition | Preserves | Notes |
|-----------|-----------|-----------|--------|
| **Reorder** | Change position of item(s) under the **same parent** | Parent, depth, hierarchy | Affects only shown-item indices within the parent |
| **Reparent** | Change the **parent** of item(s) | Item order relative to other moved items, hierarchical structure if moved as a group | Affects depth if new parent is at different depth; may create gaps in sibling order |
| **Move** | Combination of reorder and/or reparent in a single atomic operation | See above | API-level concept that encompasses both scenarios |

### 1.2 Visibility & Realizability

Before any move operation, the following must be true:

| Precondition | Must Hold | Why | How to Ensure |
|---|---|---|---|
| **Item is shown** | `ShownItems.Contains(item)` | Selection model, UI, index calculations depend on it | Throw if not shown (caller must ensure parent chain is expanded) |
| **Current parent is realized** | `item.Parent != null` | Child needs to be in parent's children collection to remove | Caller responsibility; if parent never expanded, item has no parent |
| **New parent is shown** | `ShownItems.Contains(newParent)` | Need to find insertion index in `ShownItems` | Caller responsibility; throw if not shown |
| **New parent is expanded** | `newParent.IsExpanded == true` | Need to insert into parent's children; shown children need loading | Auto-expand before insertion (matches `InsertItemAsync` pattern) |
| **New parent children are loaded** | Parent.Children has been awaited | Inserting requires knowing sibling count and order | Awaiting Parent.Children during expansion loads them |

### 1.2b Auto-Expansion of Move Targets

Move operations automatically expand the target parent if needed, improving ergonomics for drag-drop and programmatic moves.

#### Auto-Expansion Behavior

**When auto-expansion occurs**:

- Target parent is shown but collapsed: `ShownItems.Contains(newParent) && !newParent.IsExpanded`
- Expansion loads the children collection and establishes parent pointers
- Occurs once before any items are inserted (not per-item in batch moves)

**When auto-expansion does NOT occur**:

- Target parent is not shown: `!ShownItems.Contains(newParent)` → **Throws exception**
- No recursive ancestor expansion is performed
- Example: Moving to a collapsed folder inside a collapsed grandparent will fail; caller must ensure the target is visible

#### Implementation Pattern

```csharp
// Precondition: newParent must be shown
if (!this.ShownItems.Contains(newParent))
{
    throw new InvalidOperationException(
        "Target parent is not shown in the tree; expand its ancestors to make it visible");
}

// Auto-expansion: If target is shown but not expanded, expand it
if (!newParent.IsExpanded)
{
    await this.ExpandItemAsync(newParent).ConfigureAwait(true);
    // Now newParent.Children is loaded and all children have Parent set
}

// Now safe to proceed with insertion
await newParent.InsertChildAsync(index, item).ConfigureAwait(true);
```

#### Benefits & Trade-offs

| Aspect | Benefit | Trade-off |
|--------|---------|-----------|
| **User Experience** | Drag-drop onto collapsed folder auto-opens it; more intuitive | If user expands wrong folder by mistake, children become visible |
| **API Simplicity** | Caller doesn't need to manually expand target | Caller may not be aware expansion occurred; should be logged |
| **Performance** | Single operation instead of expand + move | Expansion loading children is async; adds latency to move |
| **Selection Behavior** | Expansion happens before move, so selection model remains valid | Expanded items now visible; if children were selected elsewhere, they're now shown |

#### UI Feedback During Auto-Expansion

When auto-expanding a target during drag-drop:

1. **Visual cue**: Show expanding folder animation before drop indicator disappears
2. **Logging**: Log that target was auto-expanded (debug level)
3. **Undo:**If user undoes the move, the folder should collapse back (not part of move API; caller's responsibility)

#### Multi-Item Scenario

For `MoveItemsAsync` with a collapsed target:

1. Check that target is shown
2. If target is not expanded, expand it once (before any items are moved)
3. Insert all items under the now-expanded target
4. Fire events once

No per-item expansion; the target is only expanded once at the start.

### 1.3 Multi-Item Move Semantics

When moving multiple non-contiguous items under a new parent:

- **Order Preservation**
  - Items retain their relative order from the source tree
  - Example: Move items [A, D, G] under new parent P → inserted as A, D, G in that order

- **Hierarchy Preservation**
  - If an item and its ancestor are both in the move set, they move together as a subtree
  - Example: Move [Parent, Parent.Child1, SiblingOfParent] → Parent and Child1 move as subtree under new parent

- **Index Resolution**
  - All items are inserted at the target index
  - Items after the first insertion are adjusted sequentially: index, index+1, index+2, etc.
  - Previous sibling order relationships between moved items are lost; new parent's sibling order is respected

## 2. API Specification

### 2.1 ITreeItem Interface

The `ITreeItem` interface includes the `CanAcceptChildren` property used for move validation:

```csharp
public interface ITreeItem
{
    /// <summary>
    /// Gets a value indicating whether the tree item can accept child items.
    /// </summary>
    bool CanAcceptChildren { get; }
}
```

#### Implementation Notes

| Aspect | Details |
|--------|--------|
| **Default value** | `TreeItemAdapter` base class returns `true` by default |
| **Usage** | Checked during move validation and drag-drop target validation |
| **Leaf items** | Override to return `false` for items that should never have children |
| **Dynamic behavior** | Can be implemented dynamically based on item type or current state |

### 2.2 DynamicTreeViewModel Move API

#### Method: MoveItemAsync (Single Item)

```csharp
/// <summary>
/// Moves a single tree item to a new parent at the specified index asynchronously.
/// </summary>
/// <param name="item">The item to move.</param>
/// <param name="newParent">The target parent. Must be shown and accept children.</param>
/// <param name="newIndex">
/// The zero-based index in newParent's children where the item should be inserted.
/// This is an insertion point in the parent's current children list.
/// </param>
/// <returns>A task representing the asynchronous operation.</returns>
public Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex)
```

| Phase | Actions |
|-------|--------|
| **Validation** | Verifies item and newParent are shown; checks `CanAcceptChildren`; prevents cyclic moves |
| **Event** | Fires `ItemBeingMoved` event; handlers can veto or adjust target |
| **Detachment** | Removes item from current parent's children and from `ShownItems` (with descendants) |
| **Expansion** | Auto-expands target parent if collapsed |
| **Insertion** | Inserts item into new parent's children and into `ShownItems` |
| **Selection** | Updates selection to maintain focus on moved item |
| **Completion** | Fires `ItemMoved` event with move details |

#### Method: MoveItemsAsync (Multi-Item)

```csharp
/// <summary>
/// Moves multiple items to a new parent preserving their relative order.
/// </summary>
/// <param name="items">The items to move. Must be shown and not duplicates.</param>
/// <param name="newParent">The target parent for the move.</param>
/// <param name="startIndex">The index at which the first item should be inserted.</param>
/// <returns>A task that completes when all items are moved.</returns>
public Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex)
```

| Phase | Actions |
|-------|--------|
| **Validation** | Verifies all items are shown; checks for duplicates; validates target |
| **Deduplication** | Removes descendants if their ancestors are also in the move set |
| **Events** | Fires `ItemBeingMoved` for each item; any veto cancels entire operation |
| **Detachment** | Removes all items atomically from their current parents |
| **Index Adjustment** | Adjusts `startIndex` if moving within same parent to prevent off-by-one errors |
| **Expansion** | Auto-expands target parent once (not per-item) |
| **Insertion** | Inserts all items sequentially at adjusted indices |
| **Completion** | Fires single `ItemMoved` event with all move details |

#### Method: ReorderItemAsync (Single Item)

```csharp
/// <summary>
/// Reorders a single item among its siblings asynchronously.
/// </summary>
/// <param name="item">The item whose position should change.</param>
/// <param name="newIndex">The new zero-based index among the current parent's children.</param>
/// <returns>A task that completes once the reorder finishes.</returns>
public Task ReorderItemAsync(ITreeItem item, int newIndex)
```

**Implementation**: Delegates to `MoveItemAsync(item, item.Parent, newIndex)`

#### Method: ReorderItemsAsync (Multi-Item)

```csharp
/// <summary>
/// Reorders a contiguous block of items beneath their current parent.
/// </summary>
/// <param name="items">The ordered list of items to reposition.</param>
/// <param name="startIndex">The destination index of the first item.</param>
/// <returns>A task that completes once the block reorder finishes.</returns>
public Task ReorderItemsAsync(IReadOnlyList<ITreeItem> items, int startIndex)
```

**Implementation**: Delegates to `MoveItemsAsync(items, commonParent, startIndex)` after verifying all items share the same parent

### 2.3 Event Interfaces

#### TreeItemBeingMovedEventArgs

```csharp
public class TreeItemBeingMovedEventArgs : DynamicTreeEventArgs
{
    /// <summary>
    /// Gets the parent the item currently belongs to.
    /// </summary>
    public required ITreeItem PreviousParent { get; init; }

    /// <summary>
    /// Gets or sets the proposed new parent for the item.
    /// </summary>
    public required ITreeItem NewParent { get; set; }

    /// <summary>
    /// Gets or sets the proposed index within the new parent children collection.
    /// </summary>
    /// <remarks>
    /// The index is expressed in NewParent's children collection, not in ShownItems.
    /// </remarks>
    public required int NewIndex { get; set; }

    /// <summary>
    /// Gets or sets a value indicating whether the move should proceed.
    /// </summary>
    public bool Proceed { get; set; } = true;

    /// <summary>
    /// Gets or sets an optional reason when Proceed is false.
    /// </summary>
    public string? VetoReason { get; set; }
}
```

#### TreeItemsMovedEventArgs

```csharp
public class TreeItemsMovedEventArgs : EventArgs
{
    /// <summary>
    /// Gets the collection of move descriptors representing the completed operation.
    /// </summary>
    public required IReadOnlyList<MovedItemInfo> Moves { get; init; }

    /// <summary>
    /// Gets a value indicating whether the event represents a batch move.
    /// </summary>
    public bool IsBatch => this.Moves.Count > 1;

    /// <summary>
    /// Gets the primary move information (first item in Moves).
    /// </summary>
    public MovedItemInfo PrimaryMove => this.Moves[0];
}

public sealed record MovedItemInfo(
    ITreeItem Item,
    ITreeItem PreviousParent,
    ITreeItem NewParent,
    int PreviousIndex,
    int NewIndex)
{
    /// <summary>
    /// Gets a value indicating whether the move changed the parent.
    /// </summary>
    public bool IsReparenting => !ReferenceEquals(this.PreviousParent, this.NewParent);
}
```

### DynamicTreeViewModel Events

```csharp
/// <summary>
/// Fires before an item is moved within the dynamic tree.
/// </summary>
/// <remarks>
/// Handlers may veto the move by setting Proceed to false, or adjust the target
/// parent and index via the event args.
/// </remarks>
public event EventHandler<TreeItemBeingMovedEventArgs>? ItemBeingMoved;

/// <summary>
/// Fires after one or more items have been moved within the dynamic tree.
/// </summary>
/// <remarks>
/// Even single-item moves are reported via TreeItemsMovedEventArgs.
/// Use Moves for undo/redo recording. The reported indices are child indices and
/// reflect the actual indices used.
/// </remarks>
public event EventHandler<TreeItemsMovedEventArgs>? ItemMoved;
```

### 2.4 Clipboard Operations

#### Method: CopyItemsAsync

```csharp
/// <summary>
/// Copies the provided items into the clipboard. Items must be visible in the tree.
/// </summary>
/// <param name="items">The items to copy.</param>
/// <returns>A completed task.</returns>
public async Task CopyItemsAsync(IReadOnlyList<ITreeItem> items)
```

**Behavior**: Expands selection to include all descendants (copying entire subtrees), filters to only clonable items, and stores in internal clipboard.

#### Method: CutItemsAsync

```csharp
/// <summary>
/// Cuts the provided items into the clipboard, marking them as cut. Locked items are skipped.
/// </summary>
/// <param name="items">The items to cut.</param>
/// <returns>A completed task.</returns>
public async Task CutItemsAsync(IReadOnlyList<ITreeItem> items)
```

**Behavior**: Marks items and their descendants with `IsCut = true` for visual dimming, stores top-level items (excluding descendants of other cut items) in clipboard.

#### Method: PasteItemsAsync

```csharp
/// <summary>
/// Pastes the clipboard items into the specified parent or the currently focused item.
/// </summary>
/// <param name="targetParent">Optional target parent. When null, the focused item is used.</param>
/// <param name="insertIndex">Optional insertion index under the target parent.</param>
/// <returns>A task that completes when paste is finished.</returns>
public async Task PasteItemsAsync(ITreeItem? targetParent = null, int? insertIndex = null)
```

- **Behavior Summary**

    | Clipboard State | Paste Action |
    |-----------------|-------------|
    | **Copied** | Clones items and inserts at target; originals remain unchanged |
    | **Cut** | Moves items to target using `MoveItemsAsync`; clears cut markings |
    | **Empty** | Throws `InvalidOperationException` |

- **Clipboard Properties**

    | Property | Type | Description |
    |----------|------|-------------|
    | `CurrentClipboardState` | `ClipboardState` | Current state: `Empty`, `Copied`, or `Cut` |
    | `ClipboardItems` | `IReadOnlyList<ITreeItem>` | Items currently in clipboard |
    | `IsClipboardValid` | `bool` | Whether clipboard items still exist in tree |

## 3. Drag-Drop Implementation

### 3.1 Drop Zone Detection

The control defines three drop zones for each tree item:

| Zone | Position | Indicator | Target Parent | Insert Index |
|------|----------|-----------|---------------|-------------|
| **Before** | Top 25% of item height | Line above item | Item's parent | Index of target item |
| **Inside** | Middle 50% of item height | Highlight around item | Target item itself | End of target's children |
| **After** | Bottom 25% of item height | Line below item | Item's parent | Index of target item + 1 |

**Drop Zone Constant**: `DropReorderBand = 0.25` (fraction of item height for Before/After zones)

### 3.2 Drop Target Validation

Before showing drop indicators, the control validates the target:

| Check | Purpose |
|-------|--------|
| **Not dragging onto self** | Prevents no-op |
| **Not dragging onto descendant** | Prevents cyclic relationships |
| **Target accepts children** | For Inside zone only; checks `CanAcceptChildren` |
| **Copy capability** | If Ctrl held, verifies all dragged items implement `ICanBeCloned` |
| **Common parent for reorder** | For Before/After zones, verifies all dragged items share the target's parent |

### 3.3 Hover-Expand Behavior

When dragging over a collapsed folder in the Inside zone:

| Timing | Action |
|--------|--------|
| **Immediate** | Shows drop indicator for Inside zone |
| **600ms hover** | Auto-expands the folder to reveal its children |
| **Drag leave** | Cancels hover timer; folder remains collapsed |
| **Drop** | If not yet expanded, auto-expands during move operation |

**Hover Delay Constant**: `HoverExpandDelay = TimeSpan.FromMilliseconds(600)`

### 3.4 Visual Feedback

The control uses attached properties to manage drop indicators:

```csharp
public enum DropIndicatorPosition
{
    None,
    Before,
    Inside,
    After
}

public static readonly DependencyProperty DropIndicatorProperty;
```

Item templates bind to this property to show/hide visual indicators based on the current drag state.

### 3.5 Copy vs. Move Intent

The control detects copy intent in two ways:

| Method | Detection | Visual Feedback |
|--------|-----------|----------------|
| **Ctrl key** | `IsControlKeyDown()` during drag-over | System cursor changes to copy cursor |
| **Drag flag** | Set during `DragStarting` if Alt+Drag | Maintained throughout drag session |

When copy intent is detected, drop executes `CopyItemsAsync` followed by `PasteItemsAsync` instead of `MoveItemsAsync`.

## 4. Visibility Constraints

### 4.1 Critical Visibility Rules

#### Rule 1: Only shown items can be moved

Items must be in `ShownItems` collection to be moved:

```csharp
ShownItems.Contains(item) == true
```

This is enforced by validation in move operations. The constraint exists because:

- Selection model stores indices into `ShownItems`
- UI only renders items in `ShownItems`
- Index calculations require visibility context

#### Rule 2: Target parent must be shown

The target parent must be visible (in `ShownItems`). Auto-expansion is applied only if the target is shown but collapsed. If ancestors are collapsed, the operation throws.

#### Rule 3: Selection depends on visibility

Hidden descendants (under collapsed parents) cannot be selected, so multi-item moves automatically exclude them.

### 4.2 Implication for Drag-Drop UI

| Scenario | Can Initiate Drag? | Can Drag Multiple? | Why |
|----------|---|---|---|
| User clicks visible item | ✅ Yes | If multiple visible items selected | Item is in ShownItems and rendered |
| User clicks hidden item | ❌ No (impossible) | N/A | Hidden item not rendered; no pointer event |
| User drags with parent collapsed | ❌ No | Children remain unselectable | Parent collapse hides children from selection |
| Multi-select across collapsed boundary | ❌ No (partial) | Only drag shown items | Hidden descendants cannot be selected |

**Implementation rule for drag handlers**: Filter the selected items to only those shown before attempting move:

```csharp
var itemsToMove = selectedItems
    .Where(item => this.ShownItems.Contains(item))
    .ToList();

if (itemsToMove.IsEmpty)
{
    // No items to move; cancel drag
    return false;
}

await viewModel.MoveItemsAsync(itemsToMove, dropTarget, targetIndex);
```

### 4.3 Selection Restoration

When items are moved, descendants that were selected but hidden during the move cannot have their selection easily restored. This is by design:

- If Parent and Parent.Child are both selected, and Parent is moved under a new parent, Parent remains selected
- Parent.Child remains selected only if it was visible during the move (i.e., Parent was expanded when moved)
- If Parent was collapsed, Parent.Child is still in the selection model but no longer in `ShownItems`, creating an inconsistent state

**Recommendation**: Simpler move APIs should not attempt to restore full selection. Let the first moved item be re-selected, and provide a separate selection restoration API for advanced scenarios.

## 5. Keyboard Support

The control supports keyboard shortcuts for tree manipulation:

| Shortcut | Action | Implementation |
|----------|--------|---------------|
| **Ctrl+C** | Copy selected items | Calls `CopyItemsAsync` |
| **Ctrl+X** | Cut selected items | Calls `CutItemsAsync` |
| **Ctrl+V** | Paste to focused item | Calls `PasteItemsAsync` |
| **Delete** | Remove selected items | Calls `RemoveItemAsync` for each |

Keyboard-initiated moves use the same validation and event flow as drag-drop operations.

## 6. Index Semantics

### Critical Understanding: Two Index Spaces

The API uses **child indices** (positions in `ITreeItem.Children` collections), NOT indices into `ShownItems`:

| Index Type | Used In | Coordinate Space | Example |
|------------|---------|------------------|------|
| **Child Index** | All move/reorder APIs, event args | Parent's children collection | Item is 3rd child of its parent → index = 2 |
| **Shown Index** | Internal to view model | `ShownItems` flat list | Item is 15th visible item → shown index = 14 |

### Insertion Point Semantics

The `newIndex` parameter in move operations represents an **insertion point** in the target parent's children, evaluated **before** any items are detached:

| Scenario | Behavior |
|----------|----------|
| **Move to different parent** | Index directly specifies position in target parent |
| **Move within same parent** | Index is automatically adjusted to account for removal, preventing off-by-one errors |
| **Batch move within same parent** | Internal logic detaches all items first, then recomputes insertion indices |

**Example**: Moving item at child index 2 to child index 4 within the same parent:

1. Caller specifies `newIndex = 4` (desires position 4 in final list)
2. Internal logic detects same-parent move
3. Item at index 2 is removed (indices 3+ shift down)
4. Adjusted insertion index computed based on original positions
5. Item inserted at correct position to achieve desired final order

## 7. Drag State Machine

Drag-drop is implemented using WinUI 3's drag-drop APIs:

| Event | Handler | Purpose |
|-------|---------|--------|
| `DragStarting` | `TreeItem_DragStarting` | Captures dragged items; sets data package |
| `DragOver` | `TreeItem_DragOver` | Computes drop zone; validates target; shows indicators |
| `DragLeave` | `TreeItem_DragLeave` | Clears indicators; cancels hover-expand |
| `Drop` | `TreeItem_Drop` | Executes move or copy; focuses result |

### State Management

| Field | Purpose |
|-------|--------|
| `dragOwner` | Reference to tree that initiated the drag |
| `draggedItems` | List of items being dragged |
| `dragIsCopy` | Whether drag is a copy operation |
| `hoverExpandTimer` | Timer for auto-expanding hovered folders |
| `dropIndicatorElement` | Currently highlighted drop target |

## 8. Operation Flow Summary

### Single Item Move

| Phase | Actions |
|-------|--------|
| **1. Validation** | Check item shown; check target shown and accepts children; fire `ItemBeingMoved` |
| **2. Capture State** | Record original indices; save selection state |
| **3. Detach** | Remove item from current parent; remove from `ShownItems` with descendants |
| **4. Expansion** | Auto-expand target if collapsed |
| **5. Insertion** | Insert item in target parent; insert in `ShownItems` at computed position |
| **6. Selection** | Restore selection on moved item |
| **7. Events** | Fire `ItemMoved` with move details |

### Batch Move

| Phase | Actions |
|-------|--------|
| **1. Validation** | Check all items shown; deduplicate ancestors; fire `ItemBeingMoved` for each |
| **2. Index Adjustment** | If moving within same parent, adjust `startIndex` for removal offset |
| **3. Detach** | Remove all items atomically; remove from `ShownItems` |
| **4. Expansion** | Auto-expand target once |
| **5. Insertion** | Insert all items sequentially; insert in `ShownItems` |
| **6. Selection** | Select first moved item |
| **7. Events** | Fire single `ItemMoved` with all moves |

### Drag-Drop

| Phase | Actions |
|-------|--------|
| **1. Start** | `DragStarting`: Capture selected items; set data package |
| **2. Over** | `DragOver`: Compute drop zone; validate target; show indicator; schedule hover-expand |
| **3. Leave** | `DragLeave`: Clear indicators; cancel hover-expand |
| **4. Drop** | `Drop`: Detect copy intent; call `MoveItemsAsync` or `CopyItemsAsync` + `PasteItemsAsync` |

### Copy-Paste

| Phase | Actions |
|-------|--------|
| **1. Copy** | Expand to include descendants; filter clonable items; store in clipboard |
| **2. Paste** | Clone each item recursively; insert clones at target using `InsertChildAsync` |

### Cut-Paste

| Phase | Actions |
|-------|--------|
| **1. Cut** | Mark items with `IsCut = true`; store in clipboard |
| **2. Paste** | Call `MoveItemsAsync` to relocate; clear cut markings; clear clipboard |

## 9. Implementation Status

All described features are implemented and tested:

| Feature | Status | Test Coverage |
|---------|--------|---------------|
| **Move operations** | ✅ Complete | `ViewModelMoveTests.cs` |
| **Reorder operations** | ✅ Complete | Covered in move tests |
| **Clipboard (copy/cut/paste)** | ✅ Complete | `ViewModelClipboardTests.cs` |
| **Drag-drop UI** | ✅ Complete | `DynamicTreeDragDropTests.cs` |
| **Events** | ✅ Complete | Event tests in move/clipboard tests |
| **Validation** | ✅ Complete | Test coverage for all validation rules |

## 10. API Surface

### Public Methods

```csharp
// Core move/reorder operations
public Task MoveItemAsync(ITreeItem item, ITreeItem newParent, int newIndex);
public Task MoveItemsAsync(IReadOnlyList<ITreeItem> items, ITreeItem newParent, int startIndex);
public Task ReorderItemAsync(ITreeItem item, int newIndex);
public Task ReorderItemsAsync(IReadOnlyList<ITreeItem> items, int startIndex);

// Clipboard operations
public Task CopyItemsAsync(IReadOnlyList<ITreeItem> items);
public Task CopyItemAsync(ITreeItem item);
public Task CutItemsAsync(IReadOnlyList<ITreeItem> items);
public Task CutItemAsync(ITreeItem item);
public Task PasteItemsAsync(ITreeItem? targetParent = null, int? insertIndex = null);
public Task ClearClipboardAsync();

// Clipboard properties
public ClipboardState CurrentClipboardState { get; }
public IReadOnlyList<ITreeItem> ClipboardItems { get; }
public bool IsClipboardValid { get; }
```

### Events

```csharp
public event EventHandler<TreeItemBeingMovedEventArgs>? ItemBeingMoved;
public event EventHandler<TreeItemsMovedEventArgs>? ItemMoved;
public event EventHandler? ClipboardChanged;
```

## 11. Example Usage

### Programmatic Move

```csharp
// Move a single item to end of new parent
await viewModel.MoveItemAsync(item, newParent, newParent.ChildrenCount);

// Move multiple items preserving order
await viewModel.MoveItemsAsync([item1, item2, item3], newParent, 0);

// Reorder within same parent
await viewModel.ReorderItemAsync(item, 0); // Move to top
```

### Event Handling

```csharp
// Validate moves before they occur
viewModel.ItemBeingMoved += (s, e) =>
{
    if (!DomainValidator.CanMove(e.TreeItem, e.NewParent))
    {
        e.Proceed = false;
        e.VetoReason = "Domain constraint violated";
    }
};

// Sync domain model after moves
viewModel.ItemMoved += (s, e) =>
{
    foreach (var move in e.Moves)
    {
        DomainModel.UpdateParent(move.Item, move.NewParent);
    }
};
```

### Clipboard Operations

```csharp
// Copy items
await viewModel.CopyItemsAsync(selectedItems);

// Paste to focused item
if (viewModel.CurrentClipboardState != ClipboardState.Empty)
{
    await viewModel.PasteItemsAsync(); // Uses focused item as target
}

// Cut and paste
await viewModel.CutItemsAsync(selectedItems);
// Items are now marked with IsCut = true for visual feedback
await viewModel.PasteItemsAsync(targetParent, insertIndex: 0);
// Items moved and cut markings cleared
```

## 12. Configuration Constants

| Constant | Value | Purpose |
|----------|-------|--------|
| `DropReorderBand` | `0.25` | Fraction of item height for Before/After drop zones |
| `HoverExpandDelay` | `600ms` | Delay before auto-expanding hovered folder during drag |
| `TypeAheadResetDelay` | `1000ms` | Timeout for type-ahead search reset |
