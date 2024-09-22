// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;

public class DynamicTreeItem : ContentControl
{
    /// <summary>
    /// Default indent increment value.
    /// </summary>
    private const double DefaultIndentIncrement = 34.0;

    public DynamicTreeItem()
    {
        this.DefaultStyleKey = typeof(DynamicTreeItem);

        // Attach an event handler to ensure template is reapplied when DataContext changes
        this.DataContextChanged += this.OnDataContextChanged;
    }

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

    private void OnDataContextChanged(FrameworkElement sender, DataContextChangedEventArgs args)
    {
        // Unregsiter from property changes of the old TreeItem if any
        if (this.DataContext is TreeItemAdapter currentTreeItem)
        {
            currentTreeItem.PropertyChanged -= this.OnTreeItemPropertyChanged;
        }

        // Update visual state based on the current value of IsSelected in the
        // new TreeItem and handle future changes to property values in the new
        // TreeItem
        if (args.NewValue is TreeItemAdapter newTreeItem)
        {
            this.UpdateVisualState(newTreeItem.IsSelected);
            newTreeItem.PropertyChanged += this.OnTreeItemPropertyChanged;
        }

        // Reapply the template to reflect changes in the DataContext
        _ = this.ApplyTemplate();
    }

    private void OnTreeItemPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is TreeItemAdapter treeItem && args.PropertyName?.Equals(
                nameof(ITreeItem.IsSelected),
                StringComparison.Ordinal) == true)
        {
            this.UpdateVisualState(treeItem.IsSelected);
        }
    }

    private void UpdateVisualState(bool isSelected)
        => VisualStateManager.GoToState(this, isSelected ? "Selected" : "Unselected", useTransitions: true);
}
