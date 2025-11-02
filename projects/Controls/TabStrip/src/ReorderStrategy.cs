// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Generic;
using System.Linq;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Controls;

/// <summary>
///     Strategy for handling in-TabStrip drag operations using stack-based content transforms.
///     This strategy manages reordering within TabStrip bounds using transforms to slide content
///     between shells, with a stack tracking pushed items for reversibility.
/// </summary>
internal sealed partial class ReorderStrategy : IDragStrategy
{
    private readonly ILogger logger;

    private bool isActive;
    private DragContext? context;

    // Item tracking
    private int draggedItemIndex;        // Index in Items of the dragged tab
    private int dropIndex;                // Current target drop index in Items
    private double hotspotX;              // X offset from dragged item's left edge to pointer grab point

    // Stack of pushed items (for reversibility)
    private Stack<PushedItemInfo> pushedItemsStack = new();

    // Pointer tracking (for midpoint crossing detection)
    private double lastPointerX;          // Previous frame's pointer X in strip coords

    /// <summary>
    ///     Initializes a new instance of the <see cref="ReorderStrategy"/> class.
    /// </summary>
    /// <param name="loggerFactory">The logger factory for creating loggers.</param>
    public ReorderStrategy(ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<ReorderStrategy>() ?? NullLogger<ReorderStrategy>.Instance;
        this.LogCreated();
    }

    /// <inheritdoc/>
    public bool IsActive => this.isActive;

    /// <inheritdoc/>
    public void InitiateDrag(DragContext context, SpatialPoint initialPoint)
    {
        ArgumentNullException.ThrowIfNull(context);

        if (this.isActive)
        {
            this.LogAlreadyActive();
            throw new InvalidOperationException("ReorderStrategy is already active");
        }

        this.context = context;
        this.isActive = true;

        // Store the dragged item index
        this.draggedItemIndex = context.SourceStrip.Items.IndexOf(context.DraggedItem);
        this.dropIndex = this.draggedItemIndex;  // Initially, drop index = original index

        // Store the hotspot offset (where user clicked within the item)
        this.hotspotX = context.Hotspot.X;

        // Convert to TabStrip element coordinates
        var stripPoint = initialPoint.To(CoordinateSpace.Element, context.SourceStrip);

        // Initialize stack and pointer tracking
        this.pushedItemsStack.Clear();
        this.lastPointerX = stripPoint.X;

        this.LogEnterReorderMode(stripPoint.Point);
    }

