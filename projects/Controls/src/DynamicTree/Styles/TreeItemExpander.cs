// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls.DynamicTree.Styles;

using System.ComponentModel;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;

public class TreeItemExpander : Control
{
    private TreeItemAdapter? treeItem;

    public TreeItemExpander()
    {
        this.DefaultStyleKey = typeof(TreeItemExpander);
        this.Tapped += this.OnTapped;
    }

    public event EventHandler? Expanded;

    public event EventHandler? Collapsed;

    public TreeItemAdapter? TreeItem
    {
        get => this.treeItem;
        set
        {
            if (this.treeItem == value)
            {
                return;
            }

            if (this.treeItem is not null)
            {
                this.treeItem!.PropertyChanged -= this.OnTreeItemIsExpandedChanged;
            }

            // Only if the value in TreeItem not null
            this.treeItem = value;

            if (this.treeItem is not null)
            {
                this.treeItem!.PropertyChanged += this.OnTreeItemIsExpandedChanged;
            }
        }
    }

    private void OnTreeItemIsExpandedChanged(object? sender, PropertyChangedEventArgs args) => _
        = VisualStateManager.GoToState(
            this,
            this.treeItem!.IsExpanded ? "Expanded" : "Collapsed",
            useTransitions: true);

    private void OnTapped(object sender, TappedRoutedEventArgs args)
    {
        _ = sender; // unused
        _ = args; // unused

        if (this.TreeItem is null)
        {
            return;
        }

        this.TreeItem.IsExpanded = !this.TreeItem.IsExpanded;
        if (this.TreeItem.IsExpanded)
        {
            this.Expanded?.Invoke(this, EventArgs.Empty);
        }
        else
        {
            this.Collapsed?.Invoke(this, EventArgs.Empty);
        }
    }
}
