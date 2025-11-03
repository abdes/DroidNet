// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Diagnostics;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using Windows.Foundation;

namespace DroidNet.Controls;

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

    internal IReadOnlyList<SnapshotItem> LayoutItems => this.layout?.Items ?? ImmutableList<SnapshotItem>.Empty;

    protected bool IsActive => this.context is not null;

    /// <inheritdoc/>
    public void InitiateDrag(DragContext context, SpatialPoint<ScreenSpace> position)
    {
        ArgumentNullException.ThrowIfNull(context);
        Debug.Assert(context.TabStrip is not null, "TabStrip must be set in DragContext");

        if (this.IsActive)
        {
            this.LogAlreadyActive();
            throw new InvalidOperationException("ReorderStrategy is already active");
        }

        var localPosition = context.SpatialMapper.ToElement(position);
        this.LogEnterReorderMode(position, localPosition);

        this.context = context;

        // During re-order, we are confined to the TabStrip that initiated the drag, and it is not
        // realistic or expected to have tabs changed. Therefore, we take a visual snapshot of the strip's
        // regular items, and work with it for as long as we are re-ordering and not tearing out.
        this.layout = new(context, localPosition);

        this.LogReorderInitiated(this.layout.DraggedItemVisualIndex);

    }

    internal class SnapshotItem
    {
        public required int ItemIndex { get; init; } // Index in Items collection

        public required SpatialPoint<ElementSpace> LayoutOrigin { get; init; } // Original top left corner of the item in the TabStrip coordinates space

        public required double Width { get; init; } // Actual rendered Width of the item

        public double Offset { get; set; } // Current horizontal translation offset
    }

    private sealed class ReorderLayout
    {
        private readonly List<SnapshotItem> items;
        private readonly int draggedItemVisualIndex;
        private int lastDropTargetVisualIndex;
        private double grabOffsetX = 0;

        public int DraggedItemVisualIndex => this.draggedItemVisualIndex;

        public IReadOnlyList<SnapshotItem> Items => this.items.AsReadOnly();

        public ReorderLayout(DragContext context, SpatialPoint<ElementSpace> grabPoint)
        {
            Debug.Assert(context is { TabStrip: { } }, message: "Context must be set to take snapshot");

            this.items = TakeTabStripSnapshot(context);

            // Record the index of the dragged item in the snapshot (visual index)
            this.draggedItemVisualIndex = this.items.FindIndex(i => ReferenceEquals(context.TabStrip.Items[i.ItemIndex], context.DraggedItem));
            Debug.Assert(this.draggedItemVisualIndex >= 0, "Dragged item must be found in TabStrip items");
            this.lastDropTargetVisualIndex = this.draggedItemVisualIndex;
            Debug.WriteLine("Dragged item visual index: " + this.draggedItemVisualIndex);

            var draggedItem = this.items[this.draggedItemVisualIndex];
            this.grabOffsetX = grabPoint.Point.X - draggedItem.LayoutOrigin.Point.X;
            Debug.WriteLine("Grab offset X: " + this.grabOffsetX);
        }

        public (SnapshotItem?, string) UpdateDrag(SpatialPoint<ElementSpace> pointer)
        {
            var draggedItem = items[draggedItemVisualIndex];
            Debug.WriteLine($"Pointer: {pointer}");
            Debug.WriteLine($"Dragged item logical index: {draggedItem.ItemIndex}");

            // Compute offset from original layout position
            double originalX = draggedItem.LayoutOrigin.Point.X;
            draggedItem.Offset = pointer.Point.X - originalX;
            Debug.WriteLine($"Dragged item offset: {draggedItem.Offset}");

            for (var i = 0; i < this.items.Count; i++)
            {
                if (i == this.draggedItemVisualIndex)
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
                        double direction = i < this.draggedItemVisualIndex ? -1 : 1;
                        string dir = direction < 0 ? "left" : "right";
                        item.Offset += direction * item.Width;
                        this.lastDropTargetVisualIndex = i;
                        Debug.WriteLine($"Displacing item {i} to the {dir}, new offset={item.Offset}");

                        // Return the displayed item index
                        return (item, dir);
                    }
                    else
                    {
                        Debug.WriteLine($"Pointer in left half of item {i}, no displacement");
                    }
                }
            }

            Debug.WriteLine("No displacement detected");
            return (null, "");
        }

        public (int dragIndex, int dropIndex) GetCommittedIndices()
        {
            var dragIndex = this.items[this.draggedItemVisualIndex].ItemIndex;
            var dropIndex = this.items[this.lastDropTargetVisualIndex].ItemIndex;
            return (dragIndex, dragIndex);
        }

        [System.Diagnostics.CodeAnalysis.SuppressMessage("Style", "IDE0305:Simplify collection initialization", Justification = "not good for clarity")]
        private static List<SnapshotItem> TakeTabStripSnapshot(DragContext context)
        {
            Debug.Assert(context is { TabStrip: { } }, message: "Context must be set to take snapshot");

            var strip = context.TabStrip;
            return strip
                .GetRealizedRegularElements()
                .Select(el
                    => new SnapshotItem()
                    {
                        ItemIndex = el.index,
                        LayoutOrigin = el.element.TransformToVisual(strip).TransformPoint(new Point(0, 0)).AsElement(),
                        Width = el.element.RenderSize.Width,
                    })
                .OrderBy(i => i.LayoutOrigin.ToPoint().X)
                .ToList();
        }
    }

    /// <inheritdoc/>
    public void OnDragPositionChanged(SpatialPoint<ScreenSpace> position)
    {
        if (!this.IsActive)
        {
            this.LogMoveIgnored();
            return;
        }

        Debug.Assert(this.layout is not null, "Layout must be initialized during active drag");
        var elementPosition = this.context!.SpatialMapper.ToElement(position);
        var (displacedItem, direction) = this.layout.UpdateDrag(elementPosition);
        if (displacedItem is not null)
        {
            this.LogItemDisplaced(displacedItem.ItemIndex, direction);
            var transform = this.GetContentTransform(displacedItem.ItemIndex);
            _ = transform?.X = displacedItem.Offset;
        }
    }

    /// <inheritdoc/>
    public bool CompleteDrag()
    {
        if (!this.IsActive || this.context is null)
        {
            this.LogDropIgnored();
            return false;
        }

        Debug.Assert(this.layout is not null, "Layout must be initialized during active drag");

        var (dragIndex, dropIndex) = this.layout.GetCommittedIndices();

        this.LogCommittedIndices(dragIndex, dropIndex);
        this.LogDrop(dragIndex, dropIndex);

        // Delete the dragged item, and add it at the drop index
        Debug.Assert(this.context.TabStrip is not null, "TabStrip must be set in DragContext");
        this.context.TabStrip.Items.RemoveAt(dragIndex);
        this.context.TabStrip.Items.Insert(dropIndex, this.context.DraggedItem);

        // Reset all transforms
        for (var i = 0; i < this.context.TabStrip.Items.Count; i++)
        {
            if (this.GetContentTransform(i) is { } transform)
            {
                transform.X = 0;
            }
        }

        this.LogDropSuccess(dropIndex);

        // Reset state
        this.context = null;
        this.layout = null;

        return true;
    }

    /// <summary>
    ///     Gets the TranslateTransform for the wrapper Grid at the specified Items collection index.
    /// </summary>
    /// <param name="itemIndex">Index in Items collection.</param>
    /// <returns>The TranslateTransform for the wrapper Grid, or null if not available.</returns>
    private TranslateTransform? GetContentTransform(int itemIndex)
    {
        if (this.context?.TabStrip is null || itemIndex < 0 || itemIndex >= this.context.TabStrip.Items.Count)
        {
            return null;
        }

        var item = this.context.TabStrip.Items[itemIndex];
        var repeater = this.context.TabStrip.GetRegularItemsRepeater();
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
}
