// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Provides drag-and-drop reordering logic for tab strip items.
/// </summary>
internal partial class ReorderStrategy
{
    /// <summary>
    ///     ReorderLayout implements a visual gap-sliding algorithm for tab reordering.
    /// </summary>
    /// <remarks>
    ///     <para><strong>ALGORITHM OVERVIEW</strong></para>
    ///     <para>
    ///     This implementation follows a gap-sliding reordering algorithm. When a tab is grabbed, a logical gap is
    ///     created at the dragged slot. The dragged item detaches and follows the pointer while the gap remains
    ///     stationary until a neighbour is slid into it. This yields smooth visual feedback without changing logical
    ///     indexes until a drop occurs.
    ///     </para>
    ///     <para><strong>CONTRACTS &amp; RULES</strong></para>
    ///     <list type="bullet">
    ///       <item>
    ///         <para>
    ///         Gap initialization: gapLeft and gapRight are set to the dragged item's left and right edges
    ///         (ElementSpace). The gap width equals the dragged item's width and does not change during the drag.
    ///         </para>
    ///       </item>
    ///       <item>
    ///         <para>
    ///         Dragged item motion: the dragged item follows the pointer and is clamped inside [MinLeft, MaxRight -
    ///         draggedWidth]. The initial hotspot offset remains constant for the drag duration.
    ///         </para>
    ///       </item>
    ///       <item>
    ///         <para>
    ///         Slide conditions: a neighbour is considered for sliding only when the dragged item overlaps it beyond
    ///         DragThresholds.SwapThreshold and it lies in the drag direction.
    ///         </para>
    ///       </item>
    ///       <item>
    ///         <para>Slide operation (two-step):
    ///         <list type="number">
    ///           <item>
    ///             Align the crossed item's appropriate edge to the current gap edge exactly (no spacing):
    ///             <list type="bullet">
    ///               <item>Dragging RIGHT: align crossed item's LEFT to gapLeft.</item>
    ///               <item>Dragging LEFT: align crossed item's RIGHT to gapRight.</item>
    ///             </list>
    ///             After translation, set the crossed item's Offset and call ApplyTransformToItem.
    ///           </item>
    ///           <item>
    ///             Compute the new gap slot using the crossed item's PRE-SLIDE bounds and the dragged item's width:
    ///             <list type="bullet">
    ///               <item>If crossed moved LEFT into gapLeft, newGapRight = target previous right edge.</item>
    ///               <item>If crossed moved RIGHT into gapRight, newGapLeft = target previous left edge.</item>
    ///             </list>
    ///             Then set newGapLeft/newGapRight and continue.
    ///           </item>
    ///         </list>
    ///         </para>
    ///       </item>
    ///       <item>
    ///         <para>Only the crossed target is translated; other items keep their existing offsets.</para>
    ///       </item>
    ///       <item>
    ///         <para>
    ///         Gap index resolution: DraggedItemVisualIndex is computed from each item's current left edge
    ///         (LayoutOrigin + Offset), not their original layout origins.
    ///         </para>
    ///       </item>
    ///     </list>
    ///     <para><strong>INVARIANTS</strong></para>
    ///     <list type="number">
    ///       <item>
    ///         Exactly one gap exists and its width equals the dragged item's width for the duration of the drag.
    ///       </item>
    ///       <item>
    ///         The dragged item always stays aligned under the pointer using the initial hotspot offset.
    ///       </item>
    ///       <item>
    ///         No two non-dragged items overlap: slide translations align edges exactly with the gap edge.
    ///       </item>
    ///       <item>
    ///         Slides only occur in the direction of drag; lastSlidTargetId and lastSlideDirection prevent redundant
    ///         re-sliding.
    ///       </item>
    ///     </list>
    ///     <para>
    ///     Coordinates are ElementSpace; ApplyTransformToItem accepts logical offsets in ElementSpace.
    ///     </para>
    /// </remarks>
    private sealed partial class ReorderLayout
    {
        private readonly DragContext context;
        private readonly ILogger logger;
        private readonly List<TabStripItemSnapshot> items;
        private readonly double itemSpacing;
        private readonly double draggedItemWidth;

