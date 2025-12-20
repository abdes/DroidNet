// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;

namespace Oxygen.Editor.World.SceneExplorer.Operations;

/// <summary>
/// Default implementation of <see cref="ISceneMutator"/> that enforces
/// scene-graph invariants and emits diagnostics for graph mutations.
/// </summary>
/// <param name="logger">The logger used for diagnostic messages.</param>
/// <exception cref="ArgumentNullException">Thrown when <paramref name="logger"/> is <see langword="null"/>.</exception>
public sealed partial class SceneMutator(ILogger<SceneMutator> logger) : ISceneMutator
{
    private readonly ILogger<SceneMutator> logger = logger ?? throw new ArgumentNullException(nameof(logger));

    /// <summary>
    /// Creates <paramref name="newNode"/> at the root of <paramref name="scene"/>.
    /// </summary>
    /// <param name="newNode">The node to add to the scene.</param>
    /// <param name="scene">The scene to modify.</param>
    /// <returns>A <see cref="SceneNodeChangeRecord"/> describing the change.</returns>
    /// <exception cref="ArgumentNullException">If <paramref name="newNode"/> or <paramref name="scene"/> is <see langword="null"/>.</exception>
    public SceneNodeChangeRecord CreateNodeAtRoot(SceneNode newNode, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(newNode);
        ArgumentNullException.ThrowIfNull(scene);

        var wasInRootNodes = scene.RootNodes.Contains(newNode);
        if (!wasInRootNodes)
        {
            scene.RootNodes.Add(newNode);
            this.LogAddedNodeToRootNodes(newNode);
        }

        var oldParentId = newNode.Parent?.Id;
        newNode.SetParent(newParent: null);

        return new SceneNodeChangeRecord(
            OperationName: "CreateNodeAtRoot",
            AffectedNode: newNode,
            OldParentId: oldParentId,
            NewParentId: null,
            RequiresEngineSync: true,
            AddedToRootNodes: !wasInRootNodes,
            RemovedFromRootNodes: false);
    }

    /// <summary>
    /// Creates <paramref name="newNode"/> as a child of <paramref name="parentNode"/>.
    /// </summary>
    /// <param name="newNode">The node to insert.</param>
    /// <param name="parentNode">The parent under which the node will be placed.</param>
    /// <param name="scene">The scene containing the nodes.</param>
    /// <returns>A <see cref="SceneNodeChangeRecord"/> describing the change.</returns>
    /// <exception cref="ArgumentNullException">If any argument is <see langword="null"/>.</exception>
    public SceneNodeChangeRecord CreateNodeUnderParent(SceneNode newNode, SceneNode parentNode, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(newNode);
        ArgumentNullException.ThrowIfNull(parentNode);
        ArgumentNullException.ThrowIfNull(scene);

        var previousParentId = newNode.Parent?.Id;
        newNode.SetParent(parentNode);

        var wasInRootNodes = scene.RootNodes.Contains(newNode);
        if (wasInRootNodes)
        {
            _ = scene.RootNodes.Remove(newNode);
            this.LogRemovedNodeFromRootNodesAfterParenting(newNode);
        }

        return new SceneNodeChangeRecord(
            OperationName: "CreateNodeUnderParent",
            AffectedNode: newNode,
            OldParentId: previousParentId,
            NewParentId: parentNode.Id,
            RequiresEngineSync: true,
            AddedToRootNodes: false,
            RemovedFromRootNodes: wasInRootNodes);
    }

    /// <summary>
    /// Removes the node with the given <paramref name="nodeId"/> from <paramref name="scene"/>.
    /// </summary>
    /// <param name="nodeId">The id of the node to remove.</param>
    /// <param name="scene">The scene to operate on.</param>
    /// <returns>A <see cref="SceneNodeChangeRecord"/> describing the change.</returns>
    /// <exception cref="InvalidOperationException">If the node cannot be found.</exception>
    public SceneNodeChangeRecord RemoveNode(Guid nodeId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var node = FindNodeById(scene, nodeId) ?? throw new InvalidOperationException(
            $"Cannot remove node '{nodeId}' because it was not found.");

        var oldParentId = node.Parent?.Id;
        var wasInRootNodes = scene.RootNodes.Contains(node);
        if (wasInRootNodes)
        {
            _ = scene.RootNodes.Remove(node);
            this.LogRemovedNodeFromRootNodes(node);
        }

        node.SetParent(newParent: null);

        return new SceneNodeChangeRecord(
            OperationName: "RemoveNode",
            AffectedNode: node,
            OldParentId: oldParentId,
            NewParentId: null,
            RequiresEngineSync: true,
            AddedToRootNodes: false,
            RemovedFromRootNodes: wasInRootNodes);
    }

