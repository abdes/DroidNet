// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Mvvm;
using DroidNet.Mvvm.Generators;
using Microsoft.Extensions.Logging;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.ApplicationModel.DataTransfer;
using Windows.System;
using Windows.UI.Core;

namespace DroidNet.Controls;

/// <summary>
///     A control to display a tree as a list of expandable items.
/// </summary>
[TemplatePart(Name = TreeItemPart, Type = typeof(DynamicTreeItem))]
[TemplatePart(Name = ItemsRepeaterPart, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = RootGridPart, Type = typeof(Grid))]
[ViewModel(typeof(DynamicTreeViewModel))]
public partial class DynamicTree : Control
{
    /// <summary>
    /// The name of the root grid part in the DynamicTree control template.
    /// </summary>
    public const string RootGridPart = "PartRootGrid";

    /// <summary>
    /// The name of the ItemsRepeater part in the DynamicTree control template.
    /// </summary>
    public const string ItemsRepeaterPart = "PartItemsRepeater";

    /// <summary>
    /// The name of an instantiated tree item part within the DynamicTree item template.
    /// Use to locate the nested <see cref="DynamicTreeItem" /> within the ItemsRepeater.
    /// </summary>
    public const string TreeItemPart = "PartTreeItem";

    private const double DropReorderBand = 0.25;
    private static readonly TimeSpan HoverExpandDelay = TimeSpan.FromMilliseconds(600);

    private ILogger? logger;

    private DynamicTree? dragOwner;
    private List<TreeItemAdapter>? draggedItems;
    private DispatcherTimer? hoverExpandTimer;
    private TreeItemAdapter? hoverExpandTarget;
    private FrameworkElement? dropIndicatorElement;

    private ItemsRepeater? itemsRepeater;
    private Grid? rootGrid;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DynamicTree" /> class.
    /// </summary>
    public DynamicTree()
    {
        this.DefaultStyleKey = typeof(DynamicTree);

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    private enum DropZone
    {
        Before,
        Inside,
        After,
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.itemsRepeater is not null)
        {
            this.itemsRepeater.ElementPrepared -= this.ItemsRepeater_OnElementPrepared;
            this.itemsRepeater.ElementClearing -= this.ItemsRepeater_OnElementClearing;
        }

        this.rootGrid?.RemoveHandler(PointerPressedEvent, new PointerEventHandler(this.OnPointerPressed));

        if (this.GetTemplateChild(ItemsRepeaterPart) is not ItemsRepeater itemsRepeaterPart)
        {
            throw new InvalidOperationException($"{ItemsRepeaterPart} not found in the control template.");
        }

        this.itemsRepeater = itemsRepeaterPart;
        this.rootGrid = this.GetTemplateChild(RootGridPart) as Grid;

        this.itemsRepeater.ElementPrepared += this.ItemsRepeater_OnElementPrepared;
        this.itemsRepeater.ElementClearing += this.ItemsRepeater_OnElementClearing;

        // Hook events that will check for clicks on empty space inside the ItemsRepeater
        // Use AddHandler to be able to handle the events even if something inside ItemsRepeater is also doing it.
        this.rootGrid?.AddHandler(
            PointerPressedEvent,
            new PointerEventHandler(this.OnPointerPressed),
            handledEventsToo: true);
    }

