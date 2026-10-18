// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Layout;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>An explorer layout entry: a node's entry or a folder.</summary>
/// <param name="Node">The node, for a node's entry.</param>
/// <param name="Folder">The folder, for a folder.</param>
public readonly record struct LayoutEntryId(NodeId? Node, FolderId? Folder)
{
    /// <summary>Gets the entry of a node.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The entry's id.</returns>
    public static LayoutEntryId Of(NodeId node) => new(node, Folder: null);

    /// <summary>Gets the entry of a folder.</summary>
    /// <param name="folder">The folder.</param>
    /// <returns>The entry's id.</returns>
    public static LayoutEntryId Of(FolderId folder) => new(Node: null, folder);
}
