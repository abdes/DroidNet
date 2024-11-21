// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
/// Provides data for the <see cref="DynamicTreeViewModel.ItemRemoved" /> event.
/// </summary>
/// <remarks>
/// This class contains information about the tree item that was removed,
/// including its parent and the relative index at which it was located before
/// removal. It extends the <see cref="DynamicTreeEventArgs"/> class to include
/// additional details specific to the item removal event.
/// </remarks>
public class TreeItemRemovedEventArgs : DynamicTreeEventArgs
{
    /// <summary>
    /// Gets the parent tree item from which the item was removed.
    /// </summary>
    public required ITreeItem Parent { get; init; }

    /// <summary>
    /// Gets the relative index of the removed item within its parent's children collection.
    /// </summary>
    public required int RelativeIndex { get; init; }
}
