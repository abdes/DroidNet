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