    private static bool IsControlKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Control)
        .HasFlag(CoreVirtualKeyStates.Down);

    private static bool IsShiftKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Shift)
        .HasFlag(CoreVirtualKeyStates.Down);

    private static bool IsAncestorOf(ITreeItem ancestor, TreeItemAdapter descendant)
    {
        var current = descendant.Parent;
        while (current is not null)
        {
            if (ReferenceEquals(current, ancestor))
            {
                return true;
            }

            current = current.Parent;
        }

        return false;
    }

    private static DropZone GetDropZone(FrameworkElement element, DragEventArgs args)
    {
        var position = args.GetPosition(element);
        var height = Math.Max(element.ActualHeight, 1);
        var band = height * DropReorderBand;

        return position.Y < band
            ? DropZone.Before
            : position.Y > height - band ? DropZone.After : DropZone.Inside;
    }

    private void OnLoaded(object? sender, RoutedEventArgs args)
    {
        // Handle key bindings
        this.KeyDown += this.OnKeyDown;

        // Subscribe to view model change notifications and attach to any existing ViewModel
        this.ViewModelChanged += this.OnViewModelChanged;
        this.OnViewModelChanged(this, new ViewModelChangedEventArgs<DynamicTreeViewModel>(oldValue: null));
    }

    private void OnUnloaded(object? sender, RoutedEventArgs args)
    {
        // Remove handlers that were added on load
        this.KeyDown -= this.OnKeyDown;

        // Remove subscriptions to the ViewModel
        if (this.ViewModel is not null)
        {
            this.ViewModel.PropertyChanged -= this.ViewModel_OnPropertyChanged;
        }

        // Stop listening to the ViewModelChanged event while unloaded
        this.ViewModelChanged -= this.OnViewModelChanged;

        this.ClearDragState();
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (this.rootGrid is null)
        {
            return;
        }

        // Get the point where the pointer was pressed
        var point = args.GetCurrentPoint(this.rootGrid).Position;

        // Transform the point to the root visual
        var transform = this.rootGrid.TransformToVisual(visual: null);
        var transformedPoint = transform.TransformPoint(point);

        // Check if the visual tree has an items repeater at the point where the pointer was pressed
        var elements = VisualTreeHelper.FindElementsInHostCoordinates(transformedPoint, this.itemsRepeater);
        if (!elements.Any())
        {
            // Pointer pressed outside the items => clear selection
            this.ViewModel!.SelectNoneCommand.Execute(parameter: null);
            _ = this.Focus(FocusState.Keyboard);
        }
    }

    [SuppressMessage("Style", "IDE0010:Add missing cases", Justification = "we only handle some keys")]
    private void OnKeyDown(object sender, KeyRoutedEventArgs args)
    {
        this.LogKeyDown(args.Key);
        switch (args.Key)
        {
            case VirtualKey.A when IsControlKeyDown():
                this.ViewModel!.ToggleSelectAll();

                return;

            case VirtualKey.I when IsControlKeyDown() && IsShiftKeyDown():
                this.ViewModel!.InvertSelectionCommand.Execute(parameter: null);
                return;
        }
    }

    private void ItemsRepeater_OnElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        if (args.Element is not FrameworkElement element)
        {
            return;
        }

        // Log element clearing
        this.LogElementClearing(element);

        element.PointerPressed -= this.TreeItem_PointerPressed;
        element.Tapped -= this.TreeItem_Tapped;
        element.DragStarting -= this.TreeItem_DragStarting;
        element.DragOver -= this.TreeItem_DragOver;
        element.DragLeave -= this.TreeItem_DragLeave;
        element.Drop -= this.TreeItem_Drop;

        if (element.FindName(TreeItemPart) is not DynamicTreeItem treeItem)
        {
            return;
        }

        treeItem.DragStarting -= this.TreeItem_DragStarting;
        treeItem.Expand -= this.OnExpandTreeItem;
        treeItem.Collapse -= this.OnCollapseTreeItem;
        treeItem.DoubleTapped -= this.TreeItem_DoubleTapped;
    }

    private void ItemsRepeater_OnElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is not FrameworkElement element)
        {
            return;
        }

        // Log element prepared
        this.LogElementPrepared(element);

        if (element.FindName(TreeItemPart) is not DynamicTreeItem treeItemPart)
        {
            return;
        }

        treeItemPart.OnElementPrepared();
        treeItemPart.AllowDrop = true;
        treeItemPart.CanDrag = true;
        element.AllowDrop = true;
        element.CanDrag = true;

        // Propagate the control's view model logger factory to the realized element.
        // The ViewModel.LoggerFactory never changes after construction, so we set it once
        // when the element gets prepared.
        treeItemPart.LoggerFactory = this.ViewModel?.LoggerFactory;

        element.PointerPressed += this.TreeItem_PointerPressed;
        element.Tapped += this.TreeItem_Tapped;

        treeItemPart.Collapse += this.OnCollapseTreeItem;
        treeItemPart.Expand += this.OnExpandTreeItem;
        treeItemPart.DoubleTapped += this.TreeItem_DoubleTapped;

        treeItemPart.UpdateItemMargin();

        treeItemPart.DragStarting += this.TreeItem_DragStarting;
        element.DragStarting += this.TreeItem_DragStarting;
        element.DragOver += this.TreeItem_DragOver;
        element.DragLeave += this.TreeItem_DragLeave;
        element.Drop += this.TreeItem_Drop;
    }

    private void OnViewModelChanged(object? sender, ViewModelChangedEventArgs<DynamicTreeViewModel> args)
    {
        // Detach any handlers from the previous ViewModel
        if (args.OldValue is not null)
        {
            args.OldValue.PropertyChanged -= this.ViewModel_OnPropertyChanged;
        }

        // Attach handlers to the new ViewModel
        if (this.ViewModel is not null)
        {
            this.logger = this.ViewModel.LoggerFactory?.CreateLogger<DynamicTree>();

            this.ViewModel.PropertyChanged += this.ViewModel_OnPropertyChanged;
        }
    }

    private void ViewModel_OnPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        // No action needed for now; placeholder for future property change handling.
    }

    private void TreeItem_PointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (sender is not FrameworkElement { DataContext: TreeItemAdapter item } element)
        {
            return;
        }

        Debug.WriteLine($"Tree: TreeItem_PointerPressed - {item.Label}");

        // Get the current state of the pointer
        var pointerPoint = args.GetCurrentPoint(element);

        // Check if the pointer device is a mouse
        // Check if the left mouse button is pressed
        if (args.Pointer.PointerDeviceType != PointerDeviceType.Mouse || !pointerPoint.Properties.IsLeftButtonPressed)
        {
            return;
        }

        if (IsControlKeyDown())
        {
            // Handle Ctrl+Click
            if (item.IsSelected)
            {
                this.ViewModel!.ClearSelection(item);
            }
            else
            {
                this.ViewModel!.SelectItem(item);
            }
        }
        else if (IsShiftKeyDown())
        {
            // Handle Shift+Click
            this.ViewModel!.ExtendSelectionTo(item);
        }
        else if (!item.IsSelected)
        {
            // Press on an unselected item selects it exclusively
            this.ViewModel!.ClearAndSelectItem(item);
        }

        // Give focus to the clicked element
        _ = element.Focus(FocusState.Programmatic);
    }

    private void TreeItem_Tapped(object sender, TappedRoutedEventArgs args)
    {
        if (sender is not FrameworkElement { DataContext: TreeItemAdapter item } element)
        {
            return;
        }

        // When modifier keys are pressed, defer to pointer logic (Ctrl/Shift multi-select)
        if (IsControlKeyDown() || IsShiftKeyDown())
        {
            return;
        }

        // Tap always makes the tapped item the single selection
        this.ViewModel?.ClearAndSelectItem(item);
        _ = element.Focus(FocusState.Programmatic);
    }

    // ReSharper disable once MemberCanBeMadeStatic.Local
    private void TreeItem_DoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        args.Handled = true;

        // TODO: decide what to do when tree item is double tapped
        this.LogDoubleTapped(args.OriginalSource);
    }

    private void TreeItem_DragStarting(object sender, DragStartingEventArgs args)
    {
        if (this.ViewModel is null || sender is not FrameworkElement { DataContext: TreeItemAdapter item })
        {
            return;
        }

        var items = this.GetDragItems(item);
        if (items.Count == 0)
        {
            return;
        }

        this.dragOwner = this;
        this.draggedItems = items;

        args.Data.RequestedOperation = DataPackageOperation.Move;
        args.Data.SetData("application/vnd.droidnet.dynamictree", "move");
    }

    private void TreeItem_DragOver(object sender, DragEventArgs args)
    {
        if (!this.TryResolveDropTarget(sender, args, out var target, out var zone))
        {
            args.AcceptedOperation = DataPackageOperation.None;
            this.CancelHoverExpand();
            this.ClearDropIndicatorVisual();
            return;
        }

        args.AcceptedOperation = DataPackageOperation.Move;
        args.Handled = true;

        if (sender is FrameworkElement element && element.FindName(TreeItemPart) is DynamicTreeItem treeItem)
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

    private async void TreeItem_Drop(object sender, DragEventArgs args)
    {
        if (!this.TryResolveDropTarget(sender, args, out var target, out var zone) || this.draggedItems is null)
        {
            this.ClearDragState();
            args.AcceptedOperation = DataPackageOperation.None;
            return;
        }

        args.AcceptedOperation = DataPackageOperation.Move;
        args.Handled = true;
        this.CancelHoverExpand();
        this.ClearDropIndicatorVisual();

        try
        {
            if (zone == DropZone.Inside)
            {
                await this.ViewModel!.MoveItemsAsync(this.draggedItems, target, target.ChildrenCount)
                    .ConfigureAwait(true);
            }
            else
            {
                var parent = target.Parent;
                if (parent is null)
                {
                    return;
                }

                var children = await parent.Children.ConfigureAwait(true);
                var targetIndex = children.IndexOf(target);
                if (targetIndex < 0)
                {
                    return;
                }

                if (zone == DropZone.After)
                {
                    targetIndex++;
                }

                await this.ViewModel!.MoveItemsAsync(this.draggedItems, parent, targetIndex).ConfigureAwait(true);
            }
        }
        finally
        {
            this.ClearDragState();
        }
    }

    private void TreeItem_DragLeave(object sender, DragEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.CancelHoverExpand();
        this.ClearDropIndicatorVisual();
    }

    private async void OnExpandTreeItem(object? sender, DynamicTreeEventArgs args)
        => await this.ViewModel!.ExpandItemAsync(args.TreeItem).ConfigureAwait(true);

    private async void OnCollapseTreeItem(object? sender, DynamicTreeEventArgs args)
        => await this.ViewModel!.CollapseItemAsync(args.TreeItem).ConfigureAwait(true);

    private List<TreeItemAdapter> GetDragItems(TreeItemAdapter primary)
    {
        if (this.ViewModel is null)
        {
            return [];
        }

        var selectedItems = this.ViewModel.ShownItems
            .OfType<TreeItemAdapter>()
            .Where(item => item.IsSelected)
            .ToList();

        return selectedItems.Count > 0 && selectedItems.Contains(primary)
            ? selectedItems
            : [primary];
    }

    private bool TryResolveDropTarget(
        object? sender,
        DragEventArgs args,
        [NotNullWhen(true)] out TreeItemAdapter? target,
        out DropZone zone)
    {
        target = null;
        zone = DropZone.Inside;

        if (!ReferenceEquals(this.dragOwner, this) || this.draggedItems is null || this.ViewModel is null)
        {
            return false;
        }

        if (sender is not FrameworkElement { DataContext: TreeItemAdapter item } element)
        {
            return false;
        }

        zone = GetDropZone(element, args);

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
            var parent = item.Parent;
            if (parent is null || !this.draggedItems.TrueForAll(dragged => ReferenceEquals(dragged.Parent, parent)))
            {
                return false;
            }
        }
        else if (!item.CanAcceptChildren)
        {
            return false;
        }

        target = item;
        return true;
    }

    private void ScheduleHoverExpand(TreeItemAdapter target)
    {
        if (ReferenceEquals(this.hoverExpandTarget, target) && this.hoverExpandTimer?.IsEnabled == true)
        {
            return;
        }

        this.hoverExpandTarget = target;

        this.hoverExpandTimer ??= new DispatcherTimer
        {
            Interval = HoverExpandDelay,
        };

        this.hoverExpandTimer.Tick -= this.OnHoverExpandTick;
        this.hoverExpandTimer.Tick += this.OnHoverExpandTick;
        this.hoverExpandTimer.Stop();
        this.hoverExpandTimer.Start();
    }

    private void CancelHoverExpand()
    {
        this.hoverExpandTimer?.Stop();
        this.hoverExpandTarget = null;
    }

    private async void OnHoverExpandTick(object? sender, object args)
    {
        _ = sender; // unused
        _ = args; // unused

        this.hoverExpandTimer?.Stop();
        var target = this.hoverExpandTarget;
        this.hoverExpandTarget = null;

        if (target?.IsExpanded != false || !target.CanAcceptChildren)
        {
            return;
        }

        if (this.ViewModel is null)
        {
            return;
        }

        await this.ViewModel.ExpandItemAsync(target).ConfigureAwait(true);
    }

    private void ClearDragState()
    {
        this.CancelHoverExpand();
        this.dragOwner = null;
        this.draggedItems = null;
        this.ClearDropIndicatorVisual();
    }

    private void SetDropIndicatorVisual(FrameworkElement element, DropZone zone)
    {
        var position = zone switch
        {
            DropZone.Before => DropIndicatorPosition.Before,
            DropZone.After => DropIndicatorPosition.After,
            _ => DropIndicatorPosition.None,
        };

        if (!ReferenceEquals(this.dropIndicatorElement, element))
        {
            this.ClearDropIndicatorVisual();
            this.dropIndicatorElement = element;
        }

        SetDropIndicator(element, position);
    }

    private void ClearDropIndicatorVisual()
    {
        if (this.dropIndicatorElement is null)
        {
            return;
        }

        SetDropIndicator(this.dropIndicatorElement, DropIndicatorPosition.None);
        this.dropIndicatorElement = null;
    }
}