    /// <inheritdoc/>
    public void OnDragPositionChanged(SpatialPoint currentPoint)
    {
        if (!this.isActive || this.context is null)
        {
            this.LogMoveIgnored();
            return;
        }

        // Convert to TabStrip element coordinates
        var stripPoint = currentPoint.To(CoordinateSpace.Element, this.context.SourceStrip);
        var pointerX = stripPoint.X;

        // Update dragged item transform to follow pointer
        // The transform should position the item so the hotspot aligns with the pointer
        var itemLeft = this.GetItemLeftPosition(this.draggedItemIndex);
        var draggedItemTransform = this.GetContentTransform(this.draggedItemIndex);
        if (draggedItemTransform is not null)
        {
            // Transform.X moves the item so the hotspot point aligns with the pointer
            // If item shell is at itemLeft, and we want hotspot (at itemLeft + hotspotX) to be at pointerX:
            // itemLeft + hotspotX + transform.X = pointerX
            // Therefore: transform.X = pointerX - (itemLeft + hotspotX)
            var transformX = pointerX - (itemLeft + this.hotspotX);
            draggedItemTransform.X = transformX;

            this.logger.LogTrace(
                "Dragged item transform: pointerX={PointerX:F2}, itemLeft={ItemLeft:F2}, hotspotX={HotspotX:F2}, transform.X={TransformX:F2}",
                pointerX,
                itemLeft,
                this.hotspotX,
                transformX);
        }

        // Hit-test to find target item under pointer
        var targetItemIndex = this.HitTestItemAtX(pointerX);

        // Detect midpoint crossing and update stack
        if (this.IsCrossingMidpointForward(pointerX, targetItemIndex))
        {
            // Forward crossing - push the item at dropIndex + 1
            var pushedItemIndex = this.dropIndex + 1;
            var pushedInfo = new PushedItemInfo
            {
                ItemIndex = pushedItemIndex,
                OriginalLeft = this.GetItemLeftPosition(pushedItemIndex),
                Direction = PushDirection.Forward,
            };
            this.pushedItemsStack.Push(pushedInfo);

            // Translate pushed item to cover the shell at the current dropIndex
            // This creates a cascading effect:
            // - First push (dropIndex = draggedItemIndex): Tab3 slides to Tab2's shell
            // - Second push (dropIndex = 3 after first push): Tab4 slides to Tab3's shell
            var targetShellLeft = this.GetItemLeftPosition(this.dropIndex);
            var pushedItemTransform = this.GetContentTransform(pushedItemIndex);
            if (pushedItemTransform is not null)
            {
                var offset = targetShellLeft - pushedInfo.OriginalLeft;
                pushedItemTransform.X = offset;

                this.logger.LogTrace(
                    "Push transform: pushedIndex={PushedIndex}, targetIndex={TargetIndex}, targetShellLeft={TargetLeft:F2}, originalLeft={OriginalLeft:F2}, offset={Offset:F2}",
                    pushedItemIndex,
                    this.dropIndex,
                    targetShellLeft,
                    pushedInfo.OriginalLeft,
                    offset);
            }

            // Update dropIndex to the pushed item's index
            this.dropIndex = pushedItemIndex;

            this.LogItemPushed(pushedItemIndex, PushDirection.Forward);
        }
        else if (this.IsCrossingMidpointBackward(pointerX))
        {
            // Backward crossing - pop the top item from stack and restore it
            var poppedInfo = this.pushedItemsStack.Pop();
            var poppedItemTransform = this.GetContentTransform(poppedInfo.ItemIndex);
            if (poppedItemTransform is not null)
            {
                poppedItemTransform.X = 0; // Restore popped item to its own shell
            }

            // Update dropIndex - remaining items on stack stay in their positions
            this.dropIndex = this.pushedItemsStack.Count > 0
                ? this.pushedItemsStack.Peek().ItemIndex
                : this.draggedItemIndex;

            this.LogItemPopped(poppedInfo.ItemIndex);
        }

        // Store current pointer position for next frame
        this.lastPointerX = pointerX;

        this.LogMove(stripPoint.Point);
    }

    /// <inheritdoc/>
    public bool CompleteDrag(SpatialPoint finalPoint, TabStrip? targetStrip, int? targetIndex)
    {
        if (!this.isActive || this.context is null)
        {
            this.LogDropIgnored();
            return false;
        }

        // Convert to TabStrip element coordinates
        var stripPoint = finalPoint.To(CoordinateSpace.Element, this.context.SourceStrip);

        this.LogDrop(stripPoint.Point, targetStrip, targetIndex);

        // If dropping on the same strip, commit reorder
        if (targetStrip == this.context.SourceStrip)
        {
            var strip = this.context.SourceStrip;

            // Commit the move (single Items.Move)
            if (this.dropIndex != this.draggedItemIndex && this.dropIndex >= 0)
            {
                strip.Items.Move(this.draggedItemIndex, this.dropIndex);
            }

            // Unwind stack and clear all transforms
            while (this.pushedItemsStack.Count > 0)
            {
                var poppedInfo = this.pushedItemsStack.Pop();
                var poppedItemTransform = this.GetContentTransform(poppedInfo.ItemIndex);
                if (poppedItemTransform is not null)
                {
                    poppedItemTransform.X = 0;
                }
            }

            var draggedItemTransform = this.GetContentTransform(this.draggedItemIndex);
            if (draggedItemTransform is not null)
            {
                draggedItemTransform.X = 0;
            }

            // Restore visual state
            if (this.context.SourceVisualItem is not null)
            {
                this.context.SourceVisualItem.IsDragging = false;
            }

            // Reset state
            this.isActive = false;
            this.context = null;
            this.draggedItemIndex = -1;
            this.dropIndex = -1;
            this.pushedItemsStack.Clear();

            this.LogDropSuccess(this.dropIndex);
            return true;
        }

        // Cross-strip drop - cleanup and return false
        // Unwind stack and clear transforms
        while (this.pushedItemsStack.Count > 0)
        {
            var poppedInfo = this.pushedItemsStack.Pop();
            var poppedItemTransform = this.GetContentTransform(poppedInfo.ItemIndex);
            if (poppedItemTransform is not null)
            {
                poppedItemTransform.X = 0;
            }
        }

        var draggedTransform = this.GetContentTransform(this.draggedItemIndex);
        if (draggedTransform is not null)
        {
            draggedTransform.X = 0;
        }

        if (this.context.SourceVisualItem is not null)
        {
            this.context.SourceVisualItem.IsDragging = false;
        }

        this.isActive = false;
        this.context = null;
        this.draggedItemIndex = -1;
        this.dropIndex = -1;
        this.pushedItemsStack.Clear();

        return false;
    }

