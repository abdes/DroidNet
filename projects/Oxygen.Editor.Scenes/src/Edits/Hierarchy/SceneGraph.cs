// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.ObjectModel;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Reads and changes where nodes are in the scene graph.</summary>
internal static class SceneGraph
{
    /// <summary>Gets the list of a node's siblings, its own included.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="parent">The parent, or <see langword="null"/> for the root nodes.</param>
    /// <returns>The siblings.</returns>
    public static ObservableCollection<SceneNode> ChildrenOf(Scene scene, SceneNode? parent) => parent?.Children ?? scene.RootNodes;

    /// <summary>Gets a node's place in the graph.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node.</param>
    /// <returns>Its place.</returns>
    public static NodePosition PositionOf(Scene scene, SceneNode node) => new(node.Parent?.Id, ChildrenOf(scene, node.Parent).IndexOf(node));

    /// <summary>Gets a node's full placement: graph place, local transform and layout place.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node.</param>
    /// <returns>The placement.</returns>
    public static NodePlacementState PlacementOf(Scene scene, SceneNode node)
        => new(
            node.Id,
            PositionOf(scene, node),
            LocalOf(node),
            SceneLayout.PositionOf(SceneLayout.Of(scene), LayoutEntryId.Of(node.Id)) ?? LayoutPosition.AtRoot(0));

    /// <summary>Gets a node's local transform.</summary>
    /// <param name="node">The node.</param>
    /// <returns>The local transform.</returns>
    public static TransformData LocalOf(SceneNode node)
        => node.Components.OfType<TransformComponent>().FirstOrDefault() is { } transform
            ? new TransformData { Name = transform.Name, Position = transform.LocalPosition, Rotation = transform.LocalRotation, Scale = transform.LocalScale }
            : new TransformData { Name = "Transform" };

    /// <summary>Writes a node's local transform.</summary>
    /// <param name="node">The node.</param>
    /// <param name="local">The local transform.</param>
    public static void SetLocal(SceneNode node, TransformData local)
    {
        if (node.Components.OfType<TransformComponent>().FirstOrDefault() is { } transform)
        {
            transform.LocalPosition = local.Position;
            transform.LocalRotation = local.Rotation;
            transform.LocalScale = local.Scale;
        }
    }

    /// <summary>Takes a node out of the graph, with its subtree.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node.</param>
    public static void Detach(Scene scene, SceneNode node)
    {
        if (node.Parent is null)
        {
            _ = scene.RootNodes.Remove(node);
        }
        else
        {
            node.SetParent(newParent: null);
        }
    }

    /// <summary>Moves nodes under a parent, in order, after a sibling or at the end.</summary>
    /// <remarks>
    /// Each node joins its new place before it leaves the old one, so observers of the graph never see it removed.
    /// </remarks>
    /// <param name="scene">The scene.</param>
    /// <param name="nodes">The nodes, in the order they take.</param>
    /// <param name="parent">The new parent, or <see langword="null"/> for the root.</param>
    /// <param name="after">The sibling they go after, or <see langword="null"/> for the end.</param>
    public static void MoveInto(Scene scene, IReadOnlyList<SceneNode> nodes, SceneNode? parent, SceneNode? after)
    {
        var destination = ChildrenOf(scene, parent);
        foreach (var node in nodes.Where(node => node.Parent != parent))
        {
            if (parent is null)
            {
                destination.Add(node);
                node.SetParent(newParent: null);
            }
            else
            {
                var wasRoot = node.Parent is null;
                node.SetParent(parent);
                if (wasRoot)
                {
                    _ = scene.RootNodes.Remove(node);
                }
            }
        }

        var order = destination.Where(node => !nodes.Contains(node)).ToList();
        order.InsertRange(after is null ? order.Count : order.IndexOf(after) + 1, nodes);
        for (var i = 0; i < order.Count; i++)
        {
            var from = destination.IndexOf(order[i]);
            if (from != i)
            {
                destination.Move(from, i);
            }
        }
    }

    /// <summary>Puts a detached node at a place in the graph.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="node">The node, out of the graph.</param>
    /// <param name="parent">The parent, or <see langword="null"/> for a root node.</param>
    /// <param name="index">The position among the parent's children.</param>
    public static void Attach(Scene scene, SceneNode node, SceneNode? parent, int index)
    {
        if (parent is null)
        {
            scene.RootNodes.Insert(index, node);
            return;
        }

        node.SetParent(parent);
        parent.Children.Move(parent.Children.Count - 1, index);
    }

    /// <summary>Takes nodes out of the graph, then puts each at its place, so all places refer to one graph.</summary>
    /// <param name="scene">The scene.</param>
    /// <param name="placements">The nodes and where each goes.</param>
    public static void Place(Scene scene, IReadOnlyList<(SceneNode Node, NodePosition At)> placements)
    {
        foreach (var (node, _) in placements)
        {
            Detach(scene, node);
        }

        foreach (var (node, at) in placements.OrderBy(static placement => placement.At.Index))
        {
            Attach(scene, node, at.Parent is { } parent ? FindIn(scene, parent) : null, at.Index);
        }
    }

    /// <summary>Checks a graph place: its parent exists and its index is in range.</summary>
    /// <param name="scope">The edit's scope.</param>
    /// <param name="at">The place.</param>
    /// <param name="leaving">How many of the parent's children leave before the place is taken.</param>
    /// <returns>Why the place is wrong, or <see langword="null"/> when it is right.</returns>
    public static SceneDiagnostic? Check(SceneEditScope scope, NodePosition at, int leaving = 0)
    {
        SceneNode? parent = null;
        if (at.Parent is { } parentId && (parent = scope.FindNode(parentId)) is null)
        {
            return SceneDiagnostic.Error(SceneEditCodes.TargetMissing, "The destination parent no longer exists.", at.Parent);
        }

        var count = ChildrenOf(scope.Scene, parent).Count - leaving;
        return at.Index < 0 || at.Index > count ? SceneDiagnostic.Error(SceneEditCodes.PositionOutOfRange, "The position is outside the parent's children.") : null;
    }

    /// <summary>Keeps only the nodes none of whose ancestors are listed.</summary>
    /// <param name="scope">The edit's scope.</param>
    /// <param name="nodes">The nodes.</param>
    /// <returns>The top-level nodes that exist, in order.</returns>
    public static List<SceneNode> TopLevel(SceneEditScope scope, IEnumerable<NodeId> nodes)
    {
        var ids = nodes.ToHashSet();
        return [.. ids.Select(scope.FindNode).OfType<SceneNode>().Where(node => !node.Ancestors().Any(ancestor => ids.Contains(ancestor.Id)))];
    }

    /// <summary>Gets every node and component id of a subtree's data.</summary>
    /// <param name="node">The subtree's data.</param>
    /// <returns>The node ids and the component ids.</returns>
    public static (IEnumerable<NodeId> Nodes, IEnumerable<ComponentId> Components) IdsOf(SceneNodeData node)
        => (new[] { node.Id }.Concat((node.Children ?? []).SelectMany(child => IdsOf(child).Nodes)),
            node.Components.Select(static component => component.Id).Concat((node.Children ?? []).SelectMany(child => IdsOf(child).Components)));

    private static SceneNode? FindIn(Scene scene, NodeId id) => scene.AllNodes.FirstOrDefault(node => node.Id == id);
}
