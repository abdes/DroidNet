// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.SceneExplorer.Operations;

/// <summary>
/// Executes mutations against the runtime scene graph (scene nodes and <see cref="Scene.RootNodes" />).
/// Implementations are synchronous and side-effect free beyond model mutation; engine sync is handled by callers.
/// </summary>
public interface ISceneMutator
{
    /// <summary>
    /// Creates a new scene node at the scene root, ensuring parent detachment and root membership.
    /// </summary>
    /// <param name="newNode">The node to add.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A change record describing the mutation.</returns>
    public SceneNodeChangeRecord CreateNodeAtRoot(SceneNode newNode, Scene scene);

    /// <summary>
    /// Creates a new scene node under an existing parent node.
    /// </summary>
    /// <param name="newNode">The node to add.</param>
    /// <param name="parentNode">The parent node.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A change record describing the mutation.</returns>
    public SceneNodeChangeRecord CreateNodeUnderParent(SceneNode newNode, SceneNode parentNode, Scene scene);

    /// <summary>
    /// Removes a scene node from the graph.
    /// </summary>
    /// <param name="nodeId">The identifier of the node to remove.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A change record describing the mutation.</returns>
    public SceneNodeChangeRecord RemoveNode(Guid nodeId, Scene scene);

    /// <summary>
    /// Removes a scene node and all its descendants from the graph.
    /// </summary>
    /// <param name="rootNodeId">The identifier of the hierarchy root to remove.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A change record describing the mutation.</returns>
    public SceneNodeChangeRecord RemoveHierarchy(Guid rootNodeId, Scene scene);

    /// <summary>
    /// Reparents a scene node to a new parent or root.
    /// </summary>
    /// <param name="nodeId">The identifier of the node to move.</param>
    /// <param name="oldParentId">The previous parent identifier, if known.</param>
    /// <param name="newParentId">The new parent identifier, or <see langword="null" /> for root.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>A change record describing the mutation.</returns>
    public SceneNodeChangeRecord ReparentNode(Guid nodeId, Guid? oldParentId, Guid? newParentId, Scene scene);

    /// <summary>
    /// Reparents multiple node hierarchies to a new parent or root.
    /// </summary>
    /// <param name="nodeIds">Hierarchy roots to move.</param>
    /// <param name="newParentId">The new parent identifier, or <see langword="null" /> for root.</param>
    /// <param name="scene">The owning scene.</param>
    /// <returns>Change records for each moved hierarchy.</returns>
    public IReadOnlyList<SceneNodeChangeRecord> ReparentHierarchies(IEnumerable<Guid> nodeIds, Guid? newParentId, Scene scene);
}

/// <summary>
/// Describes a scene-graph mutation for downstream engine sync, messaging, and undo.
/// </summary>
/// <param name="OperationName">The operation name for diagnostics.</param>
/// <param name="AffectedNode">The node that was mutated.</param>
/// <param name="OldParentId">The previous parent identifier.</param>
/// <param name="NewParentId">The new parent identifier.</param>
/// <param name="RequiresEngineSync">Indicates whether the engine must be synchronized.</param>
/// <param name="AddedToRootNodes">Indicates the node was added to <see cref="Scene.RootNodes" />.</param>
/// <param name="RemovedFromRootNodes">Indicates the node was removed from <see cref="Scene.RootNodes" />.</param>
public sealed record SceneNodeChangeRecord(
    string OperationName,
    SceneNode AffectedNode,
    Guid? OldParentId,
    Guid? NewParentId,
    bool RequiresEngineSync,
    bool AddedToRootNodes,
    bool RemovedFromRootNodes);
