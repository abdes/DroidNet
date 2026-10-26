// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;
using Windows.ApplicationModel.DataTransfer;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
///     Internal drag-and-drop of the <see cref="DynamicTree" /> control: reordering and reparenting items by dragging
///     their rows.
/// </summary>
public partial class DynamicTree
{
    // The fraction of a row's height, from each edge, that drops before or after the row instead of inside it.
    private const double DropReorderBand = 0.25;

    private static readonly TimeSpan HoverExpandDelay = TimeSpan.FromMilliseconds(600);

    private List<ITreeItem>? draggedItems;
    private bool dragIsCopy;
    private DispatcherTimer? hoverExpandTimer;
    private ITreeItem? hoverExpandTarget;
    private FrameworkElement? dropIndicatorElement;

    /// <summary>Describes the logical position of a drop relative to its target row.</summary>
    internal enum DropZone
    {
        /// <summary>Insert before the target.</summary>
        Before,

        /// <summary>Insert as a child of the target.</summary>
        Inside,

        /// <summary>Insert after the target.</summary>
        After,
    }

    /// <summary>Stages an internal drag without changing the tree's structure.</summary>
    /// <param name="item">The primary dragged item.</param>
    /// <param name="isCopy">Whether the drag initially requests a copy.</param>
    /// <returns>True when unlocked sources could be staged.</returns>
    internal bool BeginDrag(ITreeItem item, bool isCopy)
    {
        ArgumentNullException.ThrowIfNull(item);
        this.deferredSelection = false;
        this.deferredSelectionItem = null;
        if (this.ViewModel?.IsShown(item) != true)
        {
            return false;
        }

        var items = this.GetDragItems(item);
        if (items.Count == 0 || items.Exists(static dragged => dragged.IsLocked))
        {
            return false;
        }

        this.draggedItems = items;
        this.dragIsCopy = isCopy;
        return true;
    }

    /// <summary>Commits a valid pending drag, or rejects a drop after cancellation.</summary>
    /// <param name="target">The target row.</param>
    /// <param name="zone">The drop position relative to the target.</param>
    /// <param name="isControlDown">Whether the drop gesture holds Control, which asks for a copy.</param>
    /// <returns>The accepted operation, or None when the drop is rejected.</returns>
    internal async Task<DataPackageOperation> CompleteDragAsync(ITreeItem target, DropZone zone, bool isControlDown = false)
    {
        ArgumentNullException.ThrowIfNull(target);
        var isCopyIntent = isControlDown || this.dragIsCopy;
        var isCopyAllowed = isCopyIntent && this.CanCopyDraggedItems();
        if ((isCopyIntent && !isCopyAllowed) || !this.IsValidDropTarget(target, zone))
        {
            this.ClearDragState();
            return DataPackageOperation.None;
        }

        this.CancelHoverExpand();
        this.ClearDropIndicatorVisual();
        var owner = this.ViewModel!;
        var sources = this.draggedItems!.ToArray();
        try
        {
            var result = await ProcessDropAsync(owner, sources, target, zone, isCopyAllowed).ConfigureAwait(true);
            if (!result.Succeeded || !ReferenceEquals(this.ViewModel, owner)
                || result.Items.Count == 0 || result.Items[0] is not { } first)
            {
                return DataPackageOperation.None;
            }

            _ = owner.FocusItem(first, RequestOrigin.PointerInput);
            return isCopyAllowed ? DataPackageOperation.Copy : DataPackageOperation.Move;
        }
        catch (Exception exception) when (exception is InvalidOperationException or ArgumentException)
        {
            this.LogDropRejected(exception);

            return DataPackageOperation.None;
        }
        finally
        {
            this.ClearDragState();
        }
    }

    /// <summary>Schedules expansion while a drag hovers over a collapsed parent.</summary>
    /// <param name="target">The hovered parent.</param>
    internal void ScheduleHoverExpand(ITreeItem target)
    {
        if (ReferenceEquals(this.hoverExpandTarget, target) && this.hoverExpandTimer?.IsEnabled == true)
        {
            return;
        }

        this.hoverExpandTarget = target;

        if (this.hoverExpandTimer is null)
        {
            this.hoverExpandTimer = new DispatcherTimer
            {
                Interval = HoverExpandDelay,
            };

            this.hoverExpandTimer.Tick += this.OnHoverExpandTick;
        }

        this.hoverExpandTimer.Stop();
        this.hoverExpandTimer.Start();
    }

