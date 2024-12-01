// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics;
using System.Diagnostics.CodeAnalysis;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Windows.System;
using Windows.UI.Core;

namespace DroidNet.Controls;

/// <summary>
/// A control to display a tree as a list of expandable items.
/// </summary>
[TemplatePart(Name = TreeItemPart, Type = typeof(DynamicTreeItem))]
[TemplatePart(Name = ItemsRepeaterPart, Type = typeof(ItemsRepeater))]
[TemplatePart(Name = RootGridPart, Type = typeof(Grid))]
[ViewModel(typeof(DynamicTreeViewModel))]
public partial class DynamicTree : Control
{
    private const string RootGridPart = "PartRootGrid";
    private const string ItemsRepeaterPart = "PartItemsRepeater";
    private const string TreeItemPart = "PartTreeItem";

    private ItemsRepeater? itemsRepeater;
    private Grid? rootGrid;

    /// <summary>
    /// Initializes a new instance of the <see cref="DynamicTree"/> class.
    /// </summary>
    public DynamicTree()
    {
        this.DefaultStyleKey = typeof(DynamicTree);

        this.Loaded += (_, _) =>
        {
            // Update the ViewModel SelectionMode property on load and whenever the ViewModel changes.
            this.ViewModel!.SelectionMode = this.SelectionMode;

            // Handle key bindings
            this.KeyDown += this.OnKeyDown;
        };

        this.ViewModelChanged += (_, _) =>
        {
            if (this.ViewModel is not null && this.IsLoaded)
            {
                this.ViewModel.SelectionMode = this.SelectionMode;
            }
        };
    }

    /// <inheritdoc/>
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
            throw new InvalidOperationException("PartItemsRepeater not found in the control template.");
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
    [SuppressMessage(
        "ReSharper",
        "SwitchStatementMissingSomeEnumCasesNoDefault",
        Justification = "we only handle some keys")]
    private void OnKeyDown(object sender, KeyRoutedEventArgs args)
    {
        switch (args.Key)
        {
            case VirtualKey.A when IsControlKeyDown():
                this.ViewModel!.ToggleSelectAll();

                return;

            case VirtualKey.I when IsControlKeyDown() && IsShiftKeyDown():
                this.ViewModel!.InvertSelectionCommand.Execute(default);
                return;
        }
    }

    private void ItemsRepeater_OnElementClearing(ItemsRepeater sender, ItemsRepeaterElementClearingEventArgs args)
    {
        if (args.Element is not FrameworkElement element)
        {
            return;
        }

        element.PointerPressed -= this.TreeItem_PointerPressed;

        if (element.FindName(TreeItemPart) is not DynamicTreeItem treeItem)
        {
            return;
        }

        treeItem.Expand -= this.OnExpandTreeItem;
        treeItem.Collapse -= this.OnCollapseTreeItem;
        treeItem.DoubleTapped -= this.TreeItem_DoubleTapped;
    }

    private void ItemsRepeater_OnElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
    {
        if (args.Element is not Control element)
        {
            return;
        }

        element.PointerPressed += this.TreeItem_PointerPressed;

        // If we have a TreeItemPart with a DynamicTreeItem, then setup event handlers on it for
        // expand, collapse, and double-tap
        if (element.FindName(TreeItemPart) is not DynamicTreeItem treeItem)
        {
            return;
        }

        treeItem.Collapse += this.OnCollapseTreeItem;
        treeItem.Expand += this.OnExpandTreeItem;
        treeItem.DoubleTapped += this.TreeItem_DoubleTapped;

        treeItem.UpdateItemMargin();
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
        else
        {
            // Handle regular Click
            this.ViewModel!.ClearAndSelectItem(item);
        }

        // Give focus to the clicked element
        _ = element.Focus(FocusState.Programmatic);
    }

    private void TreeItem_DoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        args.Handled = true;

        // TODO: decide what to do when tree item is double tapped
        Debug.WriteLine($"Item double tapped: {sender}");
    }

    private async void OnExpandTreeItem(object? sender, DynamicTreeEventArgs args)
        => await this.ViewModel!.ExpandItemAsync(args.TreeItem).ConfigureAwait(true);

    private async void OnCollapseTreeItem(object? sender, DynamicTreeEventArgs args)
        => await this.ViewModel!.CollapseItemAsync(args.TreeItem).ConfigureAwait(true);
}
