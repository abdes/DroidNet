// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="DynamicTreeViewModel.ItemMoved" /> event.
/// </summary>
/// <remarks>
///     For batch moves, <see cref="Moves" /> contains one entry per moved root item. A single event
///     instance is raised after all items are relocated.
///     <para>
///     Indices reported by this event refer to positions within the underlying parent children
///     collections (<see cref="ITreeItem.Children"/>), not to indices within the view model's
///     <see cref="DynamicTreeViewModel.ShownItems"/>.</para>
///     <para>
///     For undo/redo, prefer the values in <see cref="Moves"/> over attempting to recompute indices
///     from visual state.</para>
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
}

/// <summary>
///     Represents the details of a single moved item.
/// </summary>
/// <remarks>
///     <para>
///     The <paramref name="PreviousIndex"/> value is the item's index within <paramref name="PreviousParent"/>'s children
///     collection immediately before the move.</para>
///     <para>
///     The <paramref name="NewIndex"/> value is the item's final index within <paramref name="NewParent"/>'s children
///     collection after the move completes.</para>
///     <para>
///     Both indices are child indices (see <see cref="ITreeItem.Children"/>), not indices into
///     <see cref="DynamicTreeViewModel.ShownItems"/>.</para>
/// </remarks>
[System.Diagnostics.CodeAnalysis.SuppressMessage("StyleCop.CSharp.MaintainabilityRules", "SA1402:File may only contain a single type", Justification = "keep together with event args type")]
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
