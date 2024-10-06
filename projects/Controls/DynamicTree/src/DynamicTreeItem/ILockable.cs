// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

public interface ILockable
{
    /// <summary>
    /// Gets or sets a value indicating whether the tree item is locked.
    /// <remarks>
    /// When <see langword="true" />, the item cannot be deleted or moved within the tree.
    /// </remarks>
    /// </summary>
    bool IsLocked { get; set; }
}
