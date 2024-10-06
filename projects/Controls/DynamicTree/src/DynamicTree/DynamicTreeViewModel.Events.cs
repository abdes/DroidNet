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
    /// Fires when an item before an item is removed from the dynamic tree.
    /// </summary>
    /// <remarks>
    /// It is important to subscribe this event at least from the derived concrete ViewModel to do the actions necessary to update
    /// the underlying model objects when the item is being removed from the tree. If the removal needs to be aborted and no
    /// changes should be made to the tree, the <see cref="ItemBeingRemovedEventArgs.Proceed" /> property should be set to <see langword="false" />.
    /// </remarks>
    public event EventHandler<ItemBeingRemovedEventArgs>? ItemBeingRemoved;

    /// <summary>
    /// Fires when an item is removed from the dynamic tree.
    /// </summary>
    public event EventHandler<ItemRemovedEventArgs>? ItemRemoved;

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
    public class ItemRemovedEventArgs : DynamicTreeEventArgs;
}
