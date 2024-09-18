// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.DynamicTree;

using System.Diagnostics;
using DroidNet.Controls.DynamicTree.Styles;
using DroidNet.Mvvm.Generators;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

/// <summary>
/// A control to display a tree as a list of expandable items.
/// </summary>
[ViewModel(typeof(DynamicTreeViewModel))]
public partial class DynamicTreeControl
{
    public static readonly DependencyProperty ThumbnailTemplateSelectorProperty = DependencyProperty.Register(
        nameof(ThumbnailTemplateSelector),
        typeof(DataTemplateSelector),
        typeof(DynamicTreeControl),
        new PropertyMetadata(default(DataTemplateSelector)));

    public DynamicTreeControl()
    {
        this.InitializeComponent();

        this.Style = (Style)Application.Current.Resources[nameof(DynamicTreeControl)];
    }

    public DataTemplateSelector ThumbnailTemplateSelector
    {
        get => (DataTemplateSelector)this.GetValue(ThumbnailTemplateSelectorProperty);
        set => this.SetValue(ThumbnailTemplateSelectorProperty, value);
    }

    private void ExpanderTapped(object sender, TappedRoutedEventArgs args)
    {
        args.Handled = true;

        if (sender is FrameworkElement expander)
        {
            // Retrieve the DataContext, which is the TreeItemAdapter object
            if (expander.DataContext is TreeItemAdapter item)
            {
                this.ViewModel!.ToggleExpandedCommand.Execute(item);
            }
        }
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
                this.ViewModel!.ActiveItem = item;
            }
        }
    }

    private void ItemDoubleTapped(object sender, DoubleTappedRoutedEventArgs args)
    {
        args.Handled = true;

        Debug.WriteLine($"Item double tapped: {sender}");
    }

    private void OnExpanded(object? sender, EventArgs e)
    {
        if (sender is TreeItemExpander expander)
        {
            this.ViewModel!.ExpandItemCommand.Execute(expander.TreeItem);
        }
    }

    private void OnCollapsed(object? sender, EventArgs e)
    {
        if (sender is TreeItemExpander expander)
        {
            this.ViewModel!.CollapseItemCommand.Execute(expander.TreeItem);
        }
    }
}
