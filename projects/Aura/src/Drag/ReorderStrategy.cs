// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Strategy for handling in-TabStrip drag operations using transforms. The dragged item follows
///     the pointer. Adjacent items translate to make space.
/// </summary>
/// <remarks>
///    <b>Invariants and assumptions:</b>
///    <list type="bullet">
///      <item>This strategy operates within a single <see cref="ITabStrip"/>.</item>
///      <item>
///        Its scope of a drag operation is delimited between <see cref="InitiateDrag"/> and <see
///        cref="CompleteDrag(bool)"/>. Within that scope, it expects the <see cref="DragContext"/>,
///        the dragged item, and the strip items collection, to not change.
///      </item>
///    </list>
/// </remarks>
internal partial class ReorderStrategy : IDragStrategy
{
    private readonly ILogger logger;

    private DragContext? context;
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
    ///     Gets a value indicating whether a drag operation is currently active.
    /// </summary>
    internal bool IsActive => this.context is not null;

    /// <inheritdoc/>
    public void InitiateDrag(DragContext context, SpatialPoint<PhysicalScreenSpace> position)
    {
        ArgumentNullException.ThrowIfNull(context);
        Debug.Assert(context.TabStrip is not null, "TabStrip must be set in DragContext");

        if (this.IsActive)
        {
            this.LogDragOngoing();
            throw new InvalidOperationException("A drag operation is already active.");
        }

        this.LogEnterReorderMode(position);

        this.context = context;
        this.layout = new(context, this.context.SpatialMapper.ToElement(position), this.logger);
    }

    /// <inheritdoc/>
    public void OnDragPositionChanged(SpatialPoint<PhysicalScreenSpace> position)
    {
        if (!this.IsActive)
        {
            this.LogIgnored("DragPositionChanged", "no drag operation is active");
            return;
        }

        Debug.Assert(this.layout is not null, "Layout must be initialized during active drag");
        Debug.Assert(this.context is not null, "Context must be set during active drag");

        var elementPosition = this.context.SpatialMapper.ToElement(position);
        this.layout.UpdateDrag(elementPosition);
    }

    /// <inheritdoc/>
    public int? CompleteDrag(bool drop)
    {
        if (!this.IsActive)
        {
            this.LogIgnored("CompleteDrag", "no drag operation is ongoing");
            return null;
        }

        Debug.Assert(this.layout is not null, "Layout must be initialized during active drag");
        Debug.Assert(this.context?.TabStrip is not null, "TabStrip must be set in DragContext");

        try
        {
            this.ResetTransforms();

            if (drop)
            {
                // Handle dropping back to the original position or no valid drop target, and do not trigger
                // unnecessary changes to the TabStrip items collection.
                var dropIndex = this.layout.GetCommittedDropIndex();
                var dragIndex = this.context!.TabStrip!.IndexOf(this.context.DraggedItemData);

                if (dropIndex == -1 || dragIndex == dropIndex)
                {
                    return dragIndex;
                }

                this.context!.TabStrip!.MoveItem(dragIndex, dropIndex);
                this.LogDragCompletedWithDrop(dragIndex, dropIndex);
                return dropIndex;
            }

            this.LogDragCompletedNoDrop();
            return null;
        }
        finally
        {
            // Reset the state for a new drag operation.
            this.context = null;
            this.layout = null;
        }
    }

    private void ResetTransforms()
    {
        if (this.context is not { TabStrip: { } strip } || this.layout is null)
        {
            this.LogIgnored("ResetTransforms", "no drag operation is ongoing");
            return;
        }

        foreach (var item in this.layout.Items)
        {
            strip.ApplyTransformToItem(item.ContentId, 0);
        }
    }
}
