// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Duplicates nodes beside their sources, as the viewport's Ctrl+D and Alt-drag do.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc/>
    public async Task<SceneValueCommandResult<IReadOnlyList<SceneNode>>> DuplicateNodesInPlaceAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<NodeId> nodeIds,
        IReadOnlyDictionary<NodeId, TransformData>? transforms = null)
    {
        ArgumentNullException.ThrowIfNull(context);
        ArgumentNullException.ThrowIfNull(nodeIds);
        using var authoring = EnterAuthoring(context);
        if (authoring is null)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "AUTHORING_SUSPENDED",
                "The scene is reloading or was replaced, so the duplication did not run.",
                context);
        }

        // Resolve every source first, so a stale id duplicates nothing.
        if (ResolveInPlaceSources(context.Scene, nodeIds) is not { } sources)
        {
            return this.DuplicateStaleTarget("One or more selected nodes no longer exist.", context);
        }

        if (sources.Count == 0)
        {
            return this.DuplicateStaleTarget("There is nothing to duplicate.", context);
        }

        // Map every copy before creating any, so references between the duplicated nodes follow the copies.
        var copies = new Dictionary<NodeId, NodeId>();
        var copiesData = sources.Select(source => RemapNodeIds(WithTransform(source, transforms), copies)).ToList();
        var created = new List<SceneNode>(sources.Count);
        context.History.BeginChangeSet($"Duplicate {sources.Count} node(s)");
        try
        {
            for (var index = 0; index < sources.Count; index++)
            {
                // Each copy lands right after its source: same scene parent, same Explorer folder.
                var source = sources[index];
                var folderId = FindNodeFolderId(context.Scene.ExplorerLayout, source.Id).folderId;
                var duplicated = await this.InsertCopiesAsync(context, [copiesData[index]], copies, newParentNodeId: null, folderId, source.Id).ConfigureAwait(true);
                if (!duplicated.Succeeded || duplicated.Value is not { } inserted)
                {
                    return duplicated;
                }

                created.AddRange(inserted);
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }

        return SceneCommandResults.Success<IReadOnlyList<SceneNode>>(created);
    }

    /// <summary>Resolves the nodes to duplicate; a node whose ancestor is also listed is copied with that ancestor.</summary>
    /// <param name="scene">The scene that holds the nodes.</param>
    /// <param name="nodeIds">The requested node identities.</param>
    /// <returns>The nodes to copy, or <see langword="null"/> when any identity is stale.</returns>
    private static List<SceneNode>? ResolveInPlaceSources(Scene scene, IReadOnlyList<NodeId> nodeIds)
    {
        var requested = nodeIds.ToHashSet();
        var sources = new List<SceneNode>(nodeIds.Count);
        foreach (var nodeId in nodeIds.Distinct())
        {
            if (FindNode(scene, nodeId) is not { } node)
            {
                return null;
            }

            if (!node.Ancestors().Any(ancestor => requested.Contains(ancestor.Id)))
            {
                sources.Add(node);
            }
        }

        return sources;
    }

    /// <summary>Captures a node for duplication, with the copy's requested pose, if any.</summary>
    /// <param name="source">The node to duplicate.</param>
    /// <param name="transforms">The requested poses of the copies, by source node identity.</param>
    /// <returns>The node's data.</returns>
    private static SceneNodeData WithTransform(SceneNode source, IReadOnlyDictionary<NodeId, TransformData>? transforms)
    {
        var data = source.Dehydrate();
        return transforms?.TryGetValue(source.Id, out var transform) == true
            ? data with
            {
                Components = [.. data.Components.Select(component => component is TransformData current
                    ? current with { Position = transform.Position, Rotation = transform.Rotation, Scale = transform.Scale }
                    : component)],
            }
            : data;
    }

    /// <summary>Finds the Explorer folder that directly groups a node.</summary>
    /// <returns>Whether the node has a layout entry, and its folder, if any.</returns>
    private static (bool found, FolderId? folderId) FindNodeFolderId(IList<ExplorerEntryData>? entries, NodeId nodeId, FolderId? enclosingFolderId = null)
    {
        if (entries is null)
        {
            return (false, null);
        }

        foreach (var entry in entries)
        {
            var isFolder = LayoutTypeComparer.Equals(entry.Type, "Folder");
            if (!isFolder && entry.NodeId == nodeId)
            {
                return (true, enclosingFolderId);
            }

            if (entry.Children is { } children)
            {
                // Folders group the entries inside them; a node's own children start a new scope.
                var result = FindNodeFolderId(children, nodeId, isFolder ? entry.FolderId : null);
                if (result.found)
                {
                    return result;
                }
            }
        }

        return (false, null);
    }
}
