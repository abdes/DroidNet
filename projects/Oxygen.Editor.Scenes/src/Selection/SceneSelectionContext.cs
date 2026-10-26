// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Selection;

/// <summary>
/// Classifies the row kinds represented by the current Explorer selection.
/// </summary>
public enum SceneSelectionKind
{
    /// <summary>No rows are selected.</summary>
    Empty,

    /// <summary>The scene root row is selected.</summary>
    Scene,

    /// <summary>Only scene nodes are selected.</summary>
    Node,

    /// <summary>Only explorer folders are selected.</summary>
    Folder,

    /// <summary>A mix of node, folder and/or root rows is selected.</summary>
    Mixed,
}

/// <summary>
/// Immutable, document-scoped snapshot of the Explorer selection, carrying the row
/// kind and explicit primary identity in addition to the selected node/folder ids.
/// </summary>
/// <param name="Kind">The classified selection kind.</param>
/// <param name="SelectedNodes">
/// Selected scene nodes in stable order: nodes of the scene and nodes an instance brought in, each by its reference.
/// </param>
/// <param name="SelectedFolderIds">Selected folder identities in stable order.</param>
/// <param name="PrimaryNode">The primary node, if a node is the primary.</param>
/// <param name="PrimaryFolderId">The primary folder identity, if a folder is the primary.</param>
public sealed record SceneSelectionContext(
    SceneSelectionKind Kind,
    IReadOnlyList<NodeReference> SelectedNodes,
    IReadOnlyList<FolderId> SelectedFolderIds,
    NodeReference? PrimaryNode,
    FolderId? PrimaryFolderId)
{
    /// <summary>Gets the selected nodes captured independently of the caller's collection.</summary>
    public IReadOnlyList<NodeReference> SelectedNodes { get; } = Array.AsReadOnly(SelectedNodes.ToArray());

    /// <summary>Gets the selected folder identities captured independently of the caller's collection.</summary>
    public IReadOnlyList<FolderId> SelectedFolderIds { get; } = Array.AsReadOnly(SelectedFolderIds.ToArray());

    /// <summary>Gets an empty selection context.</summary>
    public static SceneSelectionContext Empty { get; } = new(SceneSelectionKind.Empty, [], [], PrimaryNode: null, PrimaryFolderId: null);

    /// <summary>
    /// Gets the selected nodes of the scene itself, in selection order. A node an instance brought in has no scene
    /// node of its own and is left out; its instance root is not selected by it.
    /// </summary>
    public IReadOnlyList<NodeId> OwnedNodeIds => [.. this.SelectedNodes.Where(static node => node.IsOwned).Select(static node => node.Node)];

    /// <summary>Gets the primary node when it is a node of the scene itself.</summary>
    public NodeId? OwnedPrimaryNodeId => this.PrimaryNode is { IsOwned: true } primary ? primary.Node : null;

    /// <summary>
    /// Creates a node selection, the last node primary; no nodes is the empty selection.
    /// </summary>
    /// <param name="nodes">The selected nodes in stable order; repeats keep their first place.</param>
    /// <returns>The selection context.</returns>
    public static SceneSelectionContext ForNodes(IEnumerable<NodeReference> nodes)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        NodeReference[] ordered = [.. nodes.Distinct()];
        return ordered.Length == 0
            ? Empty
            : new(SceneSelectionKind.Node, ordered, [], ordered[^1], PrimaryFolderId: null);
    }

    /// <summary>
    /// Classifies a selection from the presence of each row kind, so every
    /// producer labels a node/folder/root/mixed batch the same way.
    /// </summary>
    /// <param name="hasScene">Whether the scene root row is part of the selection.</param>
    /// <param name="hasFolder">Whether one or more explorer folders are selected.</param>
    /// <param name="hasNode">Whether one or more scene nodes are selected.</param>
    /// <returns>The selection kind for the combination.</returns>
    public static SceneSelectionKind Classify(bool hasScene, bool hasFolder, bool hasNode) =>
        (hasScene, hasFolder, hasNode) switch
        {
            (true, false, false) => SceneSelectionKind.Scene,
            (false, false, true) => SceneSelectionKind.Node,
            (false, true, false) => SceneSelectionKind.Folder,
            (false, false, false) => SceneSelectionKind.Empty,
            _ => SceneSelectionKind.Mixed,
        };
}
