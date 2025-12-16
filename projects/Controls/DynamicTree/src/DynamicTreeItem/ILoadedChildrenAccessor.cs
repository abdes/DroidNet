// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace DroidNet.Controls;

/// <summary>
///     Exposes access to an item's already-loaded children without triggering any load.
/// </summary>
/// <remarks>
///     This interface must never cause lazy child loading. It is used by filtering to compute
///     "loaded-only" subtree matches without touching <see cref="ITreeItem.Children" />.
/// </remarks>
public interface ILoadedChildrenAccessor
{
    /// <summary>
    ///     Gets a value indicating whether the children collection has been loaded.
    /// </summary>
    public bool AreChildrenLoaded { get; }

    /// <summary>
    ///     Attempts to get the already-loaded children.
    /// </summary>
    /// <param name="children">The loaded children when available.</param>
    /// <returns><see langword="true"/> when children are loaded; otherwise <see langword="false"/>.</returns>
    public bool TryGetLoadedChildren(out IReadOnlyList<ITreeItem> children);
}