        // This represents the amount of offset from the left edge of the dragged item to current
        // pointer position. It is provided at initialization via the DragContext.HotspotOffsets.X
        // and theoretically remains constant throughout the drag.
        private readonly double hotspotOffset;

        private double gapLeft;

        private double gapRight;

        private int dropTargetItemIndex;

        private Guid? lastSlidTargetId;

        private Direction? lastSlideDirection;

        public ReorderLayout(DragContext context, SpatialPoint<ElementSpace> pointerPosition, ILogger logger)
        {
            Debug.Assert(context is { TabStrip: { } }, message: "Context must be set to take snapshot");
            Debug.Assert(context.HotspotOffsets.X >= 0, $"Grab offset must be non-negative: grabOffsetX={context.HotspotOffsets.X}");

            this.context = context;
            this.logger = logger;
            this.items = [.. context.TabStrip.TakeSnapshot()];
            Debug.Assert(this.items.Count > 0, "TabStrip must have at least one item");

            var draggedTabItem = context.DraggedItemData;
            this.LogReorderLayoutInitialized(this.items.Count, draggedTabItem.ContentId.ToString(), this.DraggedItemVisualIndex);

            // Scan the items to find min and max positions, the dragged item visual index and to log.
            // Items are already sorted by LayoutOrigin.X
            for (var i = 0; i < this.items.Count; i++)
            {
                var item = this.items[i];
                this.LogSnapshot(i, item);

                if (item.ContentId == draggedTabItem.ContentId)
                {
                    this.DraggedItemVisualIndex = i;
                    this.DraggedItemSnapshot = item;
                    this.dropTargetItemIndex = item.ItemIndex;
                    this.draggedItemWidth = item.Width;
                    this.hotspotOffset = context.HotspotOffsets.X;
                    this.LogDraggedItemFound(i, this.hotspotOffset);
                }

                if (i == 0)
                {
                    this.MinLeft = item.LayoutOrigin.Point.X;
                    this.MaxRight = item.LayoutOrigin.Point.X + item.Width;
                    continue;
                }

                if (i == 1)
                {
                    var previousItem = this.items[i - 1];
                    this.itemSpacing = item.LayoutOrigin.Point.X - previousItem.LayoutOrigin.Point.X - previousItem.Width;
                }

                if (i == this.items.Count - 1)
                {
                    this.MaxRight = item.LayoutOrigin.Point.X + item.Width;
                }

                Debug.Assert(item.LayoutOrigin.Point.X >= this.MinLeft, "Items must be sorted by LayoutOrigin.X");
            }

            Debug.Assert(this.DraggedItemVisualIndex >= 0, "Dragged item must be found in TabStrip items");
            Debug.Assert(this.DraggedItemVisualIndex < this.items.Count, "Dragged item visual index must be within bounds");
            Debug.Assert(this.DraggedItemSnapshot is not null, "Dragged item snapshot must be set");
            Debug.Assert(this.items.Count == 1 || this.itemSpacing >= 0, "Item spacing must be non-negative when multiple items exist");
            Debug.Assert(this.MinLeft < this.MaxRight, "Strip must have positive width");

            this.InitializeGapTracking();

            // Detach the dragged item and position it properly under the pointer
            this.DetachDraggedItem(pointerPosition.Point.X);
        }

        public IReadOnlyList<TabStripItemSnapshot> Items => this.items.AsReadOnly();

        private int DraggedItemVisualIndex { get; set; }

        private TabStripItemSnapshot DraggedItemSnapshot { get; }

        private double DraggedItemLeftEdge => this.DraggedItemSnapshot.LayoutOrigin.Point.X + this.DraggedItemSnapshot.Offset;

        private double DraggedItemRightEdge => this.DraggedItemLeftEdge + this.DraggedItemSnapshot.Width;

        private double MinLeft { get; }

        private double MaxRight { get; }

        public int GetCommittedDropIndex()
            => this.dropTargetItemIndex;

