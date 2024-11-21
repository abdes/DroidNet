// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Represents the ViewModel for a dynamic tree control, providing functionality for managing
/// hierarchical data structures, including selection, expansion, and manipulation of tree items.
/// </summary>
public abstract partial class DynamicTreeViewModel
{
    /// <summary>
    /// Fires before an item is removed from the dynamic tree.
    /// </summary>
    /// <remarks>
    /// It is important to subscribe to this event at least from the derived concrete ViewModel to
    /// do the actions necessary to update the underlying model objects when the item is being
    /// removed from the tree. If the removal needs to be aborted and no changes should be made to
    /// the tree, the <see cref="TreeItemBeingRemovedEventArgs.Proceed" /> property should be set to
    /// <see langword="false" />.
    /// </remarks>
    public event EventHandler<TreeItemBeingRemovedEventArgs>? ItemBeingRemoved;

    /// <summary>
    /// Fires when an item is removed from the dynamic tree.
    /// </summary>
    public event EventHandler<TreeItemRemovedEventArgs>? ItemRemoved;

    /// <summary>
    /// Fires before an item is added from the dynamic tree.
    /// </summary>
    /// <remarks>
    /// It is important to subscribe to this event at least from the derived concrete ViewModel to
    /// do the actions necessary to update the underlying model objects when the item is being added
    /// to the tree. If the addition needs to be aborted and no changes should be made to the tree,
    /// the <see cref="TreeItemBeingAddedEventArgs.Proceed" /> property should be set to
    /// <see langword="false" />.
    /// </remarks>
    public event EventHandler<TreeItemBeingAddedEventArgs>? ItemBeingAdded;

    /// <summary>
    /// Fires when an item is added to the dynamic tree.
    /// </summary>
    public event EventHandler<TreeItemAddedEventArgs>? ItemAdded;
}
