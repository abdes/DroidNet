// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides an interface for tree items that can be locked to prevent deletion or movement within the tree.
/// </summary>
/// <remarks>
///     Implementing this interface allows a tree item to be marked as locked, ensuring it cannot be deleted or moved.
///     This is useful in scenarios where certain items need to be protected from user modifications.
/// </remarks>
public interface ICanBeLocked
{
    /// <summary>
    ///     Gets or sets a value indicating whether the tree item is locked.
    ///     <remarks>
    ///         When <see langword="true" />, the item cannot be deleted or moved within the tree.
    ///     </remarks>
    /// </summary>
    // ReSharper disable once UnusedMember.Global
    public bool IsLocked { get; set; }
}
