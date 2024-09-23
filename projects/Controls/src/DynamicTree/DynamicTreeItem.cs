// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Collections.Specialized;
using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class DynamicTreeItem : ContentControl, INotifyPropertyChanged
{
    /// <summary>
    /// Default indent increment value.
    /// </summary>
    private const double DefaultIndentIncrement = 34.0;

    public DynamicTreeItem()
    {
        this.DefaultStyleKey = typeof(DynamicTreeItem);

        this.Loaded += this.OnLoaded;

        // Attach an event handler to ensure template is reapplied when DataContext changes
        this.DataContextChanged += this.OnDataContextChanged;
    }

    public event EventHandler<DynamicTreeEventArgs>? Expand;

    public event EventHandler<DynamicTreeEventArgs>? Collapse;

    public event PropertyChangedEventHandler? PropertyChanged;

    public bool HasChildren { get; set; }

    public void OnExpand(object? sender, EventArgs args) => this.Expand?.Invoke(
        this,
        new DynamicTreeEventArgs()
        {
            TreeItem = (TreeItemAdapter)this.DataContext,
        });

    public void OnCollapse(object? sender, EventArgs args) => this.Collapse?.Invoke(
        this,
        new DynamicTreeEventArgs()
        {
            TreeItem = (TreeItemAdapter)this.DataContext,
        });

    protected override void OnApplyTemplate()
    {
        base.OnApplyTemplate();

        // Optionally, handle template parts (if defined in the style) here
        if (this.DataContext is ITreeItem treeItem)
        {
            // Try to get the indent increment from the XAML resources, fallback to default if not found
            var indentIncrement = DefaultIndentIncrement;
            if (Application.Current.Resources.TryGetValue(
                    "DynamicTreeItemIndentIncrement",
                    out var indentIncrementObj) && indentIncrementObj is double increment)
            {
                indentIncrement = increment;
            }

            // Calculate the extra left margin based on the IndentLevel
            var extraLeftMargin = treeItem.Depth * indentIncrement;

            // Add the extra margin to the existing left margin
            this.Margin = new Thickness(
                this.Margin.Left + extraLeftMargin,
                this.Margin.Top,
                this.Margin.Right,
                this.Margin.Bottom);
        }
    }

    private async void OnLoaded(object sender, RoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        // Initialize the HasChildren property for the first time when the control is Loaded.
        if (this.DataContext is TreeItemAdapter treeItem)
        {
            var childrenCount = await treeItem.GetChildrenCountAsync().ConfigureAwait(false);
            this.HasChildren = childrenCount > 0;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.HasChildren)));
        }
    }

    private async void OnDataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args)
    {
        // Unregsiter event handlers from the old TreeItemAdapter if any
        if (this.DataContext is TreeItemAdapter currentTreeItem)
        {
            var itemChildren = (INotifyCollectionChanged)await currentTreeItem.Children.ConfigureAwait(true);
            itemChildren.CollectionChanged -= this.ItemChildrenOnCollectionChanged;
        }

        // Update visual state based on the current value of IsSelected in the
        // new TreeItem and handle future changes to property values in the new
        // TreeItem
        if (args.NewValue is TreeItemAdapter newTreeItem)
        {
            var itemChildren = (INotifyCollectionChanged)await newTreeItem.Children.ConfigureAwait(true);
            itemChildren.CollectionChanged += this.ItemChildrenOnCollectionChanged;
        }

        // Reapply the template to reflect changes in the DataContext
        _ = this.ApplyTemplate();
    }

    private async void ItemChildrenOnCollectionChanged(object? sender, NotifyCollectionChangedEventArgs args)
    {
        if (this.DataContext is TreeItemAdapter treeItem)
        {
            this.HasChildren = (await treeItem.Children.ConfigureAwait(false)).Count > 0;
            this.PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(nameof(this.HasChildren)));
        }
    }
}

public class DynamicTreeEventArgs : EventArgs
{
    public required TreeItemAdapter TreeItem { get; init; }
}
