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
    private sealed partial class ReorderLayout
    {
        private const double HotspotTolerance = 0.5;

        private readonly DragContext context;
        private readonly ILogger logger;
        private readonly List<TabStripItemSnapshot> items;
        private readonly double itemSpacing;
        private readonly double draggedItemWidth;
        private readonly double itemDisplacement;

        private double currentHotspotX;
        private int dropTargetItemIndex = -1;

        public ReorderLayout(DragContext context, SpatialPoint<ElementSpace> pointerPosition, ILogger logger)
        {
            Debug.Assert(context is { TabStrip: { } }, message: "Context must be set to take snapshot");
            Debug.Assert(context.HotspotOffsets.X >= 0, $"Grab offset must be non-negative: grabOffsetX={context.HotspotOffsets.X}");
            Debug.Assert(context.DraggedItemIndex >= 0, "Dragged item index must be non-negative");

            this.context = context;
            this.logger = logger;
            this.currentHotspotX = pointerPosition.Point.X;
            this.items = [.. context.TabStrip.TakeSnapshot()];
            Debug.Assert(this.items.Count > 0, "TabStrip must have at least one item");

            Debug.WriteLine($"[ReorderLayout] Initialized: itemCount={this.items.Count}, draggedIndex={context.DraggedItemIndex}, draggedVisualIndex={this.DraggedItemVisualIndex}");

            // Scan the items to find min and max positions, the dragged item visual index and to log.
            // Items are already sorted by LayoutOrigin.X
            for (var i = 0; i < this.items.Count; i++)
            {
                var item = this.items[i];
                this.LogSnapshot(i, item);

                if (item.ItemIndex == context.DraggedItemIndex)
                {
                    this.DraggedItemVisualIndex = i;
                    this.DraggedItemSnapshot = item;
                    this.dropTargetItemIndex = item.ItemIndex;
                    this.draggedItemWidth = item.Width;
                    this.LogDraggedItemFound(i, context.HotspotOffsets.X);
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

            this.itemDisplacement = this.draggedItemWidth + this.itemSpacing;

            Debug.Assert(this.DraggedItemVisualIndex >= 0, "Dragged item must be found in TabStrip items");
            Debug.Assert(this.DraggedItemVisualIndex < this.items.Count, "Dragged item visual index must be within bounds");
            Debug.Assert(this.DraggedItemSnapshot is not null, "Dragged item snapshot must be set");
            Debug.Assert(this.itemSpacing >= 0, "Item spacing must be non-negative");
            Debug.Assert(this.MinLeft < this.MaxRight, "Strip must have positive width");

            // Detach the dragged item and position it properly under the pointer
            this.DetachDraggedItem(pointerPosition.Point.X);
        }

        public IReadOnlyList<TabStripItemSnapshot> Items => this.items.AsReadOnly();

        private int DraggedItemVisualIndex { get; }

        private TabStripItemSnapshot DraggedItemSnapshot { get; }

        private double DraggedItemLeftEdge => this.DraggedItemSnapshot.LayoutOrigin.Point.X + this.DraggedItemSnapshot.Offset;

        private double DraggedItemRightEdge => this.DraggedItemLeftEdge + this.DraggedItemSnapshot.Width;

        private double MinLeft { get; }

        private double MaxRight { get; }

        public (int dragIndex, int dropIndex) GetCommittedIndices()
            => (this.DraggedItemSnapshot.ItemIndex, this.dropTargetItemIndex);

        public void UpdateDrag(SpatialPoint<ElementSpace> pointerPosition)
        {
            Debug.Assert(this.context.TabStrip is not null, "TabStrip must be set in DragContext");

            this.AssertInvariants();

            if (pointerPosition.Point.X == this.currentHotspotX)
            {
                return;
            }

            var movementDirection = pointerPosition.Point.X > this.currentHotspotX ? Direction.Right : Direction.Left;

            // At right edge: skip until pointer comes back to hotspot
            if (this.DraggedItemRightEdge >= this.MaxRight && pointerPosition.Point.X > this.currentHotspotX)
            {
                this.LogDragUpdateSkipped(pointerPosition.Point.X, movementDirection);
                return;
            }

            // At left edge: skip until pointer comes back to hotspot
            if (this.DraggedItemLeftEdge <= this.MinLeft && pointerPosition.Point.X < this.currentHotspotX)
            {
                this.LogDragUpdateSkipped(pointerPosition.Point.X, movementDirection);
                return;
            }

            this.ProcessDragSteps(pointerPosition, movementDirection);
        }

        private void ProcessDragSteps(SpatialPoint<ElementSpace> pointerPosition, Direction movementDirection)
        {
            var pointerDelta = Math.Abs(pointerPosition.Point.X - this.currentHotspotX);
            this.LogDragUpdate(this.currentHotspotX, pointerPosition.Point.X, pointerDelta, movementDirection);

            var step = 0;
            while (pointerDelta != 0)
            {
                ++step;
                var stepDelta = Math.Min(Math.Abs(pointerDelta), DragThresholds.SwapThreshold);
                pointerDelta -= stepDelta;

                var previousLeftEdge = this.DraggedItemLeftEdge;
                this.TranslateDraggedItem(stepDelta, movementDirection);
                var actualDelta = Math.Abs(this.DraggedItemLeftEdge - previousLeftEdge);

                this.AssertInvariants();

                // If item is clamped and can't move, stop processing
                if (actualDelta == 0)
                {
                    break;
                }

                var closest = this.FindClosestItemByCenter(movementDirection);
                if (closest != null && this.CheckOverlapWithClosest(closest))
                {
                    this.LogOverlapDetected(closest.ItemIndex);
                    this.dropTargetItemIndex = closest.ItemIndex;
                    var displacementDirection = movementDirection == Direction.Right ? Direction.Left : Direction.Right;
                    this.TranslateAdjacentItem(closest, displacementDirection);
                }
            }

            this.SyncCurrentHotspot();

            this.LogDragUpdateComplete(step);
            this.AssertInvariants();
        }

        [Conditional("DEBUG")]
        private void AssertInvariants()
        {
            // INVARIANT: Dragged item never leaves tabstrip bounds
            Debug.Assert(this.DraggedItemLeftEdge >= this.MinLeft, "Dragged item left edge must be >= MinLeft");
            Debug.Assert(this.DraggedItemRightEdge <= this.MaxRight, "Dragged item right edge must be <= MaxRight");

            // INVARIANT: currentHotspotX = DraggedItemLeftEdge + HotspotOffsets.X (within a few pixels of layout tolrerance
            var expectedHotspotX = this.DraggedItemLeftEdge + this.context.HotspotOffsets.X;
            Debug.Assert(
                Math.Abs(this.currentHotspotX - expectedHotspotX) <= 3,
                $"Hotspot invariant violated: currentHotspotX={this.currentHotspotX}, expected={expectedHotspotX}");
        }

        private bool CheckOverlapWithClosest(TabStripItemSnapshot closest)
        {
            var draggedItem = this.items[this.DraggedItemVisualIndex];
            var draggedCenter = draggedItem.LayoutOrigin.Point.X + draggedItem.Offset + (draggedItem.Width / 2);

            var closestLeft = closest.LayoutOrigin.Point.X + closest.Offset;
            var closestRight = closestLeft + closest.Width;
            var closestCenter = closestLeft + (closest.Width / 2);

            // Determine which edge to check based on relative position
            var overlap = closestCenter < draggedCenter
                ? closestRight - this.DraggedItemLeftEdge // Closest is left, check left edge
                : this.DraggedItemRightEdge - closestLeft; // Closest is right, check right edge

            return overlap > DragThresholds.SwapThreshold;
        }

        private TabStripItemSnapshot? FindClosestItemByCenter(Direction direction)
        {
            var draggedCenter = GetItemCenter(this.DraggedItemSnapshot);

            TabStripItemSnapshot? closest = null;
            var minDistance = double.MaxValue;

            for (var i = 0; i < this.items.Count; i++)
            {
                if (i == this.DraggedItemVisualIndex)
                {
                    continue;
                }

                var item = this.items[i];
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

            static double GetItemCenter(TabStripItemSnapshot item)
            {
                return item.LayoutOrigin.Point.X + item.Offset + (item.Width / 2);
            }
        }

        /// <summary>
        ///     Translates the adjacent target item to make space for the dragged item.
        /// </summary>
        /// <param name="itemSnapshot">The snapshot of the adjacent item to translate.</param>
        /// <param name="direction">The displacement direction.</param>
        /// <remarks>
        ///     Clamps adjacent-item offsets to at most ±(dragged width + spacing) and skips
        ///     reapplying transforms when that limit is already reached. This prevents repeated
        ///     offset accumulation if the UI lags behind pointer updates while still letting the
        ///     offset return to zero (and resetting the drop target) when we move back across the
        ///     item.
        /// </remarks>
        private void TranslateAdjacentItem(TabStripItemSnapshot itemSnapshot, Direction direction)
        {
            const double OffsetResetTolerance = 0.01;

            // Compute the one-step displacement in the requested direction (dragged width + spacing).
            var displacementStep = direction.Sign() * this.itemDisplacement;
            var desiredOffset = itemSnapshot.Offset + displacementStep;

            // Clamp to the expected bounds so multiple notifications cannot accumulate drift.
            var clampedOffset = Math.Clamp(desiredOffset, -this.itemDisplacement, this.itemDisplacement);

            // Avoid redundant work when the existing transform already matches the clamped value.
            if (Math.Abs(clampedOffset - itemSnapshot.Offset) <= OffsetResetTolerance)
            {
                return;
            }

            // Snap near-zero values back to 0 so transforms settle cleanly.
            // When the neighbour slides back home, restore the drop target to the dragged item.
            itemSnapshot.Offset = Math.Abs(clampedOffset) <= OffsetResetTolerance ? 0 : clampedOffset;
            if (itemSnapshot.Offset == 0 && this.dropTargetItemIndex == itemSnapshot.ItemIndex)
            {
                this.dropTargetItemIndex = this.DraggedItemSnapshot.ItemIndex;
            }

            try
            {
                this.context.TabStrip!.ApplyTransformToItem(itemSnapshot.ItemIndex, itemSnapshot.Offset);
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                Debug.WriteLine($"[ReorderLayout] ApplyTransformToItem failed for index={itemSnapshot.ItemIndex}, offset={itemSnapshot.Offset}: {ex}");
            }
#pragma warning restore CA1031 // Do not catch general exception types

            this.LogAdjacentItemDisplaced(itemSnapshot.ItemIndex, direction, itemSnapshot.Offset);
        }

        private void DetachDraggedItem(double pointerX)
        {
            var initialDraggedItemDisplacement = pointerX - this.context.HotspotOffsets.X - this.DraggedItemSnapshot.LayoutOrigin.Point.X;
            var direction = initialDraggedItemDisplacement > 0 ? Direction.Right : Direction.Left;
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
            if (delta == 0)
            {
                return;
            }

            var desiredLeftEdge = this.DraggedItemLeftEdge + (direction.Sign() * delta);
            var boundedLeftEdge = Math.Clamp(desiredLeftEdge, this.MinLeft, this.MaxRight - this.DraggedItemSnapshot.Width);

            var adjustedDelta = boundedLeftEdge - this.DraggedItemLeftEdge;
            if (Math.Abs(adjustedDelta) <= MovementTolerance)
            {
                // When reentering reorder mode after dropping into empty strip space, the dragged
                // item clamps at the strip edge, preventing hotspot realignment and tripping the
                // ReorderLayout invariant. Keep currentHotspotX consistent even if the pointer is
                // far outside the realized tab.
                this.SyncCurrentHotspot();
                return;
            }

            this.DraggedItemSnapshot.Offset += adjustedDelta;
            try
            {
                this.context.TabStrip!.ApplyTransformToItem(this.DraggedItemSnapshot.ItemIndex, this.DraggedItemSnapshot.Offset);
            }
#pragma warning disable CA1031 // Do not catch general exception types
            catch (Exception ex)
            {
                Debug.WriteLine($"[ReorderLayout] ApplyTransformToItem (dragged) failed for index={this.DraggedItemSnapshot.ItemIndex}, offset={this.DraggedItemSnapshot.Offset}: {ex}");
            }
#pragma warning restore CA1031 // Do not catch general exception types

            this.LogDraggedItemTranslated(this.DraggedItemSnapshot.Offset);
            this.SyncCurrentHotspot();
        }

        private void SyncCurrentHotspot()
            => this.currentHotspotX = this.DraggedItemLeftEdge + this.context.HotspotOffsets.X;
    }
}
