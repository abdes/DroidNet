// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Diagnostics;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

/// <summary>
/// A control to display a tree as a list of expandable items.
/// </summary>
[ViewModel(typeof(DynamicTreeViewModel))]
public partial class DynamicTree
{
    public static readonly DependencyProperty SelectionModeProperty = DependencyProperty.Register(
        nameof(SelectionMode),
        typeof(DynamicTreeSelectionMode),
        typeof(DynamicTree),
        new PropertyMetadata(DynamicTreeSelectionMode.None));

    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTree),
        new PropertyMetadata(default(DataTemplateSelector)));

    public DynamicTree()
    {
        this.InitializeComponent();
        this.Style = (Style)Application.Current.Resources[nameof(DynamicTree)];

        this.ViewModelChanged += (sender, args) =>
        {
            if (this.ViewModel is not null && this.IsLoaded)
            {
                this.ViewModel.SelectionMode = this.SelectionMode;
            }
        };

        this.Loaded += (sender, args) =>
        {
            _ = sender;
            _ = args;
            this.ViewModel!.SelectionMode = this.SelectionMode;
        };
    }

    public DynamicTreeSelectionMode SelectionMode
    {
        get => (DynamicTreeSelectionMode)this.GetValue(SelectionModeProperty);
        set => this.SetValue(SelectionModeProperty, value);
    }

    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }

    private void ItemTapped(object sender, TappedRoutedEventArgs args)
    {
        args.Handled = true;

        if (sender is FrameworkElement itemContainer)
        {
            Debug.WriteLine($"Item tapped: {itemContainer}");

            // Retrieve the DataContext, which is the TreeItemAdapter object
            if (itemContainer.DataContext is TreeItemAdapter item)
            {
                this.ViewModel!.SelectItem(item);
                _ = VisualStateManager.GoToState(
                    this,
                    item.IsSelected ? "Selected" : "Unselected",
                    useTransitions: true);
            }
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
