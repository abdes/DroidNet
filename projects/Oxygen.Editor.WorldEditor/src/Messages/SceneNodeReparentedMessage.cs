// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;

namespace Oxygen.Editor.WorldEditor.Messages;

/// <summary>
/// Message indicating that a scene node was reparented within the explorer layout.
/// </summary>
/// <param name="node">The reparented scene node.</param>
/// <param name="oldParentNodeId">Id of the old parent scene node, or <c>null</c> when parent is not a scene-node.</param>
/// <param name="newParentNodeId">Id of the new parent scene node, or <c>null</c> when parent is not a scene-node.</param>
internal sealed class SceneNodeReparentedMessage(SceneNode node, System.Guid? oldParentNodeId, System.Guid? newParentNodeId)
{
    public SceneNode Node { get; } = node;

    /// <summary>
    /// Id of the previous parent scene node, or <c>null</c> if the previous parent wasn't a scene-node.
    /// </summary>
    public System.Guid? OldParentNodeId { get; } = oldParentNodeId;

    /// <summary>
    /// Id of the new parent scene node, or <c>null</c> if the new parent isn't a scene-node.
    /// </summary>
    public System.Guid? NewParentNodeId { get; } = newParentNodeId;
}
