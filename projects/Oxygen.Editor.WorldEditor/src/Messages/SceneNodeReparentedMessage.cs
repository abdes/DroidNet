// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.World.Messages;

/// <summary>
/// Message indicating that a scene node was reparented within the explorer layout.
/// </summary>
/// <param name="node">The reparented scene node.</param>
/// <param name="oldParentNodeId">Id of the old parent scene node, or <see langword="null"/> when parent is not a scene-node.</param>
/// <param name="newParentNodeId">Id of the new parent scene node, or <see langword="null"/> when parent is not a scene-node.</param>
internal sealed class SceneNodeReparentedMessage(SceneNode node, System.Guid? oldParentNodeId, System.Guid? newParentNodeId)
{
    /// <summary>
    /// Gets the reparented scene node.
    /// </summary>
    public SceneNode Node { get; } = node;

    /// <summary>
    /// Gets id of the previous parent scene node, or <see langword="null"/> if the previous parent wasn't a scene-node.
    /// </summary>
    public System.Guid? OldParentNodeId { get; } = oldParentNodeId;

    /// <summary>
    /// Gets id of the new parent scene node, or <see langword="null"/> if the new parent isn't a scene-node.
    /// </summary>
    public System.Guid? NewParentNodeId { get; } = newParentNodeId;
}
