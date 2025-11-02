# TabStrip Reorder Strategy: Stack-Based Content Transform Design

**Date**: November 2, 2025
**Status**: Design Complete - Ready for Implementation
**Author**: Design Session with User

---

## Overview

This document specifies the **stack-based content transform reorder strategy** for TabStrip drag operations. The design eliminates collection mutation during drag, uses transforms to slide content between shells, and commits a single `Items.Move` on drop.

---

## Core Principles

1. **No Items mutation during drag**: The `Items` collection remains stable until drop.
2. **No overlay window**: Reorder is confined to the TabStrip bounds. Leaving the strip transitions to TearOut.
3. **Content transforms**: Tab content slides independently of its shell via `TranslateTransform.X`.
4. **Stack-based reversibility**: A stack tracks pushed items, enabling smooth forward/backward dragging.
5. **Single commit**: On drop, perform one `Items.Move(draggedIndex, dropIndex)`, then unwind stack and clear transforms.

---

## Terminology

- **Shell**: The wrapper Grid from ItemsRepeater template. Stays in place during drag to keep repeater layout stable. Never transformed.
- **Content**: The TabStripItem inside the wrapper Grid. This is what gets `TranslateTransform.X` applied to slide visually.
- **Dragged item**: The tab being dragged. Its TabStripItem follows the pointer via transform; its shell (wrapper Grid) stays in original position for layout.
- **Pushed item**: An adjacent tab whose TabStripItem has been translated to cover the dragged shell, leaving its own shell empty.
- **Drop index**: Current target position in `Items` where the dragged item would land if dropped now.
- **Bucket**: (Legacy term, may still appear in hit-test code) Position in the unpinned item sequence. For this design, we work directly with Items indices.

---

## State Variables (ReorderStrategy)

```csharp
// Core drag state
private bool isActive;
private DragContext? context;

// Item tracking
private int draggedItemIndex;        // Index in Items of the dragged tab
private int dropIndex;                // Current target drop index in Items
private double hotspotX;              // X offset from dragged item's left edge to pointer grab point

// Stack of pushed items (for reversibility)
private Stack<PushedItemInfo> pushedItemsStack;

// Pointer tracking (for midpoint crossing detection)
private double lastPointerX;          // Previous frame's pointer X in strip coords

// Struct to track pushed items
struct PushedItemInfo
{
    public int ItemIndex;             // Index in Items
    public double OriginalLeft;       // Item's left position before push (strip coords)
    public PushDirection Direction;   // Forward or Backward
}

enum PushDirection
{
    Forward,   // Item pushed to the right (covering dragged shell to its right)
    Backward   // Item pushed to the left (covering dragged shell to its left)
}
```

---

## Phase 1: Drag Initiation (BeginDrag → InitiateDrag)

### Trigger

User presses pointer on a TabStripItem and moves ≥ 5px (threshold exceeded).

### Sequence

1. **TabStrip.BeginDrag(TabStripItem item)**
   - Clear multi-selection per GUD-001: `SelectedItem = draggedItem`.
   - Mark container: `item.IsDragging = true` (triggers VisualState for visual feedback on shell).
   - Calculate hotspot:

     ```csharp
     var itemCoords = item.TransformToVisual(this).TransformPoint(new Point(0, 0));
     hotspotX = dragStartPoint.X - itemCoords.X;
     ```

   - Call coordinator to start session (coordinator manages screen-level cursor tracking).

2. **ReorderStrategy.InitiateDrag(context, phyInitialPoint)**
   - Store `context` (draggedItem, sourceStrip, sourceVisualItem, hotspot).
   - Initialize state:

     ```csharp
     draggedItemIndex = sourceStrip.Items.IndexOf(draggedItem);
     dropIndex = draggedItemIndex;  // Initially, drop index = original index
     pushedItemsStack = new Stack<PushedItemInfo>();
     lastPointerX = ScreenToStrip(phyInitialPoint).X;
     ```

   - Apply initial transform to dragged TabStripItem:

     ```csharp
     var stripPoint = ScreenToStrip(phyInitialPoint);
     var itemLeft = GetItemLeftPosition(draggedItemIndex);
     var draggedItemTransform = GetContentTransform(draggedItemIndex);
     draggedItemTransform.X = stripPoint.X - itemLeft - hotspotX;
     ```

   - Set `isActive = true`.

