# TabStrip Reorder Strategy: Stack-Based Test Cases

**Date**: November 2, 2025
**Status**: Test Specification - Placeholders Only (No Code Yet)
**Related Design**: See `REORDER-STACK-DESIGN.md`

---

## Test Organization

Tests are organized by phase and concern:

1. **Drag Initiation Tests**: Setup, state initialization, transform application.
2. **Pointer Movement Tests**: Content tracking, hit-testing, stack operations.
3. **Midpoint Crossing Tests**: Push/pop logic, dropIndex updates, transform correctness.
4. **Drop Tests**: Commit logic, stack unwinding, transform cleanup.
5. **Edge Case Tests**: Boundaries, pinned items, rapid reversals, virtualization.
6. **Integration Tests**: Full drag-drop sequences matching real user flows.

---

## Phase 1: Drag Initiation Tests

### Test: `InitiateDrag_InitializesStateCorrectly_Async`

**Purpose**: Verify all strategy state variables are initialized on drag start.
**Assertions**:

- `isActive` is `true`
- `draggedItemIndex` equals the dragged item's index in Items
- `dropIndex` equals `draggedItemIndex` initially
- `pushedItemsStack` is empty
- `lastPointerX` is set to the initial pointer X
- `context` is stored

---

### Test: `InitiateDrag_AppliesInitialTransformToDraggedContent_Async`

**Purpose**: Verify dragged content transform aligns with pointer at start.
**Setup**:

- TabStrip with 3 items: [Tab1, Tab2, Tab3]
- Drag Tab2 from pointer at (150, 30) within the item
**Assertions**:
- Get content transform for Tab2
- Verify `transform.X` positions content so hotspot aligns with pointer
- Formula: `transform.X = (pointerX - itemLeft - hotspotX)`

---

### Test: `InitiateDrag_DoesNotMutateItemsCollection_Async`

**Purpose**: Ensure Items collection is unchanged on drag start.
**Setup**: 3-item strip
**Assertions**:

- Items.Count before drag == Items.Count after InitiateDrag
- No placeholder inserted
- Items order unchanged

---

### Test: `InitiateDrag_MarksVisualItemAsDragging_Async`

**Purpose**: Verify `IsDragging` flag is set on the container.
**Assertions**:

- `draggedVisualItem.IsDragging` is `true`
- VisualState "Dragging" is active (check via visual tree or state property)

---

### Test: `InitiateDrag_CalculatesHotspotCorrectly_Async`

**Purpose**: Verify hotspot offset is computed from item-relative grab point.
**Setup**: Drag Tab2, pointer at (75, 15) within item bounds
**Assertions**:

- `hotspotX` stored in strategy context
- Equals pointer X minus item left edge

---

### Test: `InitiateDrag_ThrowsWhenAlreadyActive_Async`

**Purpose**: Prevent double-initialization.
**Assertions**:

- Call InitiateDrag twice
- Second call throws `InvalidOperationException`

---

## Phase 2: Pointer Movement Tests

### Test: `OnDragPhysicalPositionChanged_UpdatesDraggedContentTransform_EveryFrame_Async`

**Purpose**: Verify dragged content follows pointer on every move.
**Setup**: Initiate drag, then call OnDragPhysicalPositionChanged 3 times with different X values
**Assertions**:

- After each call, dragged content transform.X matches expected offset
- Content continuously tracks pointer position

---

### Test: `OnDragPhysicalPositionChanged_HitTestsCorrectItem_UnderPointer_Async`

**Purpose**: Verify HitTestItemAtX returns correct Items index.
**Setup**:

- 4-item strip: [Tab1, Tab2(dragged), Tab3, Tab4]
- Move pointer over Tab3's bounds
**Assertions**:
- HitTestItemAtX returns Items.IndexOf(Tab3)

---

### Test: `OnDragPhysicalPositionChanged_DoesNotMutateItems_DuringMove_Async`

