// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Events exposed by the <see cref="DynamicTreeViewModel" /> class.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    /// <summary>
    /// Fires before an item is removed from the dynamic tree.
    /// </summary>
    /// <remarks>
    /// It is important to subscribe to this event at least from the derived concrete ViewModel to do the actions necessary to
    /// update the underlying model objects when the item is being removed from the tree. If the removal needs to be aborted and
    /// no changes should be made to the tree, the <see cref="ItemBeingRemovedEventArgs.Proceed" /> property should be set to <see langword="false" />.
    /// </remarks>
    public event EventHandler<ItemBeingRemovedEventArgs>? ItemBeingRemoved;

    /// <summary>
    /// Fires when an item is removed from the dynamic tree.
    /// </summary>
    public event EventHandler<ItemRemovedEventArgs>? ItemRemoved;

    /// <summary>
    /// Fires before an item is added from the dynamic tree.
    /// </summary>
    /// <remarks>
    /// It is important to subscribe to this event at least from the derived concrete ViewModel to do the actions necessary to
    /// update the underlying model objects when the item is being added to the tree. If the addition needs to be aborted and
    /// no changes should be made to the tree, the <see cref="ItemBeingAddedEventArgs.Proceed" /> property should be set to
    /// <see langword="false" />.
    /// </remarks>
    public event EventHandler<ItemBeingAddedEventArgs>? ItemBeingAdded;

    /// <summary>
    /// Fires when an item is added to the dynamic tree.
    /// </summary>
    public event EventHandler<ItemAddedEventArgs>? ItemAdded;

    /// <summary>
    /// Provides data for the <see cref="DynamicTreeViewModel.ItemBeingRemoved" /> event.
    /// </summary>
    public class ItemBeingRemovedEventArgs : DynamicTreeEventArgs
    {
        /// <summary>
        /// Gets or sets a value indicating whether the removal of the item should proceed.
        /// </summary>
        /// <value>
        /// <see langword="true" /> if the item removal should proceed; otherwise, <see langword="false" />.
        /// </value>
        /// <remarks>
        /// Set this property to <see langword="false" /> to abort the removal of the item from the dynamic tree.
        /// </remarks>
        public bool Proceed { get; set; } = true;
    }

    /// <summary>
    /// Provides data for the <see cref="DynamicTreeViewModel.ItemRemoved" /> event.
    /// </summary>
    public class ItemRemovedEventArgs : DynamicTreeEventArgs
    {
        public required ITreeItem Parent { get; init; }

        public required int RelativeIndex { get; init; }
    }

    /// <summary>
    /// Provides data for the <see cref="DynamicTreeViewModel.ItemBeingAdded" /> event.
    /// </summary>
    public class ItemBeingAddedEventArgs : DynamicTreeEventArgs
    {
        /// <summary>
        /// Gets or sets a value indicating whether the addition of the item should proceed.
        /// </summary>
        /// <value>
        /// <see langword="true" /> if the item addition should proceed; otherwise, <see langword="false" />.
        /// </value>
        /// <remarks>
        /// Set this property to <see langword="false" /> to abort the addition of the item from the dynamic tree.
        /// </remarks>
        public bool Proceed { get; set; } = true;

        public required ITreeItem Parent { get; init; }
    }

    /// <summary>
    /// Provides data for the <see cref="DynamicTreeViewModel.ItemAdded" /> event.
    /// </summary>
    public class ItemAddedEventArgs : DynamicTreeEventArgs
    {
        public required int RelativeIndex { get; init; }
    }
}