### Visual Result (Frame 0)

- **Dragged item**: Shell visible at original position; TabStripItem translated to align with pointer (appears to follow grab point).
- **Other items**: Normal, no transforms.
- **Items collection**: Unchanged (e.g., `[Tab1, Tab2(dragged), Tab3]`).
- **Stack**: Empty `[]`.
- **dropIndex**: `draggedItemIndex` (e.g., 1).

---

## Phase 2: Pointer Moves (OnDragPhysicalPositionChanged)

### Trigger

Coordinator calls `strategy.OnDragPhysicalPositionChanged(phyScreenPoint)` at ~60Hz.

### Sequence (Every Frame)

1. **Convert screen → strip coordinates**:

   ```csharp
   var stripPoint = ScreenToStrip(phyScreenPoint);
   var pointerX = stripPoint.X;
   ```

2. **Update dragged TabStripItem transform** (always):

   ```csharp
   var itemLeft = GetItemLeftPosition(draggedItemIndex);
   var draggedItemTransform = GetContentTransform(draggedItemIndex);
   draggedItemTransform.X = pointerX - itemLeft - hotspotX;
   ```

   - Dragged TabStripItem follows pointer smoothly every frame.

3. **Hit-test to find target item under pointer**:

   ```csharp
   var targetItemIndex = HitTestItemAtX(pointerX);
   ```

   - Returns the Items index of the item under the pointer, or -1 if none.
   - Logic: iterate regular items, check if `pointerX` falls within their bounds (considering current transforms and spacing).

4. **Detect midpoint crossing and update stack**:

   **Case A: Forward crossing (moving right)**:
   - Condition: Pointer crosses the midpoint of the item at `dropIndex + 1`.
   - Action: **Push** that item onto the stack.

     ```csharp
     var pushedItemIndex = dropIndex + 1;
     var pushedInfo = new PushedItemInfo
     {
         ItemIndex = pushedItemIndex,
         OriginalLeft = GetItemLeftPosition(pushedItemIndex),
         Direction = PushDirection.Forward
     };
     pushedItemsStack.Push(pushedInfo);
     ```

   - **Translate pushed TabStripItem** to cover the dragged shell:

     ```csharp
     var draggedShellLeft = GetItemLeftPosition(draggedItemIndex);
     var pushedItemTransform = GetContentTransform(pushedItemIndex);
     var offset = draggedShellLeft - pushedInfo.OriginalLeft; // Negative (slide left)
     pushedItemTransform.X = offset;
     ```

   - **Update dropIndex**:

     ```csharp
     dropIndex = pushedItemIndex; // Drop slot moves to where the pushed item was
     ```

   **Case B: Backward crossing (moving left, reversing)**:
   - Condition: Pointer crosses back over the midpoint of the top item on the stack (moving left).
   - Action: **Pop** that item from the stack.

     ```csharp
     var poppedInfo = pushedItemsStack.Pop();
     var poppedItemTransform = GetContentTransform(poppedInfo.ItemIndex);
     poppedItemTransform.X = 0; // Restore TabStripItem to its shell
     ```

   - **Update dropIndex**:

     ```csharp
     dropIndex = (pushedItemsStack.Count > 0)
         ? pushedItemsStack.Peek().ItemIndex
         : draggedItemIndex; // If stack empty, back to original
     ```

   **Case C: No crossing**:
   - Pointer moved but didn't cross any midpoint.
   - No stack change, no new transforms.
   - Only the dragged content transform updates (step 2).

5. **Store current pointer position** for next frame's crossing detection:

   ```csharp
   lastPointerX = pointerX;
   ```

### Visual Result (During Drag)

- **Dragged TabStripItem**: Tracks pointer continuously.
- **Pushed items**: TabStripItem slides to fill the dragged shell; their shells appear empty.
- **Other items**: Normal (no transforms).
- **Items collection**: Still unchanged.
- **Stack size**: Grows when moving forward, shrinks when reversing.
- **dropIndex**: Updates to reflect the current intended drop position.

