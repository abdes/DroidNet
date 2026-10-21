// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using DroidNet.Controls;

namespace Oxygen.Editor.ContentExplorer.Panes.ContentFolders;

/// <summary>Identifies the project-tree item whose inline name editor should open.</summary>
/// <param name="item">The item being renamed.</param>
public sealed class ProjectItemRenameRequestedEventArgs(ITreeItem item) : EventArgs
{
    /// <summary>Gets the item being renamed.</summary>
    public ITreeItem Item { get; } = item;
}