    /// <summary>Updates the pending drag's cue carrier and position.</summary>
    /// <param name="element">The realized target row.</param>
    /// <param name="zone">The position to display.</param>
    internal void SetDropIndicatorVisual(FrameworkElement element, DropZone zone)
    {
        var position = zone switch
        {
            DropZone.Before => DropIndicatorPosition.Before,
            DropZone.After => DropIndicatorPosition.After,
            DropZone.Inside => DropIndicatorPosition.Inside,
            _ => DropIndicatorPosition.None,
        };

        if (!ReferenceEquals(this.dropIndicatorElement, element))
        {
            this.ClearDropIndicatorVisual();
            this.dropIndicatorElement = element;
        }

        SetDropIndicator(element, position);
        if (element is DynamicTreeItem treeItem)
        {
            treeItem.UpdateDropIndicatorVisual(position);
        }
    }

    private static bool IsAncestorOf(ITreeItem ancestor, ITreeItem descendant)
    {
        for (var current = descendant.Parent; current is not null; current = current.Parent)
        {
            if (ReferenceEquals(current, ancestor))
            {
                return true;
            }
        }

        return false;
    }

    private static bool CanDropInside(ITreeItem item) => item.CanAcceptChildren && (!item.IsLocked || item.IsRoot);

    // The middle band drops inside the row, but only a row that can take children offers it: any other row maps the
    // middle band onto the half of the row the pointer is in, so that the whole row stays a valid reorder target.
    private static DropZone GetDropZone(FrameworkElement element, DragEventArgs args, ITreeItem item)
    {
        var position = args.GetPosition(element);
        var height = Math.Max(element.ActualHeight, 1);
        var band = height * DropReorderBand;

        if (position.Y < band)
        {
            return DropZone.Before;
        }

        if (position.Y > height - band)
        {
            return DropZone.After;
        }

        if (CanDropInside(item))
        {
            return DropZone.Inside;
        }

        return position.Y < height / 2 ? DropZone.Before : DropZone.After;
    }

    private static bool IsCopyRequested(DragEventArgs args)
        => args.Modifiers.HasFlag(Windows.ApplicationModel.DataTransfer.DragDrop.DragDropModifiers.Control);

    private static async Task<TreeDropResult> ProcessDropAsync(DynamicTreeViewModel owner, IReadOnlyList<ITreeItem> sources, ITreeItem target, DropZone zone, bool isCopy)
    {
        var parent = target;
        var insertIndex = target.ChildrenCount;
        if (zone != DropZone.Inside)
        {
            if (target.Parent is not { } targetParent)
            {
                return TreeDropResult.Rejected;
            }

            parent = targetParent;
            var children = await parent.Children.ConfigureAwait(true);
            insertIndex = children.IndexOf(target);
            if (insertIndex < 0)
            {
                return TreeDropResult.Rejected;
            }

            if (zone == DropZone.After)
            {
                insertIndex++;
            }
        }

        return await owner.CommitDropAsync(
            new TreeDropRequest(sources, parent, insertIndex, isCopy ? TreeDropOperation.Copy : TreeDropOperation.Move)).ConfigureAwait(true);
    }

    private void TreeItem_DragStarting(object sender, DragStartingEventArgs args)
    {
        this.deferredSelection = false;
        this.deferredSelectionItem = null;

        if (this.ViewModel is null || sender is not FrameworkElement { DataContext: ITreeItem item })
        {
            return;
        }

        if (FindTreeItemAncestor(sender as DependencyObject) is { IsInteractiveActionInProgress: true })
        {
            args.Cancel = true;
            return;
        }

        if (!this.BeginDrag(item, IsControlKeyDown()))
        {
            args.Cancel = true;
            return;
        }

        args.Data.RequestedOperation = this.dragIsCopy ? DataPackageOperation.Copy : DataPackageOperation.Move;
        args.Data.SetData(DragDataFormat, this.dragIsCopy ? "copy" : "move");
        if (this.draggedItems is { Count: > 0 } items)
        {
            this.ItemsDragStarting?.Invoke(this, new(items, args.Data));
        }
    }

    // Raised on the source row when the drag ends however it ends, including a cancelled drag that no drop target saw.
    private void TreeItem_DropCompleted(UIElement sender, DropCompletedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.ClearDragState();
    }

    private void TreeItem_DragOver(object sender, DragEventArgs args)
    {
        var isCopyIntent = IsCopyRequested(args) || this.dragIsCopy;

        if (!this.IsInternalDrag(args)
            || (isCopyIntent && !this.CanCopyDraggedItems())
            || !this.TryResolveDropTarget(sender, args, out var target, out var zone))
        {
            args.AcceptedOperation = DataPackageOperation.None;
            this.CancelHoverExpand();
            this.ClearDropIndicatorVisual();
            return;
        }

        args.AcceptedOperation = isCopyIntent ? DataPackageOperation.Copy : DataPackageOperation.Move;
        args.Handled = true;

        if (this.GetRow(sender as UIElement) is { } treeItem)
        {
            this.SetDropIndicatorVisual(treeItem, zone);
        }

        if (zone == DropZone.Inside && target.CanAcceptChildren && !target.IsExpanded)
        {
            this.ScheduleHoverExpand(target);
        }
        else
        {
            this.CancelHoverExpand();
        }
    }

