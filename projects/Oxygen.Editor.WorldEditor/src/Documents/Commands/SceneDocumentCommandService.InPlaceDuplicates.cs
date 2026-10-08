// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Editor.World.Documents.Commands;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.WorldEditor.Documents.Commands;

/// <summary>Duplicates nodes beside their sources, as the viewport's Ctrl+D and Alt-drag do.</summary>
public sealed partial class SceneDocumentCommandService
{
    /// <inheritdoc/>
    public async Task<SceneValueCommandResult<IReadOnlyList<SceneNode>>> DuplicateNodesInPlaceAsync(
        SceneDocumentCommandContext context,
        IReadOnlyList<Guid> nodeIds,
        IReadOnlyDictionary<Guid, TransformData>? transforms = null)
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

        // Resolve every source first, so a stale id duplicates nothing. A node whose ancestor is
        // also listed is copied with that ancestor.
        var requested = nodeIds.ToHashSet();
        var sources = new List<SceneNode>(nodeIds.Count);
        foreach (var nodeId in nodeIds.Distinct())
        {
            if (FindNode(context.Scene, nodeId) is not { } node)
            {
                return this.DuplicateFailure(
                    DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                    "One or more selected nodes no longer exist.",
                    context);
            }

            if (!node.Ancestors().Any(ancestor => requested.Contains(ancestor.Id)))
            {
                sources.Add(node);
            }
        }

        if (sources.Count == 0)
        {
            return this.DuplicateFailure(
                DiagnosticCodes.ScenePrefix + "STALE_TARGET",
                "There is nothing to duplicate.",
                context);
        }

        var created = new List<SceneNode>(sources.Count);
        context.History.BeginChangeSet($"Duplicate {sources.Count} node(s)");
        try
        {
            foreach (var source in sources)
            {
                var data = source.Dehydrate();
                if (transforms?.TryGetValue(source.Id, out var transform) == true)
                {
                    data = data with
                    {
                        Components = [.. data.Components.Select(component => component is TransformData current
                            ? current with { Position = transform.Position, Rotation = transform.Rotation, Scale = transform.Scale }
                            : component)],
                    };
                }

                // Each copy lands right after its source: same scene parent, same Explorer folder.
                var folderId = FindNodeFolderId(context.Scene.ExplorerLayout, source.Id).FolderId;
                var duplicated = await this.DuplicateNodesFromDataAsync(context, [data], newParentNodeId: null, folderId, source.Id).ConfigureAwait(true);
                if (!duplicated.Succeeded || duplicated.Value is not { } copies)
                {
                    return duplicated;
                }

                created.AddRange(copies);
            }
        }
        finally
        {
            context.History.EndChangeSet();
        }

        return SceneCommandResults.Success<IReadOnlyList<SceneNode>>(created);
    }

    /// <summary>Finds the Explorer folder that directly groups a node.</summary>
    /// <returns>Whether the node has a layout entry, and its folder, if any.</returns>
    private static (bool Found, Guid? FolderId) FindNodeFolderId(IList<ExplorerEntryData>? entries, Guid nodeId, Guid? enclosingFolderId = null)
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
                if (result.Found)
                {
                    return result;
                }
            }
        }

        return (false, null);
    }
}
