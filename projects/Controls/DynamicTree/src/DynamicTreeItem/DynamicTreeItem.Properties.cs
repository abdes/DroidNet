// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

using System.ComponentModel;
using System.Diagnostics;
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
    /// <summary>
    /// The backing <see cref="DependencyProperty" /> for the <see cref="ItemAdapter" /> property.
    /// </summary>
    public static readonly DependencyProperty ItemAdapterProperty = DependencyProperty.Register(
        nameof(ItemAdapter),
        typeof(ITreeItem),
        typeof(DynamicTreeItem),
        new PropertyMetadata(
            default(TreeItemAdapter),
            (d, e) => ((DynamicTreeItem)d).OnItemAdapterChanged((ITreeItem)e.OldValue, (ITreeItem)e.NewValue)));

    /// <summary>
    /// Gets or sets the adapter that provides data for the tree item.
    /// </summary>
    /// <value>
    /// An object that implements the <see cref="ITreeItem" /> interface, which provides data and behavior for the tree item.
    /// </value>
    /// <remarks>
    /// The <see cref="ItemAdapter" /> property is used to bind the tree item to a data source. When the value of this property
    /// changes, the <see cref="OnItemAdapterChanged" /> method is called to handle any necessary updates.
    /// </remarks>
    public ITreeItem? ItemAdapter
    {
        get => (TreeItemAdapter)this.GetValue(ItemAdapterProperty);
        set => this.SetValue(ItemAdapterProperty, value);
    }

    /// <summary>
    /// Handles changes to the <see cref="ItemAdapter" /> property.
    /// </summary>
    /// <param name="oldItem">The previous value of the <see cref="ItemAdapter" /> property.</param>
    /// <param name="newItem">The new value of the <see cref="ItemAdapter" /> property.</param>
    /// <remarks>
    /// This method is called whenever the <see cref="ItemAdapter" /> property changes. It unregisters event handlers
    /// from the old item adapter and registers event handlers with the new item adapter.
    /// </remarks>
    protected virtual void OnItemAdapterChanged(ITreeItem? oldItem, ITreeItem? newItem)
    {
        // Unregsiter event handlers from the old item adapter if any
        if (oldItem is not null)
        {
            oldItem.ChildrenCollectionChanged -= this.TreeItem_ChildrenCollectionChanged;
            ((INotifyPropertyChanged)oldItem).PropertyChanged -= this.OnItemAdapterPropertyChanged;
        }

        // Update visual state based on the current value of IsSelected in the
        // new TreeItem and handle future changes to property values in the new
        // TreeItem
        if (newItem is not null)
        {
            this.UpdateSelectionVisualState(newItem.IsSelected);
            newItem.ChildrenCollectionChanged += this.TreeItem_ChildrenCollectionChanged;
            ((INotifyPropertyChanged)newItem).PropertyChanged += this.OnItemAdapterPropertyChanged;
        }
    }

    protected virtual void OnItemAdapterPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (sender is not null && string.Equals(
                args.PropertyName,
                nameof(ITreeItem.IsSelected),
                StringComparison.Ordinal))
        {
            var adapter = (TreeItemAdapter)sender;
            Debug.WriteLine(
                $"ItemAdapter_OnPropertyChanged: Label = {adapter.Label}, IsSelected = {adapter.IsSelected}");
            this.UpdateSelectionVisualState(adapter.IsSelected);
        }
    }
}
