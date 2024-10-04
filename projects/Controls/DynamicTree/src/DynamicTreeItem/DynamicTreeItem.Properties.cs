// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.Diagnostics.CodeAnalysis;
using Microsoft.UI.Xaml;

/// <summary>
/// Properties for the <see cref="DynamicTreeItem" /> control.
/// </summary>
[SuppressMessage(
    "ReSharper",
    "ClassWithVirtualMembersNeverInherited.Global",
    Justification = "class is designed to be extended when needed")]
public partial class DynamicTreeItem
{
    public static readonly DependencyProperty ItemAdapterProperty = DependencyProperty.Register(
        nameof(ItemAdapter),
        typeof(ITreeItem),
        typeof(DynamicTreeItem),
        new PropertyMetadata(
            default(TreeItemAdapter),
            (d, e) => ((DynamicTreeItem)d).OnItemAdapterChanged((ITreeItem)e.OldValue, (ITreeItem)e.NewValue)));

    public ITreeItem? ItemAdapter
    {
        get => (TreeItemAdapter)this.GetValue(ItemAdapterProperty);
        set => this.SetValue(ItemAdapterProperty, value);
    }

    protected virtual void OnItemAdapterChanged(ITreeItem? oldItem, ITreeItem? newItem)
    {
        // Unregsiter event handlers from the old item adapter if any
        if (oldItem is not null)
        {
            oldItem.ChildrenCollectionChanged -= this.TreeItem_ChildrenCollectionChanged;
        }

        // Update visual state based on the current value of IsSelected in the
        // new TreeItem and handle future changes to property values in the new
        // TreeItem
        if (newItem is not null)
        {
            newItem.ChildrenCollectionChanged += this.TreeItem_ChildrenCollectionChanged;
        }
    }
}