    /// <summary>
    /// Removes an entire hierarchy (sub-tree) whose root has id <paramref name="rootNodeId"/>.
    /// </summary>
    /// <param name="rootNodeId">The id of the root node whose hierarchy will be removed.</param>
    /// <param name="scene">The scene to operate on.</param>
    /// <returns>A <see cref="SceneNodeChangeRecord"/> describing the change.</returns>
    /// <exception cref="InvalidOperationException">If the root node cannot be found.</exception>
    public SceneNodeChangeRecord RemoveHierarchy(Guid rootNodeId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var root = FindNodeById(scene, rootNodeId) ?? throw new InvalidOperationException(
            $"Cannot remove hierarchy '{rootNodeId}' because it was not found.");

        var oldParentId = root.Parent?.Id;
        var wasInRootNodes = scene.RootNodes.Contains(root);
        if (wasInRootNodes)
        {
            _ = scene.RootNodes.Remove(root);
            this.LogRemovedHierarchyRootFromRootNodes(root);
        }

        // Detach the entire subtree from any parent; children remain linked beneath root
        root.SetParent(newParent: null);

        return new SceneNodeChangeRecord(
            OperationName: "RemoveHierarchy",
            AffectedNode: root,
            OldParentId: oldParentId,
            NewParentId: null,
            RequiresEngineSync: true,
            AddedToRootNodes: false,
            RemovedFromRootNodes: wasInRootNodes);
    }

    /// <summary>
    /// Reparents the node identified by <paramref name="nodeId"/> to <paramref name="newParentId"/>.
    /// </summary>
    /// <param name="nodeId">The id of the node to reparent.</param>
    /// <param name="oldParentId">The expected old parent id (optional).</param>
    /// <param name="newParentId">The new parent id, or <see langword="null"/> to move to root.</param>
    /// <param name="scene">The scene containing the node.</param>
    /// <returns>A <see cref="SceneNodeChangeRecord"/> describing the reparenting.</returns>
    /// <exception cref="InvalidOperationException">If the node or target parent cannot be found.</exception>
    public SceneNodeChangeRecord ReparentNode(Guid nodeId, Guid? oldParentId, Guid? newParentId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var node = FindNodeById(scene, nodeId) ?? throw new InvalidOperationException(
            $"Cannot reparent node '{nodeId}' because it was not found.");

        var previousParentId = node.Parent?.Id;
        var effectiveOldParentId = oldParentId ?? previousParentId;
        SceneNode? newParent = null;

        if (newParentId.HasValue)
        {
            newParent = FindNodeById(scene, newParentId.Value) ?? throw new InvalidOperationException(
                $"Cannot reparent node '{nodeId}' because target parent '{newParentId}' was not found.");
        }

        node.SetParent(newParent);

        var addedToRootNodes = false;
        var removedFromRootNodes = false;

        if (newParent is null && !scene.RootNodes.Contains(node))
        {
            scene.RootNodes.Add(node);
            addedToRootNodes = true;
            this.LogMovedNodeToRootNodes(node);
        }
        else if (newParent is not null && scene.RootNodes.Contains(node))
        {
            _ = scene.RootNodes.Remove(node);
            removedFromRootNodes = true;
            this.LogRemovedNodeFromRootNodesAfterReparenting(node);
        }

        return new SceneNodeChangeRecord(
            OperationName: "ReparentNode",
            AffectedNode: node,
            OldParentId: effectiveOldParentId,
            NewParentId: newParentId,
            RequiresEngineSync: true,
            AddedToRootNodes: addedToRootNodes,
            RemovedFromRootNodes: removedFromRootNodes);
    }

    /// <summary>
    /// Reparents multiple node hierarchies to a new parent.
    /// </summary>
    /// <param name="nodeIds">The ids of the nodes to reparent.</param>
    /// <param name="newParentId">The new parent id to apply to each node hierarchy.</param>
    /// <param name="scene">The scene containing the nodes.</param>
    /// <returns>A list of resulting <see cref="SceneNodeChangeRecord"/> objects.</returns>
    public IReadOnlyList<SceneNodeChangeRecord> ReparentHierarchies(IEnumerable<Guid> nodeIds, Guid? newParentId, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);

        var results = new List<SceneNodeChangeRecord>();
        foreach (var nodeId in nodeIds)
        {
            var change = this.ReparentNode(nodeId, oldParentId: null, newParentId, scene);
            results.Add(change);
        }

        return results;
    }

    private static SceneNode? FindNodeById(Scene scene, Guid nodeId)
    {
        ArgumentNullException.ThrowIfNull(scene);

        foreach (var root in scene.RootNodes)
        {
            var found = FindInTree(root);
            if (found is not null)
            {
                return found;
            }
        }

        return null;

        SceneNode? FindInTree(SceneNode node)
        {
            if (node.Id == nodeId)
            {
                return node;
            }

            foreach (var child in node.Children)
            {
                var found = FindInTree(child);
                if (found is not null)
                {
                    return found;
                }
            }

            return null;
        }
    }
}