    private void TreeItem_Drop(object sender, DragEventArgs args)
    {
        var isControlDown = IsCopyRequested(args);
        var isCopyIntent = isControlDown || this.dragIsCopy;

        if (!this.IsInternalDrag(args)
            || (isCopyIntent && !this.CanCopyDraggedItems())
            || !this.TryResolveDropTarget(sender, args, out var target, out var zone))
        {
            this.ClearDragState();
            args.AcceptedOperation = DataPackageOperation.None;
            return;
        }

        args.AcceptedOperation = isCopyIntent ? DataPackageOperation.Copy : DataPackageOperation.Move;
        args.Handled = true;
        var deferral = args.GetDeferral();
        this.RunGuarded(async () =>
        {
            try
            {
                args.AcceptedOperation = await this.CompleteDragAsync(target, zone, isControlDown).ConfigureAwait(true);
            }
            catch
            {
                // A failed commit must not report the operation to the drag source as done.
                args.AcceptedOperation = DataPackageOperation.None;
                throw;
            }
            finally
            {
                this.ClearDragState();
                deferral.Complete();
            }
        });
    }

    private void TreeItem_DragLeave(object sender, DragEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.CancelHoverExpand();
        this.ClearDropIndicatorVisual();
    }

    // A drop is ours only while this tree staged its own drag and the dragged data carries the tree's format: any
    // other drag, including a later external one after a cancelled drag, is not ours to accept.
    private bool IsInternalDrag(DragEventArgs args)
        => this.draggedItems is not null && args.DataView.Contains(DragDataFormat);

    private List<ITreeItem> GetDragItems(ITreeItem primary)
    {
        if (this.ViewModel is null)
        {
            return [];
        }

        var selectedItems = this.GetSelectedItems();

        return selectedItems.Count > 0 && selectedItems.Contains(primary)
            ? selectedItems
            : [primary];
    }

    private bool TryResolveDropTarget(
        object? sender,
        DragEventArgs args,
        [NotNullWhen(true)] out ITreeItem? target,
        out DropZone zone)
    {
        target = null;
        zone = DropZone.Inside;

        if (sender is not FrameworkElement { DataContext: ITreeItem item } element)
        {
            return false;
        }

        zone = GetDropZone(element, args, item);

        if (!this.IsValidDropTarget(item, zone))
        {
            return false;
        }

        target = item;
        return true;
    }

    private bool IsValidDropTarget(ITreeItem item, DropZone zone)
    {
        if (this.draggedItems is null || this.ViewModel?.IsShown(item) != true)
        {
            return false;
        }

        if (this.draggedItems.Exists(dragged => ReferenceEquals(dragged, item)))
        {
            return false;
        }

        if (this.draggedItems.Exists(dragged => IsAncestorOf(dragged, item)))
        {
            return false;
        }

        if (zone != DropZone.Inside)
        {
            if (item.Parent is not { CanAcceptChildren: true } parent || (parent.IsLocked && !parent.IsRoot))
            {
                return false;
            }
        }
        else if (!CanDropInside(item))
        {
            return false;
        }

        return !this.draggedItems.Exists(static dragged => dragged.IsLocked);
    }

    private bool CanCopyDraggedItems() => this.draggedItems is { Count: > 0 } && this.draggedItems.TrueForAll(item => item is ICanBeCloned);

    private void CancelHoverExpand()
    {
        this.hoverExpandTimer?.Stop();
        this.hoverExpandTarget = null;
    }

    private void OnHoverExpandTick(object? sender, object args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.hoverExpandTimer?.Stop();
        var target = this.hoverExpandTarget;
        this.hoverExpandTarget = null;

        if (target?.IsExpanded != false || !target.CanAcceptChildren || this.ViewModel is not { } viewModel)
        {
            return;
        }

        this.RunGuarded(() => viewModel.ExpandItemAsync(target));
    }

    private void ClearDragState()
    {
        this.CancelHoverExpand();
        this.draggedItems = null;
        this.ClearDropIndicatorVisual();
    }

    private void ClearDropIndicatorVisual()
    {
        if (this.dropIndicatorElement is null)
        {
            return;
        }

        SetDropIndicator(this.dropIndicatorElement, DropIndicatorPosition.None);
        if (this.dropIndicatorElement is DynamicTreeItem treeItem)
        {
            treeItem.UpdateDropIndicatorVisual(DropIndicatorPosition.None);
        }

        this.dropIndicatorElement = null;
    }
}
