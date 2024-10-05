// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Input;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Windows.System;
using Windows.UI.Core;

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

    public DynamicTree()
    {
        this.DefaultStyleKey = typeof(DynamicTree);

        // Update the ViewModel SelectionMode property on load and whenver the ViewModel changes.
        this.Loaded += (_, _) => this.ViewModel!.SelectionMode = this.SelectionMode;
        this.ViewModelChanged += (_, _) =>
        {
            if (this.ViewModel is not null && this.IsLoaded)
            {
                this.ViewModel.SelectionMode = this.SelectionMode;
            }
        };
    }

    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        if (this.GetTemplateChild(ItemsRepeaterPart) is not ItemsRepeater itemsRepeater)
        {
            throw new InvalidOperationException("PartItemsRepeater not found in the control template.");
        }

        itemsRepeater.ElementPrepared += this.OnElementPrepared;
        itemsRepeater.ElementClearing += this.ItemsRepeater_OnElementClearing;
    }

    private static bool IsControlKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Control)
        .HasFlag(CoreVirtualKeyStates.Down);

    private static bool IsShiftKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Shift)
        .HasFlag(CoreVirtualKeyStates.Down);

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

    private void OnElementPrepared(ItemsRepeater sender, ItemsRepeaterElementPreparedEventArgs args)
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
    }

    private void TreeItem_PointerPressed(object sender, PointerRoutedEventArgs args)
    {
        args.Handled = true;
        if (sender is not FrameworkElement { DataContext: TreeItemAdapter item } element)
        {
            return;
        }

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
            this.ViewModel!.SelectItem(item);
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
    }

    private void TreeItem_DoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        args.Handled = true;

        // TODO: decide what to do when tree item is double tapped
        Debug.WriteLine($"Item double tapped: {sender}");
    }

    private void OnExpandTreeItem(object? sender, DynamicTreeEventArgs args)
        => this.ViewModel!.ExpandItemCommand.Execute(args.TreeItem);

    private void OnCollapseTreeItem(object? sender, DynamicTreeEventArgs args)
        => this.ViewModel!.CollapseItemCommand.Execute(args.TreeItem);
}
