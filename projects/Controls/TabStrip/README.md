# TabStrip Layout

The TabStrip control supports three Layout policies: **Auto**, **Equal**, and
**Compact**. Each policy determines how tab items are sized based on available
space, content, and constraints. The layout is re-evaluated whenever the tab
items collection changes or the TabStrip's size changes (triggering invalidation
and re-measurement).

BOTE: This specification impacts only the layout algorithm, the control template
and the scrolling indicators etc, are all already implemented.

## General contracts

- `TabStrip` property `PreferredItemWidth` applies to all items in the strip,
  and when set, should always be clamped to `MaxItemWidth`.
- `TabStripItem` property `MinWidth` should always be clamped to `TabStrip`
  property `MaxItemWidth`.
- calculated `Width` should always be clamped between the `TabStripItem`
  `MinWidth` and the `TabStrip` `MaxItemWidth` before used any further.
- Pinned items are placed in a separate `ItemsRepeater` and are not subject to
  scrolling.
- If a `TabStripItem`'s declared `MinWidth` is greater than the `TabStrip`'s
  `MaxItemWidth`, the implementation MUST use `MaxItemWidth` and ignore the
  item's `MinWidth` value (the effective MinWidth is clamped to MaxItemWidth).

## 1. Auto Policy (Default)

- **Behavior**: Each tab sizes to fit its content naturally, subject to the
  TabStrip control's `MaxItemWidth` and each TabStripItem's `MinWidth`
  properties.
- **Implementation**: No explicit `Width` is set on tab items . The layout
  system measures each item with infinite available width to determine its
  desired size, then constrains it between `MinWidth` and `MaxItemWidth`.
- **When Used**:
  - Applied to both pinned and unpinned items, When the TabStrip control's
    `TabWidthPolicy` property is `Auto`.
  - Applied only to pinned items when the TabStrip control's `TabWidthPolicy`
    property is `Compact`.
- **Fallback**:
  - When not enough space in the unpinned items scroll host (the ScrollViewer
    named PartScrollHost in the XAML template) to accomodate all unpinned items,
    scrolling is enabled.

REMINDER: calculated width is always clamped between each item's `MinWidth` and
`MaxItemWidth`

## 2. Equal Policy

- **Behavior**: All tabs use the same width specified by `PreferredItemWidth`
  property of `TabStrip`.
- **Implementation**: Sets `calculatedWidth = PreferredItemWidth` on each tab item.
- **When Used**: When the TabStrip control's `TabWidthPolicy` property is
  `Equal`. Applies to all items, including pinned and unpinned.
- **Fallback**: If not enough space in the regular items scroll host to
  accomodate all items in the unpinned bucket, scrolling is enabled.

## 3. Compact Policy

- **Behavior**: Prioritizes fitting all tabs without scrolling if possible.
- **Implementation**:
  - Starts with Auto sizing; if the total desired widths exceed available space,
    switches to Auto layout for pinned items, and progressively shrinks the
    `Width` of unpinned items towards their `MinWidth` (each item has the
    property). The amount by which they are shrinked should be calculated by
    dividing the space deficit by the number of unpinned items.
  - If still not enough space to accomodate all unpinned items, at `MinWidth`,
    scrolling is enabled.

  - Progressive shrink (iterative algorithm): implement shrinking using an
    iterative redistribution algorithm to respect `MinWidth` for items that hit
    their minimum before the deficit is fully resolved. Example steps:

    1. Measure each unpinned item's auto desired width (intrinsic single-line
        measure). Clamp that measured desired width between the item's
        `MinWidth` and the TabStrip `MaxItemWidth` to produce the initial
        `desiredWidth` for each item.
    2. Compute sumDesired = sum(desiredWidths). If sumDesired <=
        availableUnpinnedWidth, use the desired sizes.
    3. Otherwise deficit = sumDesired - availableUnpinnedWidth. Let
        remainingItems = set of unpinned items not yet clamped to MinWidth.
    4. Compute shrinkPerItem = deficit / remainingItems.Count and attempt to set
        newWidth = desiredWidth - shrinkPerItem for each remaining item. Clamp
        newWidth to each item's `MinWidth`.
    5. If any items were clamped to `MinWidth`, recompute the remaining deficit
        and repeat steps 3–5 with the reduced remainingItems set.
    6. If all unpinned items are at `MinWidth` and deficit > 0 after the
        iterations, enable scrolling.

  - Pixel remainder policy: when splitting widths or shrink amounts across
    items, do not attempt extra redistribution of leftover pixels. If the sum of
    assigned integer pixel widths is less than available space, that small
    leftover is acceptable. If the sum exceeds available space, the algorithm
    must enable scrolling (no further redistribution to absorb fractional pixels
    is required).
- **When Used**: When the TabStrip control's `TabWidthPolicy` property is
  `Compact`. Compacting only applied to unpinned items when space is not
  sufficient. Pinned items use `Auto` policy.

## General Notes

- Uses `DispatcherQueue.TryEnqueue` to defer the check until after the layout
  pass, ensuring `DesiredSize` is accurate.
- When deferring with `DispatcherQueue.TryEnqueue`, the implementation guard
  against multiple enqueues (coalescing) to prevent layout thrash.
- **Constraints**: All policies respect each TabStripItem's `MinWidth` property
  and the TabStrip's `MaxItemWidth`.
- **Scrolling**: Enabled automatically if total item widths exceed available
  space (handled by the ScrollViewer).
- **Pinned vs. Regular Items**: Pinned items are sized separately (typically in
  Equal or Auto mode) and not subject to compacting.

Note (single-line strip): the TabStrip is a single-line strip — tabs are
measured and laid out without wrapping to multiple lines. Measuring items with
an infinite width is intended to capture the single-line, non-wrapping desired
size; the rules above (clamping, progressive shrink) then ensure items fit the
single-line available space or cause scrolling.