---

## Phase 2: Detailed Example Walkthrough

### Setup

- Items: `[Tab1, Tab2(dragged), Tab3, Tab4]`
- draggedItemIndex = 1 (Tab2)

### Frame 0: Drag Start

- draggedItemIndex = 1
- dropIndex = 1
- Stack: `[]`
- Transforms:
  - Tab2 TabStripItem: X offset to align with pointer
  - All others: X = 0

### Frame 10: Pointer moves right, crosses Tab3 midpoint

- Hit-test: targetItemIndex = 2 (Tab3)
- Midpoint crossed **forward** → Push Tab3
- Stack: `[{ItemIndex: 2, OriginalLeft: X, Direction: Forward}]`
- Transforms:
  - Tab2 TabStripItem: follows pointer
  - Tab3 TabStripItem: X = (Tab2_shell_left - Tab3_original_left) → slides **left** to cover Tab2 shell
- dropIndex = 2
- **Visual**: Tab2 shell empty, Tab3 shell empty, Tab3 TabStripItem covering Tab2 shell

### Frame 20: Pointer continues right, crosses Tab4 midpoint

- Target = Tab4
- Midpoint crossed **forward** → Push Tab4
- Stack: `[{Tab3}, {Tab4}]`
- Transforms:
  - Tab2 TabStripItem: follows pointer
  - Tab3 TabStripItem: still covering Tab2 shell
  - Tab4 TabStripItem: slides **left** to cover Tab3 shell
- dropIndex = 3
- **Visual**: Tab2, Tab3, Tab4 shells all empty; TabStripItems cascaded left

### Frame 30: Pointer reverses left, crosses Tab4 midpoint back

- Midpoint crossed **backward** → Pop Tab4
- Stack: `[{Tab3}]`
- Transforms:
  - Tab2 TabStripItem: follows pointer
  - Tab3 TabStripItem: still covering Tab2 shell
  - Tab4 TabStripItem: X = 0 (restored to Tab4 shell)
- dropIndex = 2

### Frame 40: Pointer continues left, crosses Tab3 midpoint back

- Pop Tab3
- Stack: `[]`
- Transforms:
  - Tab2 TabStripItem: follows pointer
  - Tab3 TabStripItem: X = 0 (restored to Tab3 shell)
- dropIndex = 1 (back to original)

### Frame 50: Pointer moves further left, crosses Tab1 midpoint

- Push Tab1 **backward**
- Stack: `[{ItemIndex: 1, OriginalLeft: X, Direction: Backward}]`
- Transforms:
  - Tab2 TabStripItem: follows pointer
  - Tab1 TabStripItem: slides **right** to cover Tab2 shell
- dropIndex = 0 (before Tab1)
- **Visual**: Tab1 shell empty, Tab1 TabStripItem covering Tab2 shell (shift right)

---

## Phase 3: Drop (CompleteDrag)

### Trigger

User releases pointer. Coordinator calls `strategy.CompleteDrag(phyFinalPoint, targetStrip, targetIndex)`.

### Sequence

1. **Determine drop target**:
   - If `targetStrip == sourceStrip`: same-strip reorder (continue).
   - Else: cross-strip or abort (return false; coordinator handles tear-out).

2. **Commit the move** (same-strip):

   ```csharp
   if (dropIndex != draggedItemIndex)
   {
       suppressCollectionChangeHandling = true;
       sourceStrip.Items.Move(draggedItemIndex, dropIndex);
       suppressCollectionChangeHandling = false;
   }
   ```

   - Single `Items.Move`. Repeater relayouts to final order.

3. **Unwind stack and clear all transforms**:

   ```csharp
   while (pushedItemsStack.Count > 0)
   {
       var poppedInfo = pushedItemsStack.Pop();
       var poppedItemTransform = GetContentTransform(poppedInfo.ItemIndex);
       poppedItemTransform.X = 0;
   }

   var draggedItemTransform = GetContentTransform(draggedItemIndex);
   draggedItemTransform.X = 0;
   ```

   - All TabStripItems return to their shells.
   - After `Items.Move`, shells are in the new order, so TabStripItems align correctly.