**Purpose**: Ensure Items unchanged during pointer movement.
**Setup**: Drag and move pointer across multiple items
**Assertions**:

- Items.Count stable
- No Items.Move calls
- No placeholder inserted

---

### Test: `OnDragPhysicalPositionChanged_IgnoresMove_WhenStrategyNotActive_Async`

**Purpose**: Verify no-op if called when isActive is false.
**Assertions**:

- Call OnDragPhysicalPositionChanged without InitiateDrag
- No exception thrown
- No transforms applied

---

## Phase 3: Midpoint Crossing Tests

### Test: `CrossMidpoint_Forward_PushesAdjacentItem_OntoStack_Async`

**Purpose**: Verify forward midpoint crossing pushes item onto stack.
**Setup**:

- 3-item strip: [Tab1, Tab2(dragged), Tab3]
- Initiate drag at Tab2
- Move pointer right to cross Tab3's midpoint
**Assertions**:
- `pushedItemsStack.Count` == 1
- Top of stack is `{ItemIndex: 2, Direction: Forward}`
- `dropIndex` == 2

---

### Test: `CrossMidpoint_Forward_TranslatesPushedContent_ToCoverDraggedShell_Async`

**Purpose**: Verify pushed item's content slides left to cover dragged shell.
**Setup**: Same as above
**Assertions**:

- Get content transform for Tab3
- `transform.X` is negative (slides left)
- Equals `(Tab2_shell_left - Tab3_original_left)`

---

### Test: `CrossMidpoint_Backward_PopsItemFromStack_Async`

**Purpose**: Verify reversing pops top item from stack.
**Setup**:

- Push Tab3, then move pointer back left to cross Tab3 midpoint backward
**Assertions**:
- `pushedItemsStack.Count` == 0 (popped)
- `dropIndex` returns to original (draggedItemIndex)

---

### Test: `CrossMidpoint_Backward_RestoresPoppedContent_ToOriginalPosition_Async`

**Purpose**: Verify popped item's content returns to its shell.
**Setup**: Same as above
**Assertions**:

- Get content transform for Tab3
- `transform.X` == 0 (restored)

---

### Test: `CrossMidpoint_Forward_UpdatesDropIndex_Correctly_Async`

**Purpose**: Verify dropIndex increments when pushing forward.
**Setup**:

- Drag Tab1, push Tab2, then push Tab3
**Assertions**:
- After pushing Tab2: `dropIndex` == 1
- After pushing Tab3: `dropIndex` == 2

---

### Test: `CrossMidpoint_Backward_UpdatesDropIndex_Correctly_Async`

**Purpose**: Verify dropIndex decrements when popping.
**Setup**: Push Tab2 and Tab3, then pop both
**Assertions**:

- After popping Tab3: `dropIndex` == 1
- After popping Tab2: `dropIndex` == 0

---

### Test: `CrossMidpoint_Forward_MultiplePushes_StacksCorrectly_Async`

**Purpose**: Verify stack grows with multiple forward crosses.
**Setup**:

- 5-item strip
- Drag first item, cross midpoints of items 2, 3, 4
**Assertions**:
- `pushedItemsStack.Count` == 3
- Stack contains items in LIFO order: [Item2, Item3, Item4]

---

### Test: `CrossMidpoint_Backward_MultiplePops_StacksCorrectly_Async`

**Purpose**: Verify stack shrinks with multiple backward crosses.
**Setup**: Push 3 items, then reverse and pop all 3
**Assertions**:

- After each pop, stack size decreases
- Finally, stack is empty

---

### Test: `CrossMidpoint_RapidBackAndForth_MaintainsCorrectStack_Async`

**Purpose**: Verify stack integrity with rapid direction changes.
**Setup**: Push, push, pop, push, pop, pop
**Assertions**:

- Stack size matches expected at each step
- Final dropIndex correct

---

### Test: `CrossMidpoint_NoChange_WhenPointerStaysWithinSameItem_Async`