    /// <summary>
    ///     Gets the TranslateTransform for the wrapper Grid at the specified Items collection index.
    /// </summary>
    /// <param name="itemIndex">Index in Items collection.</param>
    /// <returns>The TranslateTransform for the wrapper Grid, or null if not available.</returns>
    private TranslateTransform? GetContentTransform(int itemIndex)
    {
        if (this.context?.SourceStrip is null || itemIndex < 0 || itemIndex >= this.context.SourceStrip.Items.Count)
        {
            return null;
        }

        var item = this.context.SourceStrip.Items[itemIndex];
        var repeater = this.context.SourceStrip.GetRegularItemsRepeater();
        if (repeater is null)
        {
            return null;
        }

        // Find the wrapper Grid for this specific item data
        // ItemsRepeater.GetElementIndex returns the index in the data source for a realized element
        var wrapperGrid = this.FindWrapperGridForItem(repeater, item);
        if (wrapperGrid is null)
        {
            return null; // Container not realized
        }

        // Get the wrapper Grid's TranslateTransform
        // This is critical - transforming the wrapper Grid (not TabStripItem) keeps ItemsRepeater layout stable
        return wrapperGrid.RenderTransform as TranslateTransform;
    }

    /// <summary>
    ///     Finds the wrapper Grid container for a specific item in the repeater.
    /// </summary>
    /// <param name="repeater">The ItemsRepeater.</param>
    /// <param name="item">The data item to find.</param>
    /// <returns>The wrapper Grid, or null if not found/realized.</returns>
    private Grid? FindWrapperGridForItem(ItemsRepeater repeater, TabItem item)
    {
        // Iterate through realized containers and find the one bound to our item
        for (var i = 0; i < repeater.ItemsSourceView.Count; i++)
        {
            var element = repeater.TryGetElement(i);
            if (element is not Grid grid)
            {
                continue;
            }

            // Check if this Grid's DataContext matches our item
            if (grid.DataContext == item)
            {
                return grid;
            }
        }

        return null;
    }