4. **Restore visual state**:

   ```csharp
   sourceVisualItem.IsDragging = false;
   ```

5. **Reset strategy state**:

   ```csharp
   isActive = false;
   context = null;
   draggedItemIndex = -1;
   dropIndex = -1;
   pushedItemsStack.Clear();
   ```

### Visual Result (After Drop)

- Items collection in final order (e.g., `[Tab1, Tab3, Tab2]` if dropped at index 2).
- All transforms cleared (X = 0).
- TabStripItems back inside shells.
- One layout pass to settle final positions.

---

## Key Implementation Details

### 1. GetContentTransform Helper

Returns the `TranslateTransform` for the **TabStripItem** inside the wrapper.

```csharp
TranslateTransform GetContentTransform(int itemIndex)
{
    var item = sourceStrip.Items[itemIndex];
    var repeaterIndex = MapItemsIndexToRepeaterIndex(itemIndex); // Skip pinned if needed
    var wrapperGrid = sourceStrip.regularItemsRepeater.TryGetElement(repeaterIndex) as Grid;

    if (wrapperGrid == null)
        return null; // Container not realized

    // Find the TabStripItem inside the wrapper Grid
    var tabStripItem = wrapperGrid.Children.OfType<TabStripItem>().FirstOrDefault();
    if (tabStripItem == null)
        return null;

    // The TabStripItem has a TranslateTransform as RenderTransform (set in its default style)
    return tabStripItem.RenderTransform as TranslateTransform;
}
```

**CRITICAL**: We transform the **TabStripItem**, NOT the wrapper Grid.

**Why?** The wrapper Grid must stay in its layout position so ItemsRepeater never triggers a relayout. By transforming only the TabStripItem inside, the repeater's layout is stable (all wrappers stay put), but the visual content slides.

The TabStripItem's default style already declares a `TranslateTransform`:

```xml
<!-- TabStripItem default style (existing) -->
<Style TargetType="controls:TabStripItem">
    <Setter Property="RenderTransform">
        <Setter.Value>
            <TranslateTransform />
        </Setter.Value>
    </Setter>
    ...
</Style>
```

The wrapper Grid in the ItemsRepeater template stays **untransformed**:

```xml
<ItemsRepeater.ItemTemplate>
    <DataTemplate>
        <Grid>
            <!-- NO RenderTransform here - Grid stays put for layout stability -->

            <!-- TabStripItem gets transformed (RenderTransform from style) -->
            <controls:TabStripItem
                Item="{Binding}"
                Visibility="{Binding IsPlaceholder, Converter={StaticResource InverseBoolToVisibilityConverter}}" />
        </Grid>
    </DataTemplate>
</ItemsRepeater.ItemTemplate>
```

### 2. GetItemLeftPosition Helper

Returns the left edge of an item's shell in strip-relative coordinates.

```csharp
double GetItemLeftPosition(int itemIndex)
{
    var repeaterIndex = MapItemsIndexToRepeaterIndex(itemIndex);
    var wrapperGrid = sourceStrip.regularItemsRepeater.TryGetElement(repeaterIndex) as Grid;

    if (wrapperGrid == null)
    {
        // Fallback: compute from layout (sum of previous widths + spacing)
        return ComputeLeftFromLayout(itemIndex);
    }

    var itemPosition = wrapperGrid.TransformToVisual(sourceStrip).TransformPoint(new Point(0, 0));
    return itemPosition.X;
}
```

### 3. HitTestItemAtX

Returns the Items index of the item under the given X coordinate (strip-relative).

```csharp
int HitTestItemAtX(double pointerX)
{
    var regularItems = sourceStrip.Items.Where(t => !t.IsPinned).ToList();

    for (int i = 0; i < regularItems.Count; i++)
    {
        var itemIndex = sourceStrip.Items.IndexOf(regularItems[i]);
        var left = GetItemLeftPosition(itemIndex);
        var width = GetItemWidth(itemIndex);
        var right = left + width;

        if (pointerX >= left && pointerX < right)
            return itemIndex;
    }

    return -1; // Not over any item
}
```

### 4. Midpoint Crossing Detection

