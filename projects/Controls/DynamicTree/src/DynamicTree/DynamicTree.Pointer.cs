// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;
using Windows.UI.Core;
using static DroidNet.Controls.DynamicTreeViewModel;

namespace DroidNet.Controls;

/// <summary>
///     Pointer, tap and context-request handling of the <see cref="DynamicTree" /> control.
/// </summary>
public partial class DynamicTree
{
    private bool deferredSelection;
    private ITreeItem? deferredSelectionItem;

    /// <summary>
    /// Overrideable hook for pointer-press handling on an item.
    /// </summary>
    /// <returns><see langword="true"/> if handled.</returns>
    /// <param name="item">The item that was pressed.</param>
    /// <param name="isControlDown">Whether the Control key is down.</param>
    /// <param name="isShiftDown">Whether the Shift key is down.</param>
    /// <param name="leftButtonPressed">Whether the left mouse button is pressed.</param>
    protected internal virtual bool OnItemPointerPressed(TreeItemAdapter item, bool isControlDown, bool isShiftDown, bool leftButtonPressed)
        => this.HandleItemPress(item, isControlDown, isShiftDown, leftButtonPressed);

    /// <summary>Overrideable item press hook that supports every <see cref="ITreeItem"/> implementation.</summary>
    /// <param name="item">The logical item that was pressed.</param>
    /// <param name="isControlDown">Whether the Control key is down.</param>
    /// <param name="isShiftDown">Whether the Shift key is down.</param>
    /// <param name="leftButtonPressed">Whether the left mouse button is pressed.</param>
    /// <returns>True when the tree handled the press.</returns>
    protected internal virtual bool OnTreeItemPointerPressed(ITreeItem item, bool isControlDown, bool isShiftDown, bool leftButtonPressed)
        => item is TreeItemAdapter adapter
            ? this.OnItemPointerPressed(adapter, isControlDown, isShiftDown, leftButtonPressed)
            : this.HandleItemPress(item, isControlDown, isShiftDown, leftButtonPressed);

    /// <summary>
    /// Overrideable hook for tap handling on an item.
    /// </summary>
    /// <returns><see langword="true"/> if handled.</returns>
    /// <param name="item">The item that was tapped.</param>
    /// <param name="isControlDown">Whether the Control key is down.</param>
    /// <param name="isShiftDown">Whether the Shift key is down.</param>
    protected internal virtual bool OnItemTapped(TreeItemAdapter item, bool isControlDown, bool isShiftDown) => true;

    /// <summary>Overrideable tap hook for every <see cref="ITreeItem"/> implementation.</summary>
    /// <param name="item">The logical item that was tapped.</param>
    /// <param name="isControlDown">Whether the Control key is down.</param>
    /// <param name="isShiftDown">Whether the Shift key is down.</param>
    /// <returns>True when the tree handled the tap.</returns>
    protected internal virtual bool OnTreeItemTapped(ITreeItem item, bool isControlDown, bool isShiftDown)
        => item is not TreeItemAdapter adapter || this.OnItemTapped(adapter, isControlDown, isShiftDown);