        public void UpdateDrag(SpatialPoint<ElementSpace> pointerPosition)
        {
            Debug.Assert(this.context.TabStrip is not null, "TabStrip must be set in DragContext");

            this.AssertInvariants();

            var desiredLeftEdge = pointerPosition.Point.X - this.hotspotOffset;
            var movementDirection = desiredLeftEdge > this.DraggedItemLeftEdge ? Direction.Right : Direction.Left;

            // At right edge: skip until pointer comes back to hotspot
            if (this.DraggedItemRightEdge >= this.MaxRight && movementDirection == Direction.Right)
            {
                this.LogDragUpdateSkipped(pointerPosition.Point.X, movementDirection);
                return;
            }

            // At left edge: skip until pointer comes back to hotspot
            if (this.DraggedItemLeftEdge <= this.MinLeft && movementDirection == Direction.Left)
            {
                this.LogDragUpdateSkipped(pointerPosition.Point.X, movementDirection);
                return;
            }

            this.ProcessDragSteps(pointerPosition, movementDirection);
        }

        private static double GetItemLayoutOriginLeft(TabStripItemSnapshot item)
            => item.LayoutOrigin.Point.X;

        private static double GetItemLeft(TabStripItemSnapshot item)
            => item.LayoutOrigin.Point.X + item.Offset;

        private static double GetItemRight(TabStripItemSnapshot item)
            => GetItemLeft(item) + item.Width;

        private static double GetItemCenter(TabStripItemSnapshot item)
            => GetItemLeft(item) + (item.Width / 2);

        private int ResolveGapVisualIndexFromOrigins(double[] slotOrigins, double gapLeft)
        {
            const double PositionTolerance = 0.5;

            // Use midpoint intervals between consecutive slot origins to map the gap to a slot.
            // Example: for origins [x0, x1, x2], mid1 = (x0 + x1) / 2, mid2 = (x1 + x2) / 2.
            // gapLeft <= mid1 => index 0, gapLeft <= mid2 => index 1, else index 2, etc.
            for (var i = 0; i < slotOrigins.Length - 1; i++)
            {
                var mid = (slotOrigins[i] + slotOrigins[i + 1]) / 2.0;
                if (gapLeft <= mid + PositionTolerance)
                {
                    var resolved = i;
                    this.LogResolveGapVisualIndex(gapLeft, slotOrigins, resolved);
                    return resolved;
                }
            }

            // If the gap wasn't matched earlier, it's at or after the last slot => return last index explicitly.
            var lastIndex = slotOrigins.Length - 1;
            this.LogResolveGapVisualIndex(gapLeft, slotOrigins, lastIndex);
            return lastIndex;
        }

        private void ProcessDragSteps(SpatialPoint<ElementSpace> pointerPosition, Direction movementDirection)
        {
            var desiredLeftEdge = pointerPosition.Point.X - this.hotspotOffset;
            var pointerDelta = Math.Abs(desiredLeftEdge - this.DraggedItemLeftEdge);
            this.LogDragUpdate(this.hotspotOffset, pointerPosition.Point.X, pointerDelta, movementDirection);

            var step = 0;
            while (pointerDelta > 0.01)
            {
                ++step;
                var stepDelta = Math.Min(pointerDelta, DragThresholds.SwapThreshold);
                pointerDelta -= stepDelta;

                var previousLeftEdge = this.DraggedItemLeftEdge;
                this.TranslateDraggedItem(stepDelta, movementDirection);
                var actualDelta = Math.Abs(this.DraggedItemLeftEdge - previousLeftEdge);

                this.AssertInvariants();

                // If item is clamped and can't move, stop processing
                if (actualDelta < 0.01)
                {
                    break;
                }

                var closest = this.FindClosestItemByCenter(movementDirection);
                if (closest != null && this.CheckOverlapWithClosest(closest))
                {
                    this.LogOverlapDetected(closest.ItemIndex);
                    this.SlideTargetIntoGap(closest, movementDirection);
                }
            }

            this.LogDragUpdateComplete(step);
            this.AssertInvariants();
        }

