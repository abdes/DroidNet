// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="DynamicTreeViewModel.ItemBeingAdded" /> event.
/// </summary>
/// <remarks>
///     This class contains information about the tree item that is being added,
///     including its parent and a flag indicating whether the addition should
///     proceed. It extends the <see cref="DynamicTreeEventArgs" /> class to include
///     additional details specific to the item addition event.
/// </remarks>
public class TreeItemBeingAddedEventArgs : DynamicTreeEventArgs
{
    /// <summary>
    ///     Gets or sets a value indicating whether the addition of the item should proceed.
    /// </summary>
    /// <value>
    ///     <see langword="true" /> if the item addition should proceed; otherwise, <see langword="false" />.
    /// </value>
    /// <remarks>
    ///     Set this property to <see langword="false" /> to abort the addition of the item to the dynamic tree.
    /// </remarks>
    public bool Proceed { get; set; } = true;

    /// <summary>
    ///     Gets the parent tree item under which the new item is being added.
    /// </summary>
    public required ITreeItem Parent { get; init; }
}
