// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World.Serialization;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>One node subtree to insert: its data, its place in the graph and its place in the explorer layout.</summary>
public sealed record NodeInsert
{
    /// <summary>Initializes a new instance of the <see cref="NodeInsert"/> class.</summary>
    /// <param name="node">The subtree's data, with its node and component ids; the insert keeps its own copy.</param>
    /// <param name="at">Its place in the scene graph.</param>
    /// <param name="layout">Its entry's place in the explorer layout, in the scope of its scene parent.</param>
    /// <param name="layoutEntry">
    /// Its entry and the folders and entries under it, as a removal captured them; <see langword="null"/> builds a plain
    /// entry for the subtree.
    /// </param>
    public NodeInsert(SceneNodeData node, NodePosition at, LayoutPosition layout, ExplorerEntryData? layoutEntry = null)
    {
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(at);
        ArgumentNullException.ThrowIfNull(layout);
        this.Node = SerializationCopies.Copy(node);
        this.At = at;
        this.Layout = layout;
        this.LayoutEntry = layoutEntry is null ? null : SerializationCopies.Copy(layoutEntry);
    }

    /// <summary>Gets the subtree's data.</summary>
    public SceneNodeData Node { get; }

    /// <summary>Gets its place in the scene graph.</summary>
    public NodePosition At { get; }

    /// <summary>Gets its entry's place in the explorer layout.</summary>
    public LayoutPosition Layout { get; }

    /// <summary>Gets its layout entry subtree, or <see langword="null"/> for a plain entry.</summary>
    public ExplorerEntryData? LayoutEntry { get; }
}
