// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Microsoft.Extensions.Logging;
using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Operations;

/// <summary>
/// Default implementation of <see cref="ISceneMutator" /> that enforces scene-graph invariants.
/// </summary>
public sealed class SceneMutator : ISceneMutator
{
    private readonly ILogger<SceneMutator> logger;

    public SceneMutator(ILogger<SceneMutator> logger)
    {
        this.logger = logger ?? throw new ArgumentNullException(nameof(logger));
    }

    public SceneNodeChangeRecord CreateNodeAtRoot(SceneNode newNode, Scene scene)
    {
        ArgumentNullException.ThrowIfNull(newNode);
        ArgumentNullException.ThrowIfNull(scene);

        var wasInRootNodes = scene.RootNodes.Contains(newNode);
        if (!wasInRootNodes)
        {
            scene.RootNodes.Add(newNode);
            this.logger.LogDebug("Added node '{NodeName}' to RootNodes", newNode.Name);
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
            this.logger.LogDebug("Removed node '{NodeName}' from RootNodes after parenting", newNode.Name);
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
            this.logger.LogDebug("Removed node '{NodeName}' from RootNodes", node.Name);
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
            this.logger.LogDebug("Removed hierarchy root '{NodeName}' from RootNodes", root.Name);
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
            this.logger.LogDebug("Moved node '{NodeName}' to RootNodes", node.Name);
        }
        else if (newParent is not null && scene.RootNodes.Contains(node))
        {
            _ = scene.RootNodes.Remove(node);
            removedFromRootNodes = true;
            this.logger.LogDebug("Removed node '{NodeName}' from RootNodes after reparenting", node.Name);
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
