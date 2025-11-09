// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System;
using System.Diagnostics;
using DroidNet.Coordinates;
using Microsoft.Extensions.Logging;
using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;

namespace DroidNet.Aura.Drag;

/// <summary>
///     Strategy for handling in-TabStrip drag operations using transforms.
///     The dragged item follows the pointer. Adjacent items translate to make space.
/// </summary>
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
            this.LogIgnored("InitiateDrag", "a drag operation is already ongoing.");
            throw new InvalidOperationException("A drag operation is already ongoing.");
        }

        this.LogEnterReorderMode(position);

        this.context = context;
        var elementPosition = this.context.SpatialMapper.ToElement(position);
        this.layout = new(context, elementPosition, this.logger);
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
            if (drop)
            {
                return this.CommitDrop();
            }

            this.LogDragCompletedNoDrop();
            this.ResetTransforms();
            return null;
        }
        finally
        {
            this.ClearState();
        }
    }

    private int CommitDrop()
    {
        var (dragIndex, dropIndex) = this.layout!.GetCommittedIndices();
        this.LogDrop(dragIndex, dropIndex);

        this.ResetTransforms();

        if (dropIndex == -1 || dragIndex == dropIndex)
        {
            return dragIndex;
        }

        this.context!.TabStrip!.MoveItem(dragIndex, dropIndex);

        this.LogDropSuccess(dropIndex);
        return dropIndex;
    }

    private void ResetTransforms()
    {
        if (this.layout is null || this.context?.TabStrip is null)
        {
            return;
        }

        foreach (var item in this.layout.Items)
        {
            this.ResetSnapshotTransform(item);
        }
    }

    private void ResetSnapshotTransform(TabStripItemSnapshot item)
    {
        try
        {
            this.context!.TabStrip!.ApplyTransformToItem(item.ItemIndex, 0);
        }
#pragma warning disable CA1031 // Do not catch general exception types
        catch (Exception ex)
        {
            Debug.WriteLine($"[ReorderStrategy] ResetTransforms failed for index={item.ItemIndex}: {ex}");
            _ = TryReset(item.Container);
            return;
        }
#pragma warning restore CA1031 // Do not catch general exception types

        _ = TryReset(item.Container);

        static bool TryReset(FrameworkElement? container)
        {
            if (container is null)
            {
                return false;
            }

            var transform = container.RenderTransform;

            switch (transform)
            {
                case null:
                    return false;
                case TranslateTransform translate:
                    translate.X = 0;
                    return true;
                case CompositeTransform composite:
                    composite.TranslateX = 0;
                    return true;
                case MatrixTransform matrixTransform:
                    var matrix = matrixTransform.Matrix;
                    matrix.OffsetX = 0;
                    matrixTransform.Matrix = matrix;
                    return true;
                default:
                    return false;
            }
        }
    }

    private void ClearState()
    {
        this.context = null;
        this.layout = null;
    }
}
