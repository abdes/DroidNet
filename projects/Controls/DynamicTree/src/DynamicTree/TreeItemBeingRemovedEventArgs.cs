// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides data for the <see cref="DynamicTreeViewModel.ItemBeingRemoved" /> event.
/// </summary>
/// <remarks>
///     This class contains information about the tree item that is being removed, including a flag indicating
///     whether the removal should proceed. It extends the <see cref="DynamicTreeEventArgs" /> class to include additional
///     details specific to the item removal event.
/// </remarks>
public class TreeItemBeingRemovedEventArgs : DynamicTreeEventArgs
{
    /// <summary>
    ///     Gets or sets a value indicating whether the removal of the item should proceed.
    /// </summary>
    /// <value>
    ///     <see langword="true" /> if the item removal should proceed; otherwise, <see langword="false" />.
    /// </value>
    /// <remarks>
    ///     Set this property to <see langword="false" /> to abort the removal of the item from the dynamic tree.
    /// </remarks>
    public bool Proceed { get; set; } = true;
}
