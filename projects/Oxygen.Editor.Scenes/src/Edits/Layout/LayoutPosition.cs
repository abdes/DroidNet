// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Layout;

/// <summary>A place in the explorer layout: a container and a position among its entries.</summary>
/// <remarks>
/// The container is a folder when <paramref name="Folder"/> is set, else a node's entry when <paramref name="Node"/>
/// is set, else the layout's root. The index is in <c>[0, count]</c>; the count appends.
/// </remarks>
/// <param name="Folder">The containing folder, if any.</param>
/// <param name="Node">The node whose entry contains the place, when no folder is given.</param>
/// <param name="Index">The position among the container's entries.</param>
public sealed record LayoutPosition(FolderId? Folder, NodeId? Node, int Index)
{
    /// <summary>Creates a position in the layout's root.</summary>
    /// <param name="index">The position among the root entries.</param>
    /// <returns>The position.</returns>
    public static LayoutPosition AtRoot(int index) => new(Folder: null, Node: null, index);

    /// <summary>Creates a position in a folder.</summary>
    /// <param name="folder">The folder.</param>
    /// <param name="index">The position among the folder's entries.</param>
    /// <returns>The position.</returns>
    public static LayoutPosition InFolder(FolderId folder, int index) => new(folder, Node: null, index);

    /// <summary>Creates a position under a node's entry.</summary>
    /// <param name="node">The node.</param>
    /// <param name="index">The position among the entries under the node.</param>
    /// <returns>The position.</returns>
    public static LayoutPosition UnderNode(NodeId node, int index) => new(Folder: null, node, index);
}