        [Conditional("DEBUG")]
        private void AssertInvariants()
        {
            // INVARIANT: Dragged item never leaves tabstrip bounds
            Debug.Assert(this.DraggedItemLeftEdge >= this.MinLeft - 0.01, "Dragged item left edge must be >= MinLeft");
            Debug.Assert(this.DraggedItemRightEdge <= this.MaxRight + 0.01, "Dragged item right edge must be <= MaxRight");
        }

        private void InitializeGapTracking()
        {
            this.gapLeft = this.DraggedItemSnapshot.LayoutOrigin.Point.X;

            // Gap starts as the dragged item's occupied slot: left and right edges of
            // the dragged item (no additional spacing). The gap is a logical slot
            // where the dragged item would be inserted.
            this.gapRight = this.gapLeft + this.draggedItemWidth;
            this.DraggedItemVisualIndex = this.ResolveGapVisualIndex();
            this.dropTargetItemIndex = this.items[this.DraggedItemVisualIndex].ItemIndex;
            this.lastSlidTargetId = null;
            this.lastSlideDirection = null;
        }

        private void SlideTargetIntoGap(TabStripItemSnapshot target, Direction movementDirection)
        {
            if (this.lastSlidTargetId == target.ContentId && this.lastSlideDirection == movementDirection)
            {
                return;
            }

            var targetVisualIndex = this.items.FindIndex(item => item.ContentId == target.ContentId);
            if (targetVisualIndex < 0)
            {
                return;
            }

            // Save the current item bounds before translation.
            var (alignedLeft, newGapLeft, newGapRight) = this.ComputeSlideAndNewGap(target, movementDirection);

            var targetOffset = alignedLeft - GetItemLayoutOriginLeft(target);
            if (Math.Abs(target.Offset - targetOffset) > 0.01)
            {
                target.Offset = targetOffset;
                this.context.TabStrip!.ApplyTransformToItem(target.ContentId, targetOffset);
            }

            // After sliding the target we set the new gap based on the target's
            // original slot (captured in the helper as newGapLeft/newGapRight).
            this.gapLeft = newGapLeft;
            this.gapRight = newGapRight;

            this.DraggedItemVisualIndex = this.ResolveGapVisualIndex();
            this.dropTargetItemIndex = this.items[this.DraggedItemVisualIndex].ItemIndex;

            this.lastSlidTargetId = target.ContentId;
            this.lastSlideDirection = movementDirection;
        }

        private (double alignedLeft, double newGapLeft, double newGapRight) ComputeSlideAndNewGap(TabStripItemSnapshot target, Direction movementDirection)
        {
            var targetLeft = GetItemLeft(target);
            var targetRight = targetLeft + target.Width;

            double alignedLeft;
            double newGapLeft;
            double newGapRight;

            if (movementDirection == Direction.Right)
            {
                // Crossed item moves left: align its left edge with gapLeft.
                alignedLeft = this.gapLeft;

                // The previous slot is where the crossed item was; the new gap
                // should be the area of that slot sized to draggedItemWidth.
                newGapRight = targetRight;
                newGapLeft = newGapRight - this.draggedItemWidth;
            }
            else
            {
                // Crossed item moves right: align its right edge with gapRight.
                var alignedRight = this.gapRight;
                alignedLeft = alignedRight - target.Width;
                newGapLeft = targetLeft;
                newGapRight = newGapLeft + this.draggedItemWidth;
            }

            return (alignedLeft, newGapLeft, newGapRight);
        }

        private int ResolveGapVisualIndex()
        {
            const double PositionTolerance = 0.5;

            if (this.items.Count == 1)
            {
                return 0;
            }

            // Build array of original slot origins (layout origins). Unlike left edges which
            // contain offsets applied by slides, layout origins remain stable and reflect the
            // canonical slot positions we want to map the gap to. Resolving to the nearest
            // layout origin avoids the bug where a morphed left edge (due to a slide) pulls
            // the boundary calculation and yields the wrong index.
            var slotOrigins = new double[this.items.Count];
            for (var i = 0; i < this.items.Count; i++)
            {
                slotOrigins[i] = GetItemLayoutOriginLeft(this.items[i]);
            }

            // Special-case: if gap is before the first slot (or very close), return index 0.
            if (this.gapLeft <= slotOrigins[0] + PositionTolerance)
            {
                return 0;
            }

            // If gap matches very closely an existing slot origin, return it immediately.
            for (var i = 0; i < slotOrigins.Length; i++)
            {
                if (Math.Abs(this.gapLeft - slotOrigins[i]) <= PositionTolerance)
                {
                    return i;
                }
            }

            return this.ResolveGapVisualIndexFromOrigins(slotOrigins, this.gapLeft);
        }

