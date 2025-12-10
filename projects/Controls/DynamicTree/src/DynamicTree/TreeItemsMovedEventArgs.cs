// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="DynamicTreeViewModel.ItemMoved" /> event.
/// </summary>
/// <remarks>
///     For batch moves, <see cref="Moves" /> contains one entry per moved root item. A single event instance is
///     raised after all items are relocated.
/// </remarks>
public class TreeItemsMovedEventArgs : EventArgs
{
    /// <summary>
    ///     Gets the collection of move descriptors representing the completed operation.
    /// </summary>
    public required IReadOnlyList<MovedItemInfo> Moves { get; init; }

    /// <summary>
    ///     Gets a value indicating whether the event represents a batch move.
    /// </summary>
    public bool IsBatch => this.Moves.Count > 1;

    /// <summary>
    ///     Gets the primary move information (first item in <see cref="Moves" />).
    /// </summary>
    public MovedItemInfo PrimaryMove => this.Moves[0];

    /// <summary>
    ///     Represents the details of a single moved item.
    /// </summary>
    public sealed record MovedItemInfo(
        ITreeItem Item,
        ITreeItem PreviousParent,
        ITreeItem NewParent,
        int PreviousIndex,
        int NewIndex)
    {
        /// <summary>
        ///     Gets a value indicating whether the move changed the parent.
        /// </summary>
        public bool IsReparenting => !ReferenceEquals(this.PreviousParent, this.NewParent);
    }
}