**Purpose**: Verify no push/pop if pointer doesn't cross midpoint.
**Setup**: Move pointer within an item's bounds without crossing midpoint
**Assertions**:

- Stack unchanged
- dropIndex unchanged
- No new transforms applied (except dragged content tracking)

---

## Phase 4: Drop Tests

### Test: `CompleteDrag_CommitsSingleMove_WhenDropIndexDiffers_Async`

**Purpose**: Verify Items.Move called once on drop.
**Setup**:

- Drag Tab2, push Tab3, drop at index 2
**Assertions**:
- `Items.Move(1, 2)` called exactly once
- Final Items order: [Tab1, Tab3, Tab2]

---

### Test: `CompleteDrag_SkipsMove_WhenDropIndexUnchanged_Async`

**Purpose**: Verify no Items.Move if dropped at original position.
**Setup**: Drag and immediately drop without moving
**Assertions**:

- No Items.Move called
- Items order unchanged

---

### Test: `CompleteDrag_UnwindsStack_AndClearsAllTransforms_Async`

**Purpose**: Verify stack is popped and all transforms cleared on drop.
**Setup**: Drag with 2 pushed items, then drop
**Assertions**:

- `pushedItemsStack.Count` == 0 after drop
- All content transforms.X == 0
- Dragged content transform.X == 0

---

### Test: `CompleteDrag_RestoresDraggedVisualItemState_Async`

**Purpose**: Verify `IsDragging` flag is cleared on drop.
**Assertions**:

- `draggedVisualItem.IsDragging` is `false` after CompleteDrag
- VisualState "NotDragging" is active

---

### Test: `CompleteDrag_ResetsStrategyState_Async`

**Purpose**: Verify all internal state is cleared.
**Assertions**:

- `isActive` is `false`
- `draggedItemIndex` == -1
- `dropIndex` == -1
- `context` is null
- `pushedItemsStack` is empty

---

### Test: `CompleteDrag_AllowsReuse_AfterDrop_Async`

**Purpose**: Verify strategy can be reused for a second drag.
**Setup**: Complete one drag-drop, then initiate another
**Assertions**:

- Second InitiateDrag succeeds
- State correctly initialized for second drag

---

### Test: `CompleteDrag_HandlesAbort_WhenTargetStripIsNull_Async`

**Purpose**: Verify cleanup when drag is aborted (no target strip).
**Setup**: InitiateDrag, then CompleteDrag with targetStrip=null
**Assertions**:

- No Items.Move
- Stack unwound
- Transforms cleared
- `isActive` false

---

### Test: `CompleteDrag_HandlesCrossStripDrop_ByReturningFalse_Async`

**Purpose**: Verify cross-strip drop is rejected (coordinator handles).
**Setup**: CompleteDrag with targetStrip != sourceStrip
**Assertions**:

- CompleteDrag returns `false`
- Stack cleared
- Transforms cleared
- Strategy deactivated

---

## Phase 5: Edge Case Tests

### Test: `DragToIndex0_PushesFirstItemBackward_Async`

**Purpose**: Verify dragging before first item works.
**Setup**:

- Drag Tab2, move left to push Tab1 backward
**Assertions**:
- Tab1 pushed onto stack with `Direction.Backward`
- Tab1 content slides **right** to cover Tab2 shell
- `dropIndex` == 0

---

### Test: `DragToEnd_PushesItemsForward_ToLastSlot_Async`

**Purpose**: Verify dragging to end works.
**Setup**: Drag Tab1, push all items forward until dropIndex == Items.Count - 1
**Assertions**:

- All items pushed forward
- dropIndex at last position

---

### Test: `DragWithPinnedItems_SkipsPinnedPartition_Async`

**Purpose**: Verify pinned items are not affected by reorder.
**Setup**:

- Strip with 2 pinned and 3 regular items: [Pinned1, Pinned2, Tab1, Tab2, Tab3]
- Drag Tab2
**Assertions**:
- Hit-test only considers regular items
- Stack only contains regular items
- dropIndex is within regular range (not pinned)

