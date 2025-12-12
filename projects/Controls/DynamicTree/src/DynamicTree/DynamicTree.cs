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
    private bool dragIsCopy;
    private DispatcherTimer? hoverExpandTimer;
    private TreeItemAdapter? hoverExpandTarget;
    private FrameworkElement? dropIndicatorElement;

    private ItemsRepeater? itemsRepeater;
    private Grid? rootGrid;

    private FocusRequestOrigin lastFocusRequestOrigin = FocusRequestOrigin.None;

    private bool isApplyingFocus;
    private bool focusOperationPending;

    /// <summary>
    ///     Initializes a new instance of the <see cref="DynamicTree" /> class.
    /// </summary>
    public DynamicTree()
    {
        this.DefaultStyleKey = typeof(DynamicTree);

        this.IsTabStop = true;

        this.Loaded += this.OnLoaded;
        this.Unloaded += this.OnUnloaded;
    }

    private enum FocusRequestOrigin
    {
        None,
        Keyboard,
        Programmatic,
    }

    private enum DropZone
    {
        Before,
        Inside,
        After,
    }

    /// <summary>
    /// Overrideable hook for pointer-press handling on an item.
    /// </summary>
    /// <returns><see langword="true"/> if handled.</returns>
    /// <param name="item">The item that was pressed.</param>
    /// <param name="isControlDown">Whether the Control key is down.</param>
    /// <param name="isShiftDown">Whether the Shift key is down.</param>
    /// <param name="leftButtonPressed">Whether the left mouse button is pressed.</param>
    protected internal virtual bool OnItemPointerPressed(TreeItemAdapter item, bool isControlDown, bool isShiftDown, bool leftButtonPressed)
    {
        if (!leftButtonPressed || this.ViewModel is null)
        {
            return false;
        }

        if (isControlDown)
        {
            if (item.IsSelected)
            {
                this.ViewModel.ClearSelection(item);
            }
            else
            {
                this.ViewModel.SelectItem(item);
            }
        }
        else if (isShiftDown)
        {
            this.ViewModel.ExtendSelectionTo(item);
        }
        else if (!item.IsSelected)
        {
            this.ViewModel.ClearAndSelectItem(item);
        }

        this.lastFocusRequestOrigin = FocusRequestOrigin.Programmatic;
        return true;
    }

    /// <summary>
    /// Overrideable hook for tap handling on an item.
    /// </summary>
    /// <returns><see langword="true"/> if handled.</returns>
    /// <param name="item">The item that was tapped.</param>
    /// <param name="isControlDown">Whether the Control key is down.</param>
    /// <param name="isShiftDown">Whether the Shift key is down.</param>
    protected internal virtual bool OnItemTapped(TreeItemAdapter item, bool isControlDown, bool isShiftDown)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        // When control/shift is down, let pointer handlers handle multi-select behavior.
        if (isControlDown || isShiftDown)
        {
            return false;
        }

        // Tap always makes the tapped item the single selection.
        this.ViewModel.ClearAndSelectItem(item);

        this.lastFocusRequestOrigin = FocusRequestOrigin.Programmatic;
        var forceRaise = ReferenceEquals(this.ViewModel.FocusedItem, item);
        this.LogRequestModelFocusWrapper(item, this.lastFocusRequestOrigin, forceRaise);
        _ = this.ViewModel.FocusItem(item, forceRaise: forceRaise);
        return true;
    }

    /// <summary>
    /// Overrideable hook for when an item receives platform focus.
    /// </summary>
    /// <returns><see langword="true"/> if handled.</returns>
    /// <param name="item">The item that received focus.</param>
    /// <param name="isApplyingFocus">True if the control is currently applying focus programmatically.</param>
    protected internal virtual bool OnItemGotFocus(TreeItemAdapter item, bool isApplyingFocus)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        if (isApplyingFocus)
        {
            // When we're applying focus programmatically ignore this GotFocus to avoid a feedback loop.
            return false;
        }

        if (ReferenceEquals(this.ViewModel.FocusedItem, item))
        {
            return false;
        }

        if (this.lastFocusRequestOrigin == FocusRequestOrigin.None)
        {
            this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;
        }

        this.LogRequestModelFocusWrapper(item, this.lastFocusRequestOrigin, forceRaise: false);
        _ = this.ViewModel.FocusItem(item);
        return true;
    }

    /// <summary>
    ///     Handles keyboard input for the tree control.
    /// </summary>
    /// <param name="key">The virtual key that was pressed.</param>
    /// <param name="isControlDown">True when the Control key is held down during the key event.</param>
    /// <param name="isShiftDown">True when the Shift key is held down during the key event.</param>
    /// <returns>
    ///     A task that completes with <see langword="true"/> when the key was handled by the control,
    ///     otherwise <see langword="false"/>.
    /// </returns>
    /// <remarks>
    ///     This method implements the control's keyboard command routing and is designed to be
    ///     invoked both from platform key event handlers (which compute modifier state from the
    ///     platform) and directly from tests. It delegates to various ViewModel operations for
    ///     navigation, selection and clipboard shortcuts.
    /// </remarks>
    protected internal virtual async Task<bool> HandleKeyDownAsync(VirtualKey key, bool isControlDown, bool isShiftDown)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        _ = this.ViewModel.EnsureFocus();

        switch (key)
        {
            case VirtualKey.A when isControlDown:
                this.ViewModel.ToggleSelectAll();
                return true;

            case VirtualKey.I when isControlDown && isShiftDown:
                this.ViewModel.InvertSelectionCommand.Execute(parameter: null);
                return true;

            case VirtualKey.C when isControlDown:
                return await this.HandleCopyShortcutAsync().ConfigureAwait(true);

            case VirtualKey.X when isControlDown:
                return await this.HandleCutShortcutAsync().ConfigureAwait(true);

            case VirtualKey.V when isControlDown:
                return await this.HandlePasteShortcutAsync().ConfigureAwait(true);

            case VirtualKey.Up:
                return await this.MoveFocusAsync(FocusNavigationDirection.Up).ConfigureAwait(true);

            case VirtualKey.Down:
                return await this.MoveFocusAsync(FocusNavigationDirection.Down).ConfigureAwait(true);

            case VirtualKey.Left:
                return await this.HandleCollapseAsync().ConfigureAwait(true);

            case VirtualKey.Right:
                return await this.HandleExpandAsync().ConfigureAwait(true);

            case VirtualKey.Home:
                return await this.HandleHomeAsync(isControlDown).ConfigureAwait(true);

            case VirtualKey.End:
                return await this.HandleEndAsync(isControlDown).ConfigureAwait(true);

            case VirtualKey.Enter:
            case VirtualKey.Space:
                return this.ViewModel.ToggleSelectionForFocused(isControlDown, isShiftDown);
        }

        return false;
    }

    /// <inheritdoc />
    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.itemsRepeater is not null)
        {
            this.itemsRepeater.RemoveHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown));
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

        this.itemsRepeater.AddHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown), handledEventsToo: true);

        this.itemsRepeater.ElementPrepared += this.ItemsRepeater_OnElementPrepared;
        this.itemsRepeater.ElementClearing += this.ItemsRepeater_OnElementClearing;

        // Hook events that will check for clicks on empty space inside the ItemsRepeater
        // Use AddHandler to be able to handle the events even if something inside ItemsRepeater is also doing it.
        this.rootGrid?.AddHandler(
            PointerPressedEvent,
            new PointerEventHandler(this.OnPointerPressed),
            handledEventsToo: true);
    }

    /// <summary>
    ///     When the tree control itself gains focus, move keyboard focus to the selected item if present, otherwise ensure
    ///     there is a focused item.
    /// </summary>
    /// <param name="e">The routed event args.</param>
    protected override void OnGotFocus(RoutedEventArgs e)
    {
        base.OnGotFocus(e);

        if (this.rootGrid is null || this.ViewModel is null)
        {
            return;
        }

        // If focus is already on a descendant, let the element keep it.
        if (e.OriginalSource is DependencyObject source && this.IsDescendantOfRoot(source) && !ReferenceEquals(source, this))
        {
            return;
        }

        var selected = this.ViewModel.SelectedItem as TreeItemAdapter;
        if (selected is not null)
        {
            var index = this.ViewModel.ShownItems.IndexOf(selected);
            if (index >= 0)
            {
                this.LogFocusSelected(selected, index);

                // Request that the ViewModel assign focus (Keyboard origin) and let the View respond
                this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;
                _ = this.ViewModel.FocusItem(selected, forceRaise: true);
                return;
            }

            // Selected item not currently shown; fall through to ensure another item gets focus.
        }

        if (this.ViewModel.EnsureFocus() && this.ViewModel.FocusedItem is not null)
        {
            this.LogFocusFallback(this.ViewModel.FocusedItem);

            // Ensure keyboard-origin focus is applied via the ViewModel
            this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;
            _ = this.ViewModel.FocusItem(this.ViewModel.FocusedItem, forceRaise: true);
            return;
        }

        // No items to focus; keep focus on the tree control itself.
        _ = this.Focus(FocusState.Programmatic);
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
        // Subscribe to view model change notifications and attach to any existing ViewModel
        this.ViewModelChanged += this.OnViewModelChanged;
        this.OnViewModelChanged(this, new ViewModelChangedEventArgs<DynamicTreeViewModel>(oldValue: null));
    }

    private void OnUnloaded(object? sender, RoutedEventArgs args)
    {
        this.itemsRepeater?.RemoveHandler(KeyDownEvent, new KeyEventHandler(this.ItemsRepeater_OnKeyDown));

        // Remove subscriptions to the ViewModel
        this.ViewModel?.PropertyChanged -= this.ViewModel_OnPropertyChanged;

        // Stop listening to the ViewModelChanged event while unloaded
        this.ViewModelChanged -= this.OnViewModelChanged;

        this.ClearDragState();

        // Clear any pending focus operations to avoid executing them when unloaded
        this.focusOperationPending = false;
        this.isApplyingFocus = false;
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (this.rootGrid is null || this.itemsRepeater is null || this.ViewModel is null)
        {
            return;
        }

        // Log clicks inside/outside the items repeater for debugging
        this.LogPointerPressed(this.rootGrid, args);

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
            this.ViewModel.SelectNoneCommand.Execute(parameter: null);
            _ = this.ViewModel.FocusItem(item: null);
            _ = this.Focus(FocusState.Programmatic);
        }
    }

    private void ItemsRepeater_OnKeyDown(object sender, KeyRoutedEventArgs args)
    {
        this.OnKeyDown(sender, args);

        // Always swallow ItemsRepeater's default handling for paging keys to avoid layout nudges.
        if (!args.Handled)
        {
            switch (args.Key)
            {
                case VirtualKey.PageUp:
                case VirtualKey.PageDown:
                case VirtualKey.Home:
                case VirtualKey.End:
                    args.Handled = true;
                    break;
            }
        }
    }

    [SuppressMessage("Style", "IDE0010:Add missing cases", Justification = "we only handle some keys")]
    private async void OnKeyDown(object sender, KeyRoutedEventArgs args)
    {
        _ = sender; // unused

        if (this.ViewModel is null)
        {
            return;
        }

        this.LogKeyDown(args.Key);
        var handled = await this.HandleKeyDownAsync(args.Key, IsControlKeyDown(), IsShiftKeyDown()).ConfigureAwait(true);
        if (handled)
        {
            args.Handled = true;
        }
    }

    private async Task RunTryFocusCurrentAsync(FocusState focusState)
    {
        this.LogRunTryFocus(this.ViewModel?.FocusedItem, this.focusOperationPending, focusState);
        try
        {
            _ = await this.TryFocusCurrentAsync(focusState).ConfigureAwait(true);
        }
        finally
        {
            this.focusOperationPending = false;
            this.lastFocusRequestOrigin = FocusRequestOrigin.None;
        }
    }

    private async Task<bool> HandleCopyShortcutAsync()
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        var selectedItems = this.GetSelectedItems();
        if (selectedItems.Count == 0)
        {
            return false;
        }

        await this.ViewModel.CopyItemsAsync(selectedItems).ConfigureAwait(true);
        return true;
    }

    private async Task<bool> HandleCutShortcutAsync()
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        var selectedItems = this.GetSelectedItems()
            .Where(item => !item.IsLocked)
            .ToList();

        if (selectedItems.Count == 0)
        {
            return false;
        }

        await this.ViewModel.CutItemsAsync(selectedItems).ConfigureAwait(true);
        return true;
    }

    private async Task<bool> HandlePasteShortcutAsync()
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        if (this.ViewModel.CurrentClipboardState == ClipboardState.Empty || !this.ViewModel.IsClipboardValid)
        {
            return false;
        }

        // If there's no focused item, allow paste to the first selected item if there is a selection.
        var targetParent = this.ViewModel.FocusedItem;

        var selectedItems = this.GetSelectedItems();
        if (selectedItems.Count == 0)
        {
            return false;
        }

        targetParent ??= selectedItems.FirstOrDefault();

        if (targetParent is null)
        {
            return false;
        }

        // Record that this focus change should be a keyboard focus visual.
        this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;
        await this.ViewModel.PasteItemsAsync(targetParent).ConfigureAwait(true);
        return true;
    }

    private async Task<bool> HandleCollapseAsync()
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        // Record this as a keyboard-origin focus change so the view uses keyboard focus visuals.
        this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;
        if (!await this.ViewModel.CollapseFocusedItemAsync().ConfigureAwait(true))
        {
            return false;
        }

        // ViewModel ensures FocusedItem is reasserted; view will perform the actual keyboard focus.
        return true;
    }

    private async Task<bool> HandleExpandAsync()
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        // Record this as a keyboard-origin focus change so the view uses keyboard focus visuals.
        this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;
        if (!await this.ViewModel.ExpandFocusedItemAsync().ConfigureAwait(true))
        {
            return false;
        }

        // ViewModel ensures FocusedItem is reasserted; view will perform the actual keyboard focus.
        return true;
    }

    private async Task<bool> HandleHomeAsync(bool isControlDown)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        // This is a keyboard-origin operation; indicate to the view to use keyboard focus visuals.
        this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;

        var moved = isControlDown
            ? this.ViewModel.FocusFirstVisibleItemInTree()
            : this.ViewModel.FocusFirstVisibleItemInParent();

        if (!moved)
        {
            return false;
        }

        // ViewModel changed FocusedItem; view will apply keyboard focus via observer.
        return true;
    }

    private async Task<bool> HandleEndAsync(bool isControlDown)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        // This is a keyboard-origin operation; indicate to the view to use keyboard focus visuals.
        this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;

        var moved = isControlDown
            ? this.ViewModel.FocusLastVisibleItemInTree()
            : this.ViewModel.FocusLastVisibleItemInParent();

        if (!moved)
        {
            return false;
        }

        // ViewModel changed FocusedItem; view will apply keyboard focus via observer.
        return true;
    }

    private Task<bool> TryFocusCurrentAsync(FocusState focusState = FocusState.Keyboard)
        => this.ViewModel?.FocusedItem is { } current
            ? this.TryFocusItemAsync(current, focusState)
            : Task.FromResult(false);

    private async Task<bool> TryFocusItemAsync(ITreeItem item, FocusState focusState = FocusState.Keyboard)
    {
        if (this.ViewModel is null || this.itemsRepeater is null)
        {
            return false;
        }

        _ = this.ViewModel.FocusItem(item);

        await this.FocusRealizedItemAsync(item, focusState).ConfigureAwait(true);
        return true;
    }

    private async Task FocusRealizedItemAsync(ITreeItem? item, FocusState focusState = FocusState.Keyboard)
    {
        if (item is null || this.itemsRepeater is null || this.ViewModel is null)
        {
            return;
        }

        var index = this.ViewModel.ShownItems.IndexOf(item);
        if (index == -1)
        {
            this.LogFocusIndexMissing(item);
            return;
        }

        var element = this.itemsRepeater.TryGetElement(index) ?? this.itemsRepeater.GetOrCreateElement(index);
        if (element is null)
        {
            this.LogFocusElementMissing(item, index);
            return;
        }

        element.StartBringIntoView();

        var focusTarget = (element as FrameworkElement)?.FindName(TreeItemPart) as DependencyObject ?? element;

        if (focusTarget is Control control)
        {
            control.IsTabStop = true;
            control.UseSystemFocusVisuals = focusState == FocusState.Keyboard;
        }

        var previousApplying = this.isApplyingFocus;
        this.isApplyingFocus = true;
        try
        {
            this.LogFocusApplyAttempt(item, index, focusState, previousApplying, this.focusOperationPending);
            var result = await FocusManager.TryFocusAsync(focusTarget, focusState);
            this.LogFocusResult(item, index, focusTarget.GetType().Name, result.Succeeded);

            if (!result.Succeeded && !ReferenceEquals(focusTarget, element))
            {
                var fallbackResult = await FocusManager.TryFocusAsync(element, focusState);
                this.LogFocusResult(item, index, element.GetType().Name, fallbackResult.Succeeded);
            }
        }
        finally
        {
            this.isApplyingFocus = previousApplying;
        }
    }

    private async Task<bool> MoveFocusAsync(FocusNavigationDirection direction)
    {
        if (this.ViewModel is null)
        {
            return false;
        }

        // This is a keyboard-origin operation; ensure keyboard focus visuals are applied.
        this.lastFocusRequestOrigin = FocusRequestOrigin.Keyboard;
        var moved = direction switch
        {
            FocusNavigationDirection.Down => this.ViewModel.FocusNextVisibleItem(),
            FocusNavigationDirection.Up => this.ViewModel.FocusPreviousVisibleItem(),
            _ => false,
        };

        // ViewModel changed FocusedItem via FocusNext/Previous; view will apply dotnet focus in property-changed handler.
        return moved;
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
        element.GotFocus -= this.TreeItem_GotFocus;
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
        element.GotFocus += this.TreeItem_GotFocus;
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
        args.OldValue?.PropertyChanged -= this.ViewModel_OnPropertyChanged;

        // Attach handlers to the new ViewModel
        if (this.ViewModel is not null)
        {
            this.logger = this.ViewModel.LoggerFactory?.CreateLogger<DynamicTree>();

            this.ViewModel.PropertyChanged += this.ViewModel_OnPropertyChanged;
        }
    }

    private void ViewModel_OnPropertyChanged(object? sender, System.ComponentModel.PropertyChangedEventArgs args)
    {
        if (this.ViewModel is null)
        {
            return;
        }

        var prop = args?.PropertyName;
        if (string.IsNullOrEmpty(prop) || string.Equals(prop, nameof(DynamicTreeViewModel.FocusedItem), System.StringComparison.Ordinal))
        {
            var focusState = this.lastFocusRequestOrigin == FocusRequestOrigin.Keyboard ? FocusState.Keyboard : FocusState.Programmatic;

            // Always try to focus current view model selection when FocusedItem changes, using the last requested origin.
            // If the currently focused element in the platform already matches the ViewModel's FocusedItem,
            // there's no need to re-apply focus (this avoids duplicate focus operations when the platform
            // has already moved focus due to user input).
            var currentFocusedElement = FocusManager.GetFocusedElement() as DependencyObject;
            var focusedItem = this.ViewModel.FocusedItem as ITreeItem;
            if (focusedItem is not null && this.itemsRepeater is not null && currentFocusedElement is not null)
            {
                var index = this.ViewModel.ShownItems.IndexOf(focusedItem);
                if (index >= 0)
                {
                    var targetElement = this.itemsRepeater.TryGetElement(index) as DependencyObject;
                    var focusedMatchesTarget = false;
                    if (targetElement is not null)
                    {
                        var parent = currentFocusedElement;
                        while (parent is not null)
                        {
                            if (ReferenceEquals(parent, targetElement))
                            {
                                focusedMatchesTarget = true;
                                break;
                            }

                            parent = VisualTreeHelper.GetParent(parent);
                        }
                    }

                    if (focusedMatchesTarget)
                    {
                        // We already have platform focus on the target; do not enqueue a focus operation.
                        this.LogViewModelPropertyChanged(prop, this.ViewModel.FocusedItem, this.lastFocusRequestOrigin, enqueue: false);
                        this.lastFocusRequestOrigin = FocusRequestOrigin.None;
                        return;
                    }
                }
            }

            // Ensure focus operations execute on the UI thread. Avoid enqueueing duplicate
            // focus operations if one is already pending.
            if (!this.focusOperationPending)
            {
                // Log that we are enqueueing a focus operation.
                this.LogViewModelPropertyChanged(prop, this.ViewModel.FocusedItem, this.lastFocusRequestOrigin, enqueue: true);
                this.focusOperationPending = true;
                _ = this.DispatcherQueue?.TryEnqueue(() => _ = this.RunTryFocusCurrentAsync(focusState));
            }
        }
    }

    private void TreeItem_PointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (sender is not FrameworkElement { DataContext: TreeItemAdapter item } element)
        {
            return;
        }

        Debug.WriteLine($"Tree: TreeItem_PointerPressed - {item.Label}");
        this.LogPointerPressed(element, args);

        var leftPressed = false;
        if (args.Pointer.PointerDeviceType == PointerDeviceType.Mouse)
        {
            var pointerPoint = args.GetCurrentPoint(element);
            leftPressed = pointerPoint.Properties.IsLeftButtonPressed;
        }

        var handled = this.OnItemPointerPressed(item, IsControlKeyDown(), IsShiftKeyDown(), leftPressed);
        args.Handled = handled;
    }

    private void TreeItem_Tapped(object sender, TappedRoutedEventArgs args)
    {
        if (sender is not FrameworkElement { DataContext: TreeItemAdapter item } element)
        {
            return;
        }

        this.LogTapped(element, args);
        var handled = this.OnItemTapped(item, IsControlKeyDown(), IsShiftKeyDown());
        args.Handled = handled;
    }

    private void TreeItem_GotFocus(object sender, RoutedEventArgs args)
    {
        if (sender is not FrameworkElement element || element.DataContext is not TreeItemAdapter item)
        {
            return;
        }

        var handled = this.OnItemGotFocus(item, this.isApplyingFocus);
        _ = handled;
    }

    private bool IsDescendantOfRoot(DependencyObject element)
    {
        var current = element;
        while (current is not null)
        {
            if (ReferenceEquals(current, this.rootGrid))
            {
                return true;
            }

            current = VisualTreeHelper.GetParent(current);
        }

        return false;
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
        this.dragIsCopy = IsControlKeyDown();

        args.Data.RequestedOperation = this.dragIsCopy ? DataPackageOperation.Copy : DataPackageOperation.Move;
        args.Data.SetData("application/vnd.droidnet.dynamictree", this.dragIsCopy ? "copy" : "move");
    }

    private void TreeItem_DragOver(object sender, DragEventArgs args)
    {
        var isCopyIntent = this.IsCopyIntentCurrent();
        var isCopyAllowed = isCopyIntent && this.CanCopyDraggedItems();

        if (isCopyIntent && !isCopyAllowed)
        {
            args.AcceptedOperation = DataPackageOperation.None;
            this.CancelHoverExpand();
            this.ClearDropIndicatorVisual();
            return;
        }

        if (!this.TryResolveDropTarget(sender, args, isCopyIntent, out var target, out var zone))
        {
            args.AcceptedOperation = DataPackageOperation.None;
            this.CancelHoverExpand();
            this.ClearDropIndicatorVisual();
            return;
        }

        var isCopy = isCopyAllowed;
        args.AcceptedOperation = isCopy ? DataPackageOperation.Copy : DataPackageOperation.Move;
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
        var isCopyIntent = this.IsCopyIntentCurrent();
        var isCopyAllowed = isCopyIntent && this.CanCopyDraggedItems();

        if (isCopyIntent && !isCopyAllowed)
        {
            this.ClearDragState();
            args.AcceptedOperation = DataPackageOperation.None;
            return;
        }

        if (!this.TryResolveDropTarget(sender, args, isCopyIntent, out var target, out var zone) || this.draggedItems is null)
        {
            this.ClearDragState();
            args.AcceptedOperation = DataPackageOperation.None;
            return;
        }

        var isCopy = isCopyAllowed;
        args.AcceptedOperation = isCopy ? DataPackageOperation.Copy : DataPackageOperation.Move;
        args.Handled = true;
        this.CancelHoverExpand();
        this.ClearDropIndicatorVisual();

        try
        {
            // Move: request programmatic focus and set focus to the first moved item on the model
            this.lastFocusRequestOrigin = FocusRequestOrigin.Programmatic;
            await this.ProcessDropAsync(target, zone, isCopy).ConfigureAwait(true);
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

    [SuppressMessage("Style", "IDE0046:Convert to conditional expression", Justification = "code readability")]
    [SuppressMessage("Style", "IDE0305:Simplify collection initialization", Justification = "code readability")]
    private List<TreeItemAdapter> GetSelectedItems()
    {
        if (this.ViewModel is null)
        {
            return [];
        }

        return this.ViewModel.ShownItems
            .OfType<TreeItemAdapter>()
            .Where(item => item.IsSelected)
            .ToList();
    }

    private List<TreeItemAdapter> GetDragItems(TreeItemAdapter primary)
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
        bool isCopyIntent,
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

        if (zone != DropZone.Inside && !isCopyIntent)
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

    private bool IsCopyIntentCurrent() => IsControlKeyDown() || this.dragIsCopy;

    private bool CanCopyDraggedItems() => this.draggedItems is { Count: > 0 } && this.draggedItems.TrueForAll(item => item is ICanBeCloned);

    private async Task ProcessDropAsync(TreeItemAdapter target, DropZone zone, bool isCopy)
    {
        ITreeItem? parent = target;
        var insertIndex = target.ChildrenCount;

        if (zone != DropZone.Inside)
        {
            parent = target.Parent;
            if (parent is null)
            {
                return;
            }

            var children = await parent.Children.ConfigureAwait(true);
            insertIndex = children.IndexOf(target);
            if (insertIndex < 0)
            {
                return;
            }

            if (zone == DropZone.After)
            {
                insertIndex++;
            }
        }

        if (isCopy)
        {
            await this.ViewModel!.CopyItemsAsync(this.draggedItems!).ConfigureAwait(true);

            if (this.ViewModel.CurrentClipboardState == ClipboardState.Empty)
            {
                return;
            }

            await this.ViewModel.PasteItemsAsync(parent, insertIndex).ConfigureAwait(true);

            // ViewModel.PasteItemsAsync will set FocusedItem to the new item; property change handler will focus programmatically.
            return;
        }

        await this.ViewModel!.MoveItemsAsync(this.draggedItems!, parent, insertIndex).ConfigureAwait(true);

        // For move, explicitly set the model FocusedItem to the first moved item; property change handler will focus programmatically.
        _ = this.ViewModel!.FocusItem(this.draggedItems!.FirstOrDefault());
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