    private static bool IsControlKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Control)
        .HasFlag(CoreVirtualKeyStates.Down);

    private static bool IsShiftKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Shift)
        .HasFlag(CoreVirtualKeyStates.Down);

    private static bool IsMenuKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Menu)
        .HasFlag(CoreVirtualKeyStates.Down);

    private static DynamicTreeItem? FindTreeItemAncestor(DependencyObject? element)
    {
        for (var current = element; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (current is DynamicTreeItem item)
            {
                return item;
            }
        }

        return null;
    }

    // An event from a control that handles its own input must not select, focus, invoke or start a drag of its row:
    // the row's expander, trailing content and rename editor. A popup's content is not under its row in the visual
    // tree, so text inputs are recognized by themselves.
    private static bool IsInteractiveSource(DependencyObject? source)
        => (FindTreeItemAncestor(source) is { } row && row.IsInteractiveContentElement(source)) || IsTextInputSource(source);

    private static bool IsTextInputSource(DependencyObject? source)
    {
        for (var current = source; current is not null; current = VisualTreeHelper.GetParent(current))
        {
            if (current is TextBox or RichEditBox)
            {
                return true;
            }
        }

        return false;
    }

    private bool HandleItemPress(ITreeItem item, bool isControlDown, bool isShiftDown, bool leftButtonPressed)
    {
        if (!leftButtonPressed || this.ViewModel is null)
        {
            return false;
        }

        _ = this.ViewModel.FocusItem(item, RequestOrigin.PointerInput);

        // Check if we should defer selection update (Multiple selection active, item already selected, no modifiers)
        if (item.IsSelected && this.ViewModel.SelectedItemsCount > 1 && !isControlDown && !isShiftDown)
        {
            this.deferredSelection = true;
            this.deferredSelectionItem = item;
            return true;
        }

        this.deferredSelection = false;
        this.deferredSelectionItem = null;

        this.SelectItem(item, isControlDown, isShiftDown);
        return true;
    }

    // Applies a selection the press deferred so that a drag of the whole selection could start; it is the click's
    // release that collapses the selection to the clicked item.
    private void CompleteDeferredSelection(ITreeItem item)
    {
        if (this.deferredSelection && ReferenceEquals(item, this.deferredSelectionItem))
        {
            this.SelectItem(item, isControlDown: false, isShiftDown: false);
        }

        this.deferredSelection = false;
        this.deferredSelectionItem = null;
    }

    private void OnPointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (this.rootGrid is null || this.itemsRepeater is null || this.ViewModel is null
            || this.IsFilterBarElement(args.OriginalSource as DependencyObject))
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
            // Log clicks inside/outside the items repeater for debugging
            this.LogPointerPressed(this.rootGrid, args);

            // PointerInput pressed outside the items => clear selection
            this.ViewModel.SelectNoneCommand.Execute(parameter: null);

            // We set platform focus to the tree control itself so keyboard navigation and input
            // are routed to the tree.
            _ = this.Focus(FocusState.Programmatic);
        }
    }

    private void TreeItem_ContextRequested(UIElement sender, ContextRequestedEventArgs args)
    {
        if (args.Handled || IsTextInputSource(args.OriginalSource as DependencyObject))
        {
            return;
        }

        if (sender is not FrameworkElement { DataContext: ITreeItem item } element)
        {
            return;
        }

        Windows.Foundation.Point? position = args.TryGetPosition(element, out var pointerPosition) ? pointerPosition : null;
        var request = new TreeItemContextRequestedEventArgs(item, element, position);
        this.ItemContextRequested?.Invoke(this, request);
        args.Handled = request.Handled;
    }

    private void TreeItem_PointerPressed(object sender, PointerRoutedEventArgs args)
    {
        if (sender is not FrameworkElement { DataContext: ITreeItem item } element)
        {
            return;
        }

        this.LogPointerPressed(element, args);
        if (IsInteractiveSource(args.OriginalSource as DependencyObject))
        {
            return;
        }

        // A mouse selects on press, so that a drag can start from the selection. Touch and pen select on tap: a pan
        // or a scroll must not change the selection.
        if (args.Pointer.PointerDeviceType != PointerDeviceType.Mouse)
        {
            return;
        }

        var leftPressed = args.GetCurrentPoint(element).Properties.IsLeftButtonPressed;
        if (leftPressed)
        {
            args.Handled = true;
            _ = this.OnTreeItemPointerPressed(item, IsControlKeyDown(), IsShiftKeyDown(), leftPressed);
        }
    }

    private void TreeItem_PointerReleased(object sender, PointerRoutedEventArgs args)
    {
        // Prevent the event from bubbling up to outer controls (like a scroller)
        // which might steal focus on release.
        // We only swallow the left button release, as that is the one that triggers selection/focus.
        // Right-click release is often used for context menus and should be allowed to bubble.
        var properties = args.GetCurrentPoint(sender as UIElement).Properties;
        if (properties.PointerUpdateKind == PointerUpdateKind.LeftButtonReleased)
        {
            args.Handled = true;

            if (sender is FrameworkElement { DataContext: ITreeItem item })
            {
                this.CompleteDeferredSelection(item);
            }
            else
            {
                this.deferredSelection = false;
                this.deferredSelectionItem = null;
            }
        }
    }

    private void TreeItem_Tapped(object sender, TappedRoutedEventArgs args)
    {
        if (sender is not FrameworkElement { DataContext: ITreeItem item } element)
        {
            return;
        }

        if (IsInteractiveSource(args.OriginalSource as DependencyObject))
        {
            return;
        }

        this.LogTapped(element, args);
        var isControlDown = IsControlKeyDown();
        var isShiftDown = IsShiftKeyDown();

        // The mouse already selected on press; touch and pen select here.
        if (args.PointerDeviceType != PointerDeviceType.Mouse
            && this.OnTreeItemPointerPressed(item, isControlDown, isShiftDown, leftButtonPressed: true))
        {
            this.CompleteDeferredSelection(item);
        }

        args.Handled = this.OnTreeItemTapped(item, isControlDown, isShiftDown);
    }

    private void TreeItem_DoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        if (IsInteractiveSource(args.OriginalSource as DependencyObject))
        {
            return;
        }

        args.Handled = true;
        this.LogDoubleTapped();
        if (sender is FrameworkElement { DataContext: ITreeItem item })
        {
            this.ViewModel?.InvokeItem(item);
        }
    }
}