---

### Test: `DragWithPinnedItems_DoesNotPushPinnedItems_Async`

**Purpose**: Verify pinned items are never pushed onto stack.
**Setup**: Same as above, drag Tab1 and cross Tab2 midpoint
**Assertions**:

- Stack contains only Tab2 (regular item)
- Pinned1 and Pinned2 not in stack

---

### Test: `PointerLeavesStripBounds_AbortsReorder_Async`

**Purpose**: Verify leaving strip bounds deactivates strategy.
**Setup**: Drag within strip, then move pointer outside strip bounds
**Assertions**:

- Strategy deactivated
- Coordinator transitions to TearOut (mock/verify coordinator call)

---

### Test: `ContainerNotRealized_FallsBackToLayoutWidth_Async`

**Purpose**: Verify fallback when TryGetElement returns null.
**Setup**: Mock TryGetElement to return null for one item
**Assertions**:

- GetItemLeftPosition uses fallback computation
- Hit-test still works with estimated bounds

---

### Test: `RapidReversals_MaintainCorrectTransforms_Async`

**Purpose**: Verify no accumulated error with rapid direction changes.
**Setup**: Push 3 items, pop 2, push 1, pop 1
**Assertions**:

- Final transforms match expected (only remaining pushed item has offset)
- No stale transforms on other items

---

### Test: `StackOverflow_DoesNotOccur_WithManyPushes_Async`

**Purpose**: Verify stack handles large number of pushes.
**Setup**: Strip with 50 items, drag and push all forward
**Assertions**:

- Stack.Count == 49 (all items except dragged)
- No exception
- Performance acceptable

---

### Test: `MidpointDetection_WorksWithVariableWidths_Async`

**Purpose**: Verify midpoint logic with SizeToContent policy (variable widths).
**Setup**: Strip with items of different widths (100px, 150px, 200px)
**Assertions**:

- Midpoint computed correctly for each item
- Push triggers at correct X position

---

### Test: `MidpointDetection_WorksWithEqualWidths_Async`

**Purpose**: Verify midpoint logic with Equal policy (all same width).
**Setup**: Strip with TabWidthPolicy.Equal, all items 160px
**Assertions**:

- Midpoints at regular intervals (160/2 = 80px from each left edge)
- Push triggers consistently

---

## Phase 6: Integration Tests (Full Sequences)

### Test: `FullSequence_DragForward_TwoPositions_AndDrop_Async`

**Purpose**: End-to-end test of forward drag and drop.
**Setup**: [Tab1, Tab2(drag), Tab3, Tab4]
**Actions**:

1. InitiateDrag on Tab2
2. Move right, cross Tab3 midpoint (push Tab3)
3. Move right, cross Tab4 midpoint (push Tab4)
4. Drop at index 3
**Assertions**:

- After step 2: stack = [Tab3], dropIndex = 2
- After step 3: stack = [Tab3, Tab4], dropIndex = 3
- After drop: Items = [Tab1, Tab3, Tab4, Tab2], stack empty, all transforms cleared

---

### Test: `FullSequence_DragForward_ThenBackward_AndDrop_Async`

**Purpose**: Forward, reverse, then drop.
**Setup**: [Tab1, Tab2(drag), Tab3, Tab4]
**Actions**:

1. InitiateDrag on Tab2
2. Move right, push Tab3
3. Move right, push Tab4
4. Move left, pop Tab4
5. Drop at index 2
**Assertions**:

- After step 4: stack = [Tab3], dropIndex = 2
- After drop: Items = [Tab1, Tab3, Tab2, Tab4]

---

### Test: `FullSequence_DragBackward_ToIndex0_AndDrop_Async`

**Purpose**: Drag to beginning.
**Setup**: [Tab1, Tab2, Tab3(drag), Tab4]
**Actions**:

1. Drag Tab3
2. Move left, push Tab2
3. Move left, push Tab1
4. Drop at index 0
**Assertions**:

- After step 3: stack = [Tab2, Tab1], dropIndex = 0
- After drop: Items = [Tab3, Tab1, Tab2, Tab4]

---

### Test: `FullSequence_DragForward_ToEnd_AndDrop_Async`

**Purpose**: Drag to end.
**Setup**: [Tab1(drag), Tab2, Tab3, Tab4]
**Actions**:

1. Drag Tab1
2. Push Tab2, Tab3, Tab4
3. Drop at end
**Assertions**:

- dropIndex = 3 (after Tab4)
- Final Items = [Tab2, Tab3, Tab4, Tab1]

---

### Test: `FullSequence_DragAndReturnToOriginal_AndDrop_Async`

**Purpose**: Drag out and back, drop at start position.
**Setup**: [Tab1, Tab2(drag), Tab3]
**Actions**:

1. Drag Tab2
2. Push Tab3
3. Pop Tab3 (move back)
4. Drop at original index 1
**Assertions**:

- Items unchanged: [Tab1, Tab2, Tab3]
- No Items.Move called

---

### Test: `FullSequence_RapidBackAndForth_MultipleItems_AndDrop_Async`

**Purpose**: Stress test with rapid direction changes.
**Setup**: [Tab1, Tab2(drag), Tab3, Tab4, Tab5]
**Actions**:

1. Drag Tab2
2. Push Tab3, Tab4
3. Pop Tab4
4. Push Tab4, Tab5
5. Pop Tab5, Tab4, Tab3
6. Push Tab1 (backward)
7. Drop at index 0
**Assertions**:

- Stack operations match: 2 forward, 1 back, 2 forward, 3 back, 1 backward
- Final Items = [Tab2, Tab1, Tab3, Tab4, Tab5]

---

## Phase 7: Helper Method Unit Tests

### Test: `GetContentTransform_ReturnsCorrectTransform_ForRealizedContainer_Async`

**Purpose**: Verify helper retrieves transform from wrapper Grid.
**Setup**: Realized item in repeater
**Assertions**:

- Returns TranslateTransform instance
- Same instance returned on subsequent calls

---

### Test: `GetContentTransform_ReturnsNull_ForUnrealizedContainer_Async`

**Purpose**: Verify fallback when container not in visual tree.
**Setup**: Item not realized (virtualized)
**Assertions**:

- Returns null (or fallback transform)

---

### Test: `GetItemLeftPosition_ReturnsCorrectX_ForRealizedContainer_Async`

**Purpose**: Verify item bounds calculation.
**Setup**: Item at known position
**Assertions**:

- Returned X matches expected strip-relative coordinate

---

### Test: `GetItemLeftPosition_UsesFallback_WhenContainerNotRealized_Async`

**Purpose**: Verify layout-based fallback.
**Setup**: Mock TryGetElement to return null
**Assertions**:

- Returns computed position (sum of previous widths + spacing)

---

### Test: `HitTestItemAtX_ReturnsCorrectIndex_ForPointerOverItem_Async`

**Purpose**: Verify hit-test accuracy.
**Setup**: Pointer at (250, 30), item at left=200, width=100
**Assertions**:

- Returns correct Items index

---

### Test: `HitTestItemAtX_ReturnsMinusOne_ForPointerOutsideBounds_Async`

**Purpose**: Verify hit-test miss.
**Setup**: Pointer outside all item bounds
**Assertions**:

- Returns -1

---

### Test: `IsCrossingMidpointForward_ReturnsTrue_WhenCrossed_Async`

**Purpose**: Verify midpoint forward detection.
**Setup**: lastPointerX = 149, pointerX = 151, midpoint = 150
**Assertions**:

- Returns true

---

### Test: `IsCrossingMidpointForward_ReturnsFalse_WhenNotCrossed_Async`

