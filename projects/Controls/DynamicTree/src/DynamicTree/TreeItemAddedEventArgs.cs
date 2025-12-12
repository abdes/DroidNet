// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="DynamicTreeViewModel.ItemAdded" /> event.
/// </summary>
/// <remarks>
///     This class contains information about the tree item that was added,
///     including the relative index at which it was inserted within its parent's
///     children collection. It extends the <see cref="DynamicTreeEventArgs" /> class
///     to include additional details specific to the item addition event.
///     <para>
///     <see cref="RelativeIndex"/> is a <em>child index</em> in <see cref="Parent"/>'s children collection
///     (<see cref="ITreeItem.Children"/>). It is not an index into <see cref="DynamicTreeViewModel.ShownItems"/>, which
///     depends on expansion/collapse state.</para>
///     <para>
///     For undo/redo, record <see cref="Parent"/> and <see cref="RelativeIndex"/>; do not attempt to restore the
///     operation using visual indices.</para>
/// </remarks>
public class TreeItemAddedEventArgs : DynamicTreeEventArgs
{
    /// <summary>
    ///     Gets the parent tree item under which the new item is being added.
    /// </summary>
    public required ITreeItem Parent { get; init; }

    /// <summary>
    ///     Gets the relative index of the added item within its parent's children collection.
    /// </summary>
    /// <remarks>
    ///     This is the insertion index that was actually used. The view model may clamp the requested index,
    ///     and handlers of <see cref="DynamicTreeViewModel.ItemBeingAdded"/> may veto the operation.
    /// </remarks>
    public required int RelativeIndex { get; init; }
}
