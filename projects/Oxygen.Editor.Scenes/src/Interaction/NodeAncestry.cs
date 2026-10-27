// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.ContentPipeline.Inspection;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Components;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Interaction;

/// <summary>
/// The ancestors of a node, which an instance may have brought in: the one rule that hiding, locking and selecting
/// follow down a hierarchy.
/// </summary>
public static class NodeAncestry
{
    /// <summary>Gets a node's ancestors, nearest first.</summary>
    /// <param name="scene">The scene the node is in.</param>
    /// <param name="node">The node, which an instance may have brought in.</param>
    /// <param name="structureOf">The contents read of a source, or <see langword="null"/> when they are not read.</param>
    /// <returns>
    /// For a node an instance brings in, its parents in its instance's contents up to a root node of the source, across
    /// nesting levels, then the instance root and the root's ancestors in the scene; for a node of the scene, its
    /// ancestors in the scene. <see langword="null"/> for a node an instance brings in that its instance's contents do
    /// not hold yet, whose ancestors are unknown.
    /// </returns>
    public static IReadOnlyList<NodeReference>? Of(Scene scene, NodeReference node, Func<AssetPath, SceneStructure?> structureOf)
    {
        ArgumentNullException.ThrowIfNull(scene);
        ArgumentNullException.ThrowIfNull(node);
        ArgumentNullException.ThrowIfNull(structureOf);
        var root = scene.AllNodes.FirstOrDefault(owned => owned.Id == node.Node);
        var ancestors = new List<NodeReference>();

        // A node that is no instance root brings in nothing: only its own ancestry is known.
        if (!node.IsOwned && root?.Components.OfType<SceneInstanceComponent>().FirstOrDefault() is { } instance)
        {
            if (structureOf(instance.Source) is not { } structure || structure.Find(node.Path) is not { } entry)
            {
                return null;
            }

            for (var path = entry.Parent; path is not null; path = structure.Find(path)?.Parent)
            {
                ancestors.Add(new NodeReference(node.Node, path));
            }
        }

        for (var current = node.IsOwned ? root?.Hierarchy.Parent : root; current is not null; current = current.Hierarchy.Parent)
        {
            ancestors.Add(NodeReference.To(current.Id));
        }

        return ancestors;
    }
}