**Purpose**: Verify no false positives.
**Setup**: Pointer moves but stays left of midpoint
**Assertions**:

- Returns false

---

### Test: `IsCrossingMidpointBackward_ReturnsTrue_WhenCrossed_Async`

**Purpose**: Verify midpoint backward detection.
**Setup**: lastPointerX = 151, pointerX = 149, midpoint = 150, stack not empty
**Assertions**:

- Returns true

---

### Test: `IsCrossingMidpointBackward_ReturnsFalse_WhenStackEmpty_Async`

**Purpose**: Verify no pop when stack is empty.
**Setup**: Empty stack, pointer crosses back
**Assertions**:

- Returns false (cannot pop from empty stack)

---

## Phase 8: Transform Offset Calculation Tests

### Test: `PushedItemOffset_Forward_IsNegative_ForLeftSlide_Async`

**Purpose**: Verify offset direction for forward push.
**Setup**: Dragged shell at X=100, pushed item at X=220
**Assertions**:

- Offset = 100 - 220 = -120 (slides left)

---

### Test: `PushedItemOffset_Backward_IsPositive_ForRightSlide_Async`

**Purpose**: Verify offset direction for backward push.
**Setup**: Dragged shell at X=220, pushed item at X=100
**Assertions**:

- Offset = 220 - 100 = +120 (slides right)

---

### Test: `PushedItemOffset_CoversShellExactly_WithSpacing_Async`

**Purpose**: Verify pushed content aligns with dragged shell.
**Setup**: Items with spacing=4, widths=160
**Assertions**:

- Pushed content left edge aligns with dragged shell left edge (within 1px tolerance)

---

## Phase 9: Visual State Tests

### Test: `IsDragging_SetToTrue_OnDragStart_Async`

**Purpose**: Verify visual feedback flag.
**Assertions**:

- `draggedVisualItem.IsDragging` true after InitiateDrag

---

### Test: `IsDragging_SetToFalse_OnDrop_Async`

**Purpose**: Verify flag cleared on drop.
**Assertions**:

- `draggedVisualItem.IsDragging` false after CompleteDrag

---

### Test: `DraggingVisualState_AppliedToShell_NotContent_Async`

**Purpose**: Verify visual effects apply to shell, not transformed content.
**Setup**: Check VisualStateManager state
**Assertions**:

- Shell has "Dragging" state active
- Content transform is independent

---

## Phase 10: Coordinate Space Tests

### Test: `ScreenToStrip_ConvertsPhysicalToLogical_Correctly_Async`

**Purpose**: Verify coordinate conversion accuracy.
**Setup**: Known screen point, known strip bounds, DPI=150
**Assertions**:

- Converted point matches expected logical coordinate

---

### Test: `ScreenToStrip_HandlesMultiMonitorDPI_Correctly_Async`

**Purpose**: Verify DPI handling across monitors.
**Setup**: Drag across two monitors with different DPI (96 and 144)
**Assertions**:

- Strip-relative coordinates accurate on both monitors

---

### Test: `TransformToVisual_ReturnsCorrectItemPosition_InStripCoords_Async`

**Purpose**: Verify item position calculation.
**Setup**: Item at known position in repeater
**Assertions**:

- TransformToVisual(strip) returns expected point

---

## Summary

**Total test cases**: ~70 placeholders covering:

- Initialization (6 tests)
- Movement tracking (4 tests)
- Midpoint crossing (10 tests)
- Drop and commit (8 tests)
- Edge cases (12 tests)
- Integration sequences (6 tests)
- Helper methods (10 tests)
- Transform offsets (3 tests)
- Visual states (3 tests)
- Coordinate spaces (3 tests)

**Next steps**:

1. Implement core logic in ReorderStrategy.
2. Add TabStrip helpers (GetContentTransform, GetItemLeftPosition, etc.).
3. Write actual test code using these placeholders.
4. Run and debug.

**Status**: Test specification complete. Ready for implementation.