        private bool CheckOverlapWithClosest(TabStripItemSnapshot closest)
        {
            var draggedLeft = this.DraggedItemLeftEdge;
            var draggedRight = this.DraggedItemRightEdge;
            var draggedCenter = GetItemLeft(this.DraggedItemSnapshot) + (this.DraggedItemSnapshot.Width / 2);

            var closestLeft = GetItemLeft(closest);
            var closestRight = GetItemRight(closest);
            var closestCenter = GetItemCenter(closest);

            // Determine which edge to check based on relative position
            var overlap = closestCenter < draggedCenter
                ? closestRight - draggedLeft // Closest is left, check left edge
                : draggedRight - closestLeft; // Closest is right, check right edge

            return overlap > DragThresholds.SwapThreshold;
        }

        private TabStripItemSnapshot? FindClosestItemByCenter(Direction direction)
        {
            var draggedCenter = GetItemCenter(this.DraggedItemSnapshot);

            TabStripItemSnapshot? closest = null;
            var minDistance = double.MaxValue;

            for (var i = 0; i < this.items.Count; i++)
            {
                var item = this.items[i];

                if (item.ContentId == this.DraggedItemSnapshot.ContentId)
                {
                    continue;
                }

                var itemCenter = GetItemCenter(item);

                // Filter by direction: only consider items in the direction of movement
                if (direction == Direction.Left && itemCenter >= draggedCenter)
                {
                    continue; // Moving left, skip items to the right or at same position
                }

                if (direction == Direction.Right && itemCenter <= draggedCenter)
                {
                    continue; // Moving right, skip items to the left or at same position
                }

                var distance = Math.Abs(draggedCenter - itemCenter);

                if (distance < minDistance)
                {
                    minDistance = distance;
                    closest = item;
                }
            }

            return closest;

            // Uses class-level GetItemCenter helper
        }

        private void DetachDraggedItem(double pointerX)
        {
            var initialDraggedItemDisplacement = pointerX - this.hotspotOffset - this.DraggedItemSnapshot.LayoutOrigin.Point.X;
            var direction = initialDraggedItemDisplacement >= 0 ? Direction.Right : Direction.Left;
            this.TranslateDraggedItem(Math.Abs(initialDraggedItemDisplacement), direction);
        }

        /// <summary>
        ///     Translates the dragged item while keeping it aligned under the pointer and inside
        ///     the tab strip bounds.
        /// </summary>
        /// <param name="delta">The requested pointer movement magnitude.</param>
        /// <param name="direction">The movement direction.</param>
        private void TranslateDraggedItem(double delta, Direction direction)
        {
            const double MovementTolerance = 0.001;

            Debug.Assert(delta >= 0, "Delta must be non-negative");
            if (delta < MovementTolerance)
            {
                return;
            }

            var desiredLeftEdge = this.DraggedItemLeftEdge + (direction.Sign() * delta);
            var boundedLeftEdge = Math.Clamp(desiredLeftEdge, this.MinLeft, this.MaxRight - this.DraggedItemSnapshot.Width);

            var adjustedDelta = boundedLeftEdge - this.DraggedItemLeftEdge;
            if (Math.Abs(adjustedDelta) < MovementTolerance && Math.Abs(delta) > 0)
            {
                return;
            }

            this.DraggedItemSnapshot.Offset += adjustedDelta;
            this.context.TabStrip!.ApplyTransformToItem(this.DraggedItemSnapshot.ContentId, this.DraggedItemSnapshot.Offset);
        }
    }
}