**Forward crossing** (moving right):

```csharp
bool IsCrossingMidpointForward(double pointerX, int targetItemIndex)
{
    if (targetItemIndex != dropIndex + 1)
        return false; // Not the next item to the right

    var targetLeft = GetItemLeftPosition(targetItemIndex);
    var targetWidth = GetItemWidth(targetItemIndex);
    var midpoint = targetLeft + (targetWidth / 2.0);

    var wasLeftOfMidpoint = lastPointerX < midpoint;
    var nowRightOfMidpoint = pointerX >= midpoint;

    return wasLeftOfMidpoint && nowRightOfMidpoint;
}
```

**Backward crossing** (moving left, reversing):

```csharp
bool IsCrossingMidpointBackward(double pointerX)
{
    if (pushedItemsStack.Count == 0)
        return false;

    var topPushedItem = pushedItemsStack.Peek();
    var itemIndex = topPushedItem.ItemIndex;

    var itemLeft = GetItemLeftPosition(itemIndex);
    var itemWidth = GetItemWidth(itemIndex);
    var midpoint = itemLeft + (itemWidth / 2.0);

    var wasRightOfMidpoint = lastPointerX >= midpoint;
    var nowLeftOfMidpoint = pointerX < midpoint;

    return wasRightOfMidpoint && nowLeftOfMidpoint;
}
```

### 5. Pushed Item Offset Calculation

**Forward push** (TabStripItem slides left to cover dragged shell):

```csharp
var draggedShellLeft = GetItemLeftPosition(draggedItemIndex);
var pushedItemLeft = GetItemLeftPosition(pushedItemIndex);
var offset = draggedShellLeft - pushedItemLeft; // Typically negative
pushedItemTransform.X = offset;
```

**Backward push** (TabStripItem slides right to cover dragged shell):

```csharp
// Same formula, but draggedShellLeft > pushedItemLeft, so offset is positive
var offset = draggedShellLeft - pushedItemLeft; // Positive
pushedItemTransform.X = offset;
```

---

## Edge Cases

### 1. Dragging to Index 0 (Before First Item)

- Pointer crosses first item's midpoint from left.
- Push first item **backward** (slides right to cover dragged shell).
- dropIndex = 0.
- On drop: `Items.Move(draggedIndex, 0)` puts dragged item first.

### 2. Dragging to End (After Last Item)

- Pointer crosses last item's midpoint from left.
- Push items forward until dropIndex = last regular index + 1.
- On drop: `Items.Move(draggedIndex, Items.Count - 1)` appends.

### 3. Pinned Items

- Reorder logic applies only to regular (unpinned) items.
- Hit-test and push/pop skip pinned partition.
- dropIndex is always within the regular item range.

### 3. Rapid Back-and-Forth

- Stack naturally handles: push, push, pop, pop.
- No accumulated error; each frame recomputes from current state.

### 4. Drop at Original Position

- dropIndex == draggedItemIndex.
- Skip `Items.Move` (no-op).
- Unwind stack (should be empty), clear transforms.
- Visual: everything returns to normal.

### 6. Container Not Realized (Virtualization)

- If `TryGetElement` returns null (container not in visual tree):
  - Fallback: use `lastLayoutResult` or `PreferredItemWidth` to compute positions.
  - Midpoint detection still works with estimated bounds.

### 7. Pointer Leaves TabStrip Bounds

- ReorderStrategy only active while pointer is over the strip.
- Leaving the strip triggers strategy cleanup and transitions to TearOut (coordinator handles).

---

## TabStrip Template Requirements

### Wrapper Grid with TranslateTransform

The ItemsRepeater template must wrap each TabStripItem in a Grid with a `TranslateTransform`:

```xml
<ItemsRepeater.ItemTemplate>
    <DataTemplate>
        <Grid>
            <Grid.RenderTransform>
                <TranslateTransform />
            </Grid.RenderTransform>

            <!-- Placeholder Border (visible when IsPlaceholder=true) -->
            <Border Visibility="{Binding IsPlaceholder, Converter={StaticResource BoolToVisibilityConverter}}" ... />

            <!-- TabStripItem (visible when IsPlaceholder=false) -->
            <controls:TabStripItem
                Item="{Binding}"
                Visibility="{Binding IsPlaceholder, Converter={StaticResource InverseBoolToVisibilityConverter}}" />
        </Grid>
    </DataTemplate>
</ItemsRepeater.ItemTemplate>
```