    /// <summary>
    ///     Gets the left position of an item's shell in strip-relative coordinates.
    ///     This returns the LAYOUT position (shell), not the visual position (which includes transforms).
    /// </summary>
    /// <param name="itemIndex">Index in Items collection.</param>
    /// <returns>The left edge position in logical DIPs relative to the TabStrip.</returns>
    private double GetItemLeftPosition(int itemIndex)
    {
        if (this.context?.SourceStrip is null || itemIndex < 0 || itemIndex >= this.context.SourceStrip.Items.Count)
        {
            return 0;
        }

        var item = this.context.SourceStrip.Items[itemIndex];
        var repeater = this.context.SourceStrip.GetRegularItemsRepeater();
        if (repeater is null)
        {
            return this.ComputeLeftFromLayout(itemIndex);
        }

        var wrapperGrid = this.FindWrapperGridForItem(repeater, item);
        if (wrapperGrid is null)
        {
            // Fallback: compute from layout
            return this.ComputeLeftFromLayout(itemIndex);
        }

        // Get the repeater's position in the TabStrip
        var repeaterTransform = repeater.TransformToVisual(this.context.SourceStrip);
        var repeaterOrigin = repeaterTransform.TransformPoint(new Windows.Foundation.Point(0, 0));

        // Get the wrapper Grid's ActualOffset within the repeater (layout position, ignores RenderTransform)
        var itemOffsetInRepeater = wrapperGrid.ActualOffset;

        // Combine to get strip-relative position
        return repeaterOrigin.X + itemOffsetInRepeater.X;
    }

    /// <summary>
    ///     Gets the width of an item.
    /// </summary>
    /// <param name="itemIndex">Index in Items collection.</param>
    /// <returns>The item width in logical DIPs.</returns>
    private double GetItemWidth(int itemIndex)
    {
        if (this.context?.SourceStrip is null || itemIndex < 0 || itemIndex >= this.context.SourceStrip.Items.Count)
        {
            return 0;
        }

        var item = this.context.SourceStrip.Items[itemIndex];
        var repeater = this.context.SourceStrip.GetRegularItemsRepeater();
        if (repeater is null)
        {
            return this.context.SourceStrip.PreferredItemWidth;
        }

        var wrapperGrid = this.FindWrapperGridForItem(repeater, item);
        if (wrapperGrid is null)
        {
            // Fallback: use preferred item width from layout manager
            this.logger.LogTrace("GetItemWidth({Index}): Container not realized, using PreferredItemWidth={Width:F2}", itemIndex, this.context.SourceStrip.PreferredItemWidth);
            return this.context.SourceStrip.PreferredItemWidth;
        }

        this.logger.LogTrace("GetItemWidth({Index}): ActualWidth={Width:F2}", itemIndex, wrapperGrid.ActualWidth);
        return wrapperGrid.ActualWidth;
    }

    /// <summary>
    ///     Hit-tests to find the Items index of the item under the given X coordinate.
    /// </summary>
    /// <param name="pointerX">X coordinate in strip-relative logical DIPs.</param>
    /// <returns>The Items index of the item under the pointer, or -1 if none.</returns>
    private int HitTestItemAtX(double pointerX)
    {
        if (this.context?.SourceStrip is null)
        {
            return -1;
        }

        // Iterate regular (unpinned) items in the Items collection
        var regularItems = this.context.SourceStrip.Items.Where(t => !t.IsPinned).ToList();

        for (var i = 0; i < regularItems.Count; i++)
        {
            var item = regularItems[i];
            var itemIndex = this.context.SourceStrip.Items.IndexOf(item);

            var left = this.GetItemLeftPosition(itemIndex);
            var width = this.GetItemWidth(itemIndex);
            var right = left + width;

            if (pointerX >= left && pointerX < right)
            {
                return itemIndex;
            }
        }

        return -1; // Not over any item
    }

    /// <summary>
    ///     Checks if pointer is crossing a midpoint forward (moving right).
    /// </summary>
    /// <param name="pointerX">Current pointer X position.</param>
    /// <param name="targetItemIndex">The item index under the pointer.</param>
    /// <returns>True if crossing forward, false otherwise.</returns>
    private bool IsCrossingMidpointForward(double pointerX, int targetItemIndex)
    {
        if (targetItemIndex != this.dropIndex + 1)
        {
            return false; // Not the next item to the right
        }

        var targetLeft = this.GetItemLeftPosition(targetItemIndex);
        var targetWidth = this.GetItemWidth(targetItemIndex);
        var midpoint = targetLeft + (targetWidth / 2.0);

        var wasLeftOfMidpoint = this.lastPointerX < midpoint;
        var nowRightOfMidpoint = pointerX >= midpoint;

        return wasLeftOfMidpoint && nowRightOfMidpoint;
    }

