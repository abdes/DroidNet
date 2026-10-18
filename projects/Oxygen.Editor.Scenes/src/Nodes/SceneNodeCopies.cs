// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Edits;
using Oxygen.Editor.Scenes.Edits.Hierarchy;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Nodes;

/// <summary>Copies node subtrees as data with new node and component ids, for duplicates and the clipboard.</summary>
/// <remarks>
/// A node reference in a script slot that names a node copied in the same call follows the copy; any other reference is
/// kept. Copied directional lights leave the atmosphere slot to their source. AssetValue references and material slot ids
/// are kept.
/// </remarks>
public static class SceneNodeCopies
{
    /// <summary>Copies node subtrees.</summary>
    /// <param name="scene">The scene the copies are for.</param>
    /// <param name="roots">The subtrees to copy.</param>
    /// <param name="transforms">The local transforms of the copied roots, by source root id; others keep theirs.</param>
    /// <returns>The copies and their ids.</returns>
    public static NodeCopies Copy(Scene scene, IEnumerable<SceneNodeData> roots, IReadOnlyDictionary<NodeId, TransformData>? transforms = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(roots);
        var ids = new Dictionary<NodeId, NodeId>();
        var renewed = roots.Select(root => Renew(WithTransform(SerializationCopies.Copy(root), transforms), ids)).ToList();
        var copies = renewed.Select(data =>
        {
            var node = SceneNode.CreateAndHydrate(scene, data);
            CopiedNodeReferences.RemapSubtree(node, ids);
            return node.Dehydrate();
        }).ToList();
        return new NodeCopies(copies, ids);
    }

    /// <summary>Builds the insert that duplicates nodes beside their sources, as the viewport's duplicate does.</summary>
    /// <remarks>
    /// Each copy goes right after its source, under the same parent and in the same explorer place, with the explorer
    /// folders under the source copied too. A listed node under another listed node is copied with it; missing ids are
    /// skipped.
    /// </remarks>
    /// <param name="scene">The scene.</param>
    /// <param name="layout">The scene's explorer layout.</param>
    /// <param name="nodes">The nodes to duplicate.</param>
    /// <param name="transforms">The local transforms of the copies, by source id; others keep their source's.</param>
    /// <returns>The insert, or <see langword="null"/> when none of the nodes exists.</returns>
    public static InsertNodes? DuplicateInPlace(Scene scene, IList<ExplorerEntryData> layout, IReadOnlyCollection<NodeId> nodes, IReadOnlyDictionary<NodeId, TransformData>? transforms = null)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(layout);
        ArgumentNullException.ThrowIfNull(nodes);
        var requested = nodes.ToHashSet();
        var sources = requested.Select(id => scene.AllNodes.FirstOrDefault(node => node.Id == id)).OfType<SceneNode>()
            .Where(node => !node.Ancestors().Any(ancestor => requested.Contains(ancestor.Id))).ToList();
        if (sources.Count == 0)
        {
            return null;
        }

        var copies = Copy(scene, sources.Select(static source => source.Dehydrate()), transforms);
        var graph = FinalPlaces(sources, source => SceneGraph.PositionOf(scene, source), static at => at.Index, static at => at.Parent, static (at, index) => at with { Index = index });
        var places = FinalPlaces(
            sources,
            source => SceneLayout.PositionOf(layout, LayoutEntryId.Of(source.Id)) ?? throw new InvalidOperationException($"NodeValue {source.Id} has no explorer entry."),
            static at => at.Index,
            static at => (at.Folder, at.Node),
            static (at, index) => at with { Index = index });
        var inserts = sources.Select((source, i) => new NodeInsert(
            copies.Roots[i],
            graph[i],
            places[i],
            SceneLayout.Find(layout, LayoutEntryId.Of(source.Id)) is { } entry ? RemapEntry(entry.Entry, copies.Ids) : null)).ToList();
        return new InsertNodes(inserts, inserts.Count == 1 ? $"Duplicate {inserts[0].Node.Name}" : $"Duplicate {inserts.Count} nodes");
    }

    /// <summary>Copies an explorer entry subtree for copied nodes: node ids follow the copies, folders get new ids.</summary>
    /// <param name="entry">The entry.</param>
    /// <param name="ids">Maps source node ids to their copies' ids.</param>
    /// <returns>The copy.</returns>
    internal static ExplorerEntryData RemapEntry(ExplorerEntryData entry, IReadOnlyDictionary<NodeId, NodeId> ids)
        => entry with
        {
            NodeId = entry.NodeId is { } node ? ids.GetValueOrDefault(node, node) : null,
            FolderId = entry.FolderId.HasValue ? FolderId.New() : null,
            Children = entry.Children?.Select(child => RemapEntry(child, ids)).ToList(),
        };

    // A copy goes right after its source; earlier copies in the same container shift the later ones.
    private static List<TPosition> FinalPlaces<TPosition, TKey>(
        List<SceneNode> sources,
        Func<SceneNode, TPosition> positionOf,
        Func<TPosition, int> indexOf,
        Func<TPosition, TKey> containerOf,
        Func<TPosition, int, TPosition> withIndex)
        where TPosition : notnull
    {
        var positions = sources.Select(positionOf).ToList();
        var indexes = positions.Select(indexOf).ToList();
        var result = new TPosition[positions.Count];
        foreach (var group in Enumerable.Range(0, positions.Count).GroupBy(i => containerOf(positions[i])))
        {
            var rank = 0;
            foreach (var i in group.OrderBy(i => indexes[i]))
            {
                result[i] = withIndex(positions[i], indexes[i] + 1 + rank++);
            }
        }

        return [.. result];
    }

    private static SceneNodeData WithTransform(SceneNodeData data, IReadOnlyDictionary<NodeId, TransformData>? transforms)
        => transforms?.TryGetValue(data.Id, out var transform) == true
            ? data with
            {
                Components = [.. data.Components.Select(component => component is TransformData current
                    ? current with { Position = transform.Position, Rotation = transform.Rotation, Scale = transform.Scale }
                    : component)],
            }
            : data;

    private static SceneNodeData Renew(SceneNodeData data, Dictionary<NodeId, NodeId> ids)
    {
        var id = NodeId.New();
        ids.Add(data.Id, id);
        return data with
        {
            Id = id,
            Components = [.. data.Components.Select(Renew)],
            Children = data.Children?.Select(child => Renew(child, ids)).ToList(),
        };
    }

    private static ComponentData Renew(ComponentData data)
        => data switch
        {
            DirectionalLightData directional => directional with { Id = ComponentId.New(), AtmosphereSlot = AtmosphereLightSlot.None },
            _ => data with { Id = ComponentId.New() },
        };
}