**Note**: The template already has this structure. The `TranslateTransform` on the wrapper Grid is the target for our X offsets.

### TabStripItem Visual States (Existing)

The `IsDragging` property triggers the "Dragging" VisualState, which applies:

- `Opacity = 0.7` on the root grid (subtle feedback).
- `ScaleTransform` to 0.95 (optional "lifting" feel).

These visual effects apply to the **TabStripItem itself**. The `TranslateTransform.X` we apply for content sliding is **independent** and applied to the same TabStripItem's `RenderTransform` property (which can be a `TransformGroup` if we need both scale and translate).

---

## Coordinate Spaces Summary

1. **Physical screen pixels**: From `GetCursorPos` or coordinator. Used by coordinator for cross-window tracking.
2. **Logical screen DIPs**: From WinRT pointer events. Converted to physical for coordinator.
3. **Strip-relative logical DIPs**: From `ScreenToStrip(phyPoint)`. Used for hit-testing and item bounds.
4. **Repeater-relative logical DIPs**: From `TransformToVisual(repeater)`. Used to get item positions within repeater.

All transforms (X offsets) are in **logical DIPs** matching the TabStrip's coordinate space.

---

## Performance Considerations

1. **Transform updates**: Very cheap. No layout pass; just composition layer updates.
2. **Stack operations**: O(1) push/pop, minimal allocation (stack grows to at most N items).
3. **Hit-testing**: O(N) scan per frame, but N is small (typically < 20 tabs visible).
4. **Collection mutation**: Only once, on drop. Repeater relayouts once to final state.
5. **No GC churn**: Reuse stack; no transient placeholder items.

---

## Accessibility and Automation

- **During drag**: Items collection stable. Screen readers see original order.
- **On drop**: One `CollectionChanged` event. Screen reader announces reorder.
- **Selection**: Cleared on drag start (per GUD-001). No churn during drag.
- **Keyboard**: Reorder only triggered by pointer drag. Keyboard reorder would need separate flow (not in scope).

---

## Testing Strategy

See `REORDER-STACK-TESTS.md` for comprehensive test case placeholders.

---

## Migration from Placeholder-Based Approach

### What Changes

- **Remove**: `InsertPlaceholderAtDraggedItemPosition`, `SwapPlaceholderWithAdjacentItem`, `RemovePlaceholder`, `CommitReorderFromPlaceholder`.
- **Remove**: `reorderPlaceholder`, `reorderPlaceholderBucketIndex` fields in TabStrip.
- **Add**: Stack-based push/pop logic in ReorderStrategy.
- **Add**: Transform helpers: `GetContentTransform`, `GetItemLeftPosition`.

### What Stays

- Screen/logical coordinate conversions.
- Layout width policies (Equal/Compact/SizeToContent).
- Coordinator workflow.
- Drag threshold detection.

### Backward Compatibility

- Old placeholder methods can be marked `[Obsolete]` and throw `NotSupportedException` with a message pointing to the new design.

---

## Open Questions / Future Work

1. **Animation**: Should pushed item content slide smoothly (Storyboard) or snap instantly?
   - **Decision**: Start with instant snap. Add animation polish later if needed.

2. **DPI scaling**: All transforms are logical; should work across mixed-DPI monitors. Verify in multi-monitor tests.

3. **RTL layouts**: Does `TranslateTransform.X` need special handling for right-to-left cultures?
   - **Decision**: Not in initial scope. Add if needed.

4. **Virtualization**: What if many items are virtualized and not realized?
   - **Mitigation**: Fallback to layout-computed positions (covered in design).

---

## Summary

This design delivers the exact UX the user specified:

- TabStripItem slides to follow the pointer.
- Adjacent TabStripItems fill the dragged shell when midpoint crossed.
- Stack enables perfect reversibility.
- Single commit on drop, no data churn during drag.

**Status**: Ready for implementation and testing.