    /// <summary>
    ///     Checks if pointer is crossing a midpoint backward (moving left, reversing).
    /// </summary>
    /// <param name="pointerX">Current pointer X position.</param>
    /// <returns>True if crossing backward, false otherwise.</returns>
    private bool IsCrossingMidpointBackward(double pointerX)
    {
        if (this.pushedItemsStack.Count == 0)
        {
            return false;
        }

        var topPushedItem = this.pushedItemsStack.Peek();
        var itemIndex = topPushedItem.ItemIndex;

        var itemLeft = this.GetItemLeftPosition(itemIndex);
        var itemWidth = this.GetItemWidth(itemIndex);
        var midpoint = itemLeft + (itemWidth / 2.0);

        var wasRightOfMidpoint = this.lastPointerX >= midpoint;
        var nowLeftOfMidpoint = pointerX < midpoint;

        return wasRightOfMidpoint && nowLeftOfMidpoint;
    }

    /// <summary>
    ///     Maps an Items index to a repeater index by skipping pinned items.
    /// </summary>
    /// <param name="itemIndex">Index in Items collection.</param>
    /// <returns>The corresponding repeater index.</returns>
    private int MapItemsIndexToRepeaterIndex(int itemIndex)
    {
        if (this.context?.SourceStrip is null || itemIndex < 0)
        {
            return -1;
        }

        var repeaterIndex = 0;
        for (var i = 0; i < itemIndex; i++)
        {
            if (!this.context.SourceStrip.Items[i].IsPinned)
            {
                repeaterIndex++;
            }
        }

        return repeaterIndex;
    }

    /// <summary>
    ///     Computes the left position from layout when container is not realized.
    /// </summary>
    /// <param name="itemIndex">Index in Items collection.</param>
    /// <returns>Estimated left position in logical DIPs.</returns>
    private double ComputeLeftFromLayout(int itemIndex)
    {
        if (this.context?.SourceStrip is null)
        {
            return 0;
        }

        var repeater = this.context.SourceStrip.GetRegularItemsRepeater();
        if (repeater?.Layout is not StackLayout layout)
        {
            return 0;
        }

        var spacing = layout.Spacing;

        // Sum up the actual widths of all previous regular items plus spacing between them
        var position = 0.0;
        var regularItems = this.context.SourceStrip.Items.Where(t => !t.IsPinned).ToList();

        for (var i = 0; i < regularItems.Count; i++)
        {
            var currentItem = regularItems[i];
            var currentItemIndex = this.context.SourceStrip.Items.IndexOf(currentItem);

            if (currentItemIndex == itemIndex)
            {
                // Found our target item
                break;
            }

            // Add spacing before this item (except for the first item)
            if (i > 0)
            {
                position += spacing;
            }

            // Add this item's width
            var width = this.GetItemWidth(currentItemIndex);
            position += width;
        }

        return position;
    }

    /// <summary>
    ///     Converts a physical screen point to strip-relative coordinates.
    /// </summary>
    /// <param name="screenPoint">Physical screen point.</param>
    /// <returns>Strip-relative point in logical DIPs.</returns>

    /// <summary>
    ///     Struct to track pushed items on the stack.
    /// </summary>
    private struct PushedItemInfo
    {
        /// <summary>Gets the index in Items collection.</summary>
        public int ItemIndex { get; init; }

        /// <summary>Gets the item's left position before push (strip coords).</summary>
        public double OriginalLeft { get; init; }

        /// <summary>Gets the direction the item was pushed.</summary>
        public PushDirection Direction { get; init; }
    }

    /// <summary>
    ///     Direction an item was pushed.
    /// </summary>
    private enum PushDirection
    {
        /// <summary>Item pushed to the right (covering dragged shell to its right).</summary>
        Forward,

        /// <summary>Item pushed to the left (covering dragged shell to its left).</summary>
        Backward,
    }
}
