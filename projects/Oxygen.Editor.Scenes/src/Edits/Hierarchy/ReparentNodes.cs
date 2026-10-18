// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Changes;
using Oxygen.Editor.Scenes.Edits.Layout;
using Oxygen.Editor.Scenes.Layout;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Edits.Hierarchy;

/// <summary>Moves nodes under a new parent, keeping their local transforms or their world transforms.</summary>
/// <remarks>
/// <para>
/// The nodes go after a sibling when one is given, whose parent is then the destination; otherwise they are appended to
/// the parent's children. Each node's explorer entry moves with it, nested folders and expanded state included, after
/// the sibling's entry or at the end of the destination scope.
/// </para>
/// <para>
/// With <see cref="PreserveWorld"/>, each node's local transform is compensated so it stays where it is in the world.
/// A listed node under another listed node moves with it. A locked node in a moved subtree refuses the edit.
/// </para>
/// </remarks>
public sealed record ReparentNodes : ISceneEdit
{
    /// <summary>Initializes a new instance of the <see cref="ReparentNodes"/> class.</summary>
    /// <param name="nodes">The nodes to move, in order.</param>
    /// <param name="parent">The new parent, or <see langword="null"/> for the root; ignored when <paramref name="after"/> is given.</param>
    /// <param name="preserveWorld">Whether the nodes keep their world transforms rather than their local ones.</param>
    /// <param name="after">The sibling the nodes go after, if any.</param>
    public ReparentNodes(IReadOnlyList<NodeId> nodes, NodeId? parent, bool preserveWorld, NodeId? after = null)
    {
        ArgumentNullException.ThrowIfNull(nodes);
        this.Nodes = [.. nodes.Distinct()];
        this.Parent = parent;
        this.PreserveWorld = preserveWorld;
        this.After = after;
    }

    /// <summary>Gets the nodes to move, in order.</summary>
    public IReadOnlyList<NodeId> Nodes { get; }

    /// <summary>Gets the new parent, or <see langword="null"/> for the root.</summary>
    public NodeId? Parent { get; }

    /// <summary>Gets a value indicating whether the nodes keep their world transforms.</summary>
    public bool PreserveWorld { get; }

    /// <summary>Gets the sibling the nodes go after, if any.</summary>
    public NodeId? After { get; }

    /// <inheritdoc/>
    public string Label => this.Nodes.Count == 1 ? "Move NodeValue" : $"Move {this.Nodes.Count} nodes";

    /// <inheritdoc/>
    public IReadOnlyCollection<NodeId> Targets
        => [.. this.Nodes.Concat(new[] { this.Parent, this.After }.OfType<NodeId>()).Distinct()];

    /// <inheritdoc/>
    SceneEditValidation ISceneEdit.Validate(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var error = this.Check(scope);
        return error is null ? scope.CheckSubtrees(this.Nodes) : SceneEditValidation.Invalid(error);
    }

    /// <inheritdoc/>
    SceneEditApplication ISceneEdit.Apply(SceneEditScope scope)
    {
        ArgumentNullException.ThrowIfNull(scope);
        var scene = scope.Scene;
        var layout = scope.Layout;
        var nodes = SceneGraph.TopLevel(scope, this.Nodes);
        var parent = this.DestinationOf(scope);
        var states = nodes.Select(node => SceneGraph.PlacementOf(scene, layout, node)).ToArray();
        var locals = nodes.ToDictionary(static node => node.Id, node => this.LocalAfterMove(node, parent));

        SceneGraph.MoveInto(scene, nodes, parent, this.After is { } after ? scope.FindNode(after) : null);
        foreach (var node in nodes)
        {
            SceneGraph.SetLocal(node, locals[node.Id]);
        }

        var entries = nodes.Select(node => SceneLayout.Find(layout, LayoutEntryId.Of(node.Id))).ToArray();
        foreach (var entry in entries.OfType<(ExplorerEntryData Entry, IList<ExplorerEntryData> List)>())
        {
            _ = entry.List.Remove(entry.Entry);
        }

        var (container, at) = this.LayoutDestination(layout, parent);
        for (var i = 0; i < nodes.Count; i++)
        {
            container.Insert(at++, entries[i]?.Entry ?? SceneLayout.EntryFor(nodes[i]));
        }

        NodeId[] moved = [.. nodes.Select(static node => node.Id)];

        SceneLayout.Prune(layout);
        return new SceneEditApplication.Applied(new PlaceNodes(states), [new SceneChange.NodesPlaced(moved), new SceneChange.LayoutChanged()]);
    }

    private SceneDiagnostic? Check(SceneEditScope scope)
    {
        if (this.Nodes.Count == 0)
        {
            return SceneDiagnostic.Error(SceneEditCodes.NothingToEdit, "No node to move.");
        }

        if (this.After is { } after && this.Nodes.Contains(after))
        {
            return SceneDiagnostic.Error(SceneEditCodes.HierarchyCycle, "Nodes cannot be moved after one of themselves.", after);
        }

        var parent = this.DestinationOf(scope);
        foreach (var node in SceneGraph.TopLevel(scope, this.Nodes))
        {
            if (parent is not null && (ReferenceEquals(parent, node) || parent.Ancestors().Contains(node)))
            {
                return SceneDiagnostic.Error(SceneEditCodes.HierarchyCycle, "A node cannot be moved into its own descendant.", node.Id);
            }

            if (this.PreserveWorld && !node.IgnoreParentTransform && !SceneTransformMath.TryPreserveWorldLocal(node, parent, out _, out _, out _))
            {
                return SceneDiagnostic.Error(SceneEditCodes.TransformUnrepresentable, "The destination would produce an unrepresentable transform (singular or sheared).", node.Id);
            }
        }

        return null;
    }

    // Inserting after a sibling implies the sibling's parent.
    private SceneNode? DestinationOf(SceneEditScope scope)
        => this.After is { } after ? scope.FindNode(after)?.Parent : this.Parent is { } parent ? scope.FindNode(parent) : null;

    private TransformData LocalAfterMove(SceneNode node, SceneNode? parent)
        => this.PreserveWorld && !node.IgnoreParentTransform && SceneTransformMath.TryPreserveWorldLocal(node, parent, out var position, out var rotation, out var scale)
            ? SceneGraph.LocalOf(node) with { Position = position, Rotation = rotation, Scale = scale }
            : SceneGraph.LocalOf(node);

    private (IList<ExplorerEntryData> Container, int Index) LayoutDestination(IList<ExplorerEntryData> layout, SceneNode? parent)
    {
        if (this.After is { } after && SceneLayout.Find(layout, LayoutEntryId.Of(after)) is { } anchor)
        {
            return (anchor.List, anchor.List.IndexOf(anchor.Entry) + 1);
        }

        var container = parent is null ? layout : SceneLayout.RequireContainer(layout, LayoutPosition.UnderNode(parent.Id, 0));
        return (container, container.Count);
    }
}
