// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Strategy for handling in-TabStrip drag operations using stack-based content transforms.
///     This strategy manages reordering within TabStrip bounds using transforms to slide content
///     between shells, with a stack tracking pushed items for reversibility.
/// </summary>
internal partial class ReorderStrategy : IDragStrategy
{
    private readonly ILogger logger;

    private DragContext? context;

    // Item tracking
    private ReorderLayout? layout;

    /// <summary>
    ///     Initializes a new instance of the <see cref="ReorderStrategy"/> class.
    /// </summary>
    /// <param name="loggerFactory">The logger factory for creating loggers.</param>
    public ReorderStrategy(ILoggerFactory? loggerFactory = null)
    {
        this.logger = loggerFactory?.CreateLogger<ReorderStrategy>() ?? NullLogger<ReorderStrategy>.Instance;
        this.LogCreated();
    }

    /// <summary>
    ///    Gets a value indicating whether gets the layout items being tracked during the reorder operation.
    /// </summary>
    internal bool IsActive => this.context is not null;

    /// <inheritdoc/>
    public void InitiateDrag(DragContext context, SpatialPoint<PhysicalScreenSpace> position)
    {
        ArgumentNullException.ThrowIfNull(context);
        Debug.Assert(context.TabStrip is not null, "TabStrip must be set in DragContext");

        if (this.IsActive)
        {
            this.LogIgnored("InitiateDrag", "a drag operation is already ongoing.");
            return;
        }

        this.LogEnterReorderMode(position);

        this.context = context;
        this.layout = new(context, context.SpatialMapper.ToElement(position));
    }

    /// <inheritdoc/>
    public void OnDragPositionChanged(SpatialPoint<PhysicalScreenSpace> position)
    {
        if (!this.IsActive)
        {
            this.LogIgnored("DragPositionChanged", "a drag operation is already ongoing.");
            return;
        }

        Debug.Assert(this.layout is not null, "Layout must be initialized during active drag");
        Debug.Assert(this.context is not null, "Context must be set during active drag");

        var elementPosition = this.context.SpatialMapper.ToElement(position);
        var (displacedItem, direction) = this.layout.UpdateDrag(elementPosition);
        if (displacedItem is not null)
        {
            this.LogItemDisplaced(displacedItem.ItemIndex, direction);

            // Use the interface method to apply transform
            this.context.TabStrip!.ApplyTransformToItem(displacedItem.ItemIndex, displacedItem.Offset);
        }
    }

    /// <inheritdoc/>
    public int? CompleteDrag(bool drop)
    {
        if (!drop)
        {
            this.LogDragCompletedNoDrop();

            // With no drop, completing the drag simply resets state
            this.context = null;
            this.layout = null;
        }

        if (!this.IsActive)
        {
            this.LogIgnored("CompleteDrag", "no drag operation is ongoing");
            return null;
        }

        Debug.Assert(this.layout is not null, "Layout must be initialized during active drag");

        var (dragIndex, dropIndex) = this.layout.GetCommittedIndices();

        this.LogDrop(dragIndex, dropIndex);

        // Use interface methods to manipulate items
        Debug.Assert(this.context?.TabStrip is not null, "TabStrip must be set in DragContext");
        this.context.TabStrip.RemoveItemAt(dragIndex);
        this.context.TabStrip.InsertItemAt(dropIndex, this.context.DraggedItem);

        // Reset all transforms using interface method
        for (var i = 0; i < this.layout.Items.Count; i++)
        {
            this.context.TabStrip.ApplyTransformToItem(this.layout.Items[i].ItemIndex, 0);
        }

        this.LogDropSuccess(dropIndex);

        // Reset state
        this.context = null;
        this.layout = null;

        return dropIndex;
    }

    private sealed class ReorderLayout
    {
        private readonly List<TabStripItemSnapshot> items;
        private readonly double grabOffsetX;

        private int lastDropTargetVisualIndex;

        public ReorderLayout(DragContext context, SpatialPoint<ElementSpace> grabPoint)
        {
            Debug.Assert(context is { TabStrip: { } }, message: "Context must be set to take snapshot");

            // Use the interface method to take snapshot
            this.items = [.. context.TabStrip.TakeSnapshot()];

            // Find the dragged item in the snapshot using its ItemIndex from context
            this.DraggedItemVisualIndex = this.items.FindIndex(i => i.ItemIndex == context.DraggedItemIndex);
            Debug.Assert(this.DraggedItemVisualIndex >= 0, "Dragged item must be found in TabStrip items");
            this.lastDropTargetVisualIndex = this.DraggedItemVisualIndex;
            Debug.WriteLine("Dragged item visual index: " + this.DraggedItemVisualIndex);

            var draggedItem = this.items[this.DraggedItemVisualIndex];
            this.grabOffsetX = grabPoint.Point.X - draggedItem.LayoutOrigin.Point.X;
            Debug.WriteLine("Grab offset X: " + this.grabOffsetX);
        }

        public int DraggedItemVisualIndex { get; }

        public IReadOnlyList<TabStripItemSnapshot> Items => this.items.AsReadOnly();

        public (TabStripItemSnapshot? item, string direction) UpdateDrag(SpatialPoint<ElementSpace> pointer)
        {
            var draggedItem = this.items[this.DraggedItemVisualIndex];
            Debug.WriteLine($"Pointer: {pointer}");
            Debug.WriteLine($"Dragged item logical index: {draggedItem.ItemIndex}");

            // Compute offset from original layout position
            var originalX = draggedItem.LayoutOrigin.Point.X;
            draggedItem.Offset = pointer.Point.X - originalX;
            Debug.WriteLine($"Dragged item offset: {draggedItem.Offset}");

            for (var i = 0; i < this.items.Count; i++)
            {
                if (i == this.DraggedItemVisualIndex)
                {
                    Debug.WriteLine($"Skipping dragged item at index {i}");
                    continue;
                }

                var item = this.items[i];
                var renderedStart = item.LayoutOrigin.Point.X + item.Offset;
                var renderedEnd = renderedStart + item.Width;
                Debug.WriteLine($"Checking item {i} (logical {item.ItemIndex}): renderedStart={renderedStart}, renderedEnd={renderedEnd}, pointer.X={pointer.Point.X}");

                if (pointer.Point.X >= renderedStart && pointer.Point.X < renderedEnd)
                {
                    var localX = pointer.Point.X - renderedStart;
                    Debug.WriteLine($"Pointer inside item {i}, localX={localX}, halfWidth={item.Width / 2}");
                    if (localX > item.Width / 2)
                    {
                        double direction = i < this.DraggedItemVisualIndex ? -1 : 1;
                        var dir = direction < 0 ? "left" : "right";
                        item.Offset += direction * item.Width;
                        this.lastDropTargetVisualIndex = i;
                        Debug.WriteLine($"Displacing item {i} to the {dir}, new offset={item.Offset}");

                        // Return the displayed item index
                        return (item, dir);
                    }

                    Debug.WriteLine($"Pointer in left half of item {i}, no displacement");
                }
            }

            Debug.WriteLine("No displacement detected");
            return (null, string.Empty);
        }

        public (int dragIndex, int dropIndex) GetCommittedIndices()
        {
            var dragIndex = this.items[this.DraggedItemVisualIndex].ItemIndex;
            var dropIndex = this.items[this.lastDropTargetVisualIndex].ItemIndex;
            return (dragIndex, dropIndex);
        }
    }
}
