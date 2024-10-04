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
[ViewModel(typeof(DynamicTreeViewModel))]
public partial class DynamicTree
{
    public static readonly DependencyProperty SelectionModeProperty = DependencyProperty.Register(
        nameof(SelectionMode),
        typeof(SelectionMode),
        typeof(DynamicTree),
        new PropertyMetadata(SelectionMode.None));

    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTree),
        new PropertyMetadata(default(DataTemplateSelector)));

    public DynamicTree()
    {
        this.InitializeComponent();
        this.DefaultStyleKey = typeof(DynamicTree);

        this.ViewModelChanged += (sender, args) =>
        {
            _ = sender; // unused
            _ = args; // unused

            if (this.ViewModel is not null && this.IsLoaded)
            {
                this.ViewModel.SelectionMode = this.SelectionMode;
            }
        };

        this.Loaded += (sender, args) =>
        {
            _ = sender; // unused
            _ = args; // unused

            this.ViewModel!.SelectionMode = this.SelectionMode;
        };
    }

    public SelectionMode SelectionMode
    {
        get => (SelectionMode)this.GetValue(SelectionModeProperty);
        set => this.SetValue(SelectionModeProperty, value);
    }

    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }

    private static bool IsControlKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Control)
        .HasFlag(CoreVirtualKeyStates.Down);

    private static bool IsShiftKeyDown() => InputKeyboardSource
        .GetKeyStateForCurrentThread(VirtualKey.Shift)
        .HasFlag(CoreVirtualKeyStates.Down);

    private void ItemPointerClicked(object sender, PointerRoutedEventArgs args)
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

        var coreWindow = CoreWindow.GetForCurrentThread();

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

    private void ItemDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        args.Handled = true;

        Debug.WriteLine($"Item double tapped: {sender}");
    }

    private void OnExpandTreeItem(object? sender, DynamicTreeEventArgs args)
        => this.ViewModel!.ExpandItemCommand.Execute(args.TreeItem);

    private void OnCollapseTreeItem(object? sender, DynamicTreeEventArgs args)
        => this.ViewModel!.CollapseItemCommand.Execute(args.TreeItem);
}
