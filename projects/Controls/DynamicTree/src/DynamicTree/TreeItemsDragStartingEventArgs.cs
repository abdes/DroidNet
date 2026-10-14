// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Windows.ApplicationModel.DataTransfer;

namespace DroidNet.Controls;

/// <summary>Identifies the items a drag carries, so the tree's owner can describe them to other drop targets.</summary>
/// <param name="items">The dragged items, in tree order.</param>
/// <param name="data">The drag's data package; the owner may add its own formats.</param>
public sealed class TreeItemsDragStartingEventArgs(IReadOnlyList<ITreeItem> items, DataPackage data) : EventArgs
{
    /// <summary>Gets the dragged items.</summary>
    public IReadOnlyList<ITreeItem> Items { get; } = items;

    /// <summary>Gets the drag's data package.</summary>
    public DataPackage Data { get; } = data;
}
