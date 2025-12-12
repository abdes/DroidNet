// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="DynamicTreeViewModel.ItemBeingMoved" /> event.
/// </summary>
/// <remarks>
///     Handlers can veto the move by setting <see cref="Proceed" /> to <see langword="false" /> or
///     adjust the proposed destination using <see cref="NewParent" /> and <see cref="NewIndex" />.
///     <para>
///     <see cref="NewIndex"/> is an insertion index in the coordinate system of <see cref="NewParent"/>'s current
///     children list (<see cref="ITreeItem.Children"/>) at the time the move request is evaluated.
///     It is <em>not</em> an index into the view model's <see cref="DynamicTreeViewModel.ShownItems"/>.</para>
///     <para>
///     For moves within the same parent, callers and handlers should treat <see cref="NewIndex"/> as an insertion
///     point in the list while the moved item is still present. Computing a destination index after temporarily
///     removing the item can produce off-by-one errors.</para>
/// </remarks>
public class TreeItemBeingMovedEventArgs : DynamicTreeEventArgs
{
    /// <summary>
    ///     Gets the parent the item currently belongs to.
    /// </summary>
    public required ITreeItem PreviousParent { get; init; }

    /// <summary>
    ///     Gets or sets the proposed new parent for the item.
    /// </summary>
    public required ITreeItem NewParent { get; set; }

    /// <summary>
    ///     Gets or sets the proposed index within the new parent children collection.
    /// </summary>
    /// <remarks>
    ///     The index is expressed in <see cref="NewParent"/>'s children collection, not in <see cref="DynamicTreeViewModel.ShownItems"/>.
    /// </remarks>
    public required int NewIndex { get; set; }

    /// <summary>
    ///     Gets or sets a value indicating whether the move should proceed.
    /// </summary>
    public bool Proceed { get; set; } = true;

    /// <summary>
    ///     Gets or sets an optional reason when <see cref="Proceed" /> is <see langword="false" />.
    /// </summary>
    public string? VetoReason { get; set; }
}
