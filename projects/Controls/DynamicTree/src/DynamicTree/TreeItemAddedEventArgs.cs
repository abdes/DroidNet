// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Provides data for the <see cref="DynamicTreeViewModel.ItemAdded" /> event.
/// </summary>
/// <remarks>
/// This class contains information about the tree item that was added,
/// including the relative index at which it was inserted within its parent's
/// children collection. It extends the <see cref="DynamicTreeEventArgs"/> class
/// to include additional details specific to the item addition event.
/// </remarks>
public class TreeItemAddedEventArgs : DynamicTreeEventArgs
{
    /// <summary>
    /// Gets the parent tree item under which the new item is being added.
    /// </summary>
    public required ITreeItem Parent { get; init; }

    /// <summary>
    /// Gets the relative index of the added item within its parent's children collection.
    /// </summary>
    public required int RelativeIndex { get; init; }
}
