// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Provides data for the <see cref="DynamicTreeViewModel.ItemBeingAdded" /> event.
/// </summary>
public class TreeItemBeingAddedEventArgs : DynamicTreeEventArgs
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
