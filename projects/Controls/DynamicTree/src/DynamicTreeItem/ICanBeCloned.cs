// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Provides a custom cloning capability for tree items during clipboard paste operations.
///     Implement this interface on any <see cref="ITreeItem" /> that needs to participate in
///     copy/paste; reflection-based cloning is intentionally not supported.
/// </summary>
public interface ICanBeCloned
{
    /// <summary>
    ///     Clones this item with all its properties but without any parent or children relationships.
    /// </summary>
    /// <returns>A cloned copy of this item ready for insertion under a new parent.</returns>
    public ITreeItem Clone();
}
