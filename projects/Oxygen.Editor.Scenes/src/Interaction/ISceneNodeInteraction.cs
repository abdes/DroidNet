// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Interaction;

/// <summary>
/// The editing state of scene nodes that is not part of the scene: nodes hidden in the editor and nodes locked against
/// editing. Commands obey it; the workspace keeps and persists it.
/// </summary>
public interface ISceneNodeInteraction
{
    /// <summary>Gets whether a node is hidden in the editing viewports.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <returns><see langword="true"/> when the node is hidden.</returns>
    public bool IsHidden(NodeId nodeId);

    /// <summary>Gets whether a node is locked against editing.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <returns><see langword="true"/> when the node is locked.</returns>
    public bool IsLocked(NodeId nodeId);

    /// <summary>Finds the closest locked node that protects a node from editing.</summary>
    /// <param name="node">The scene node.</param>
    /// <returns>The node itself or an ancestor that is locked; otherwise, <see langword="null"/>.</returns>
    public SceneNode? GetLockOwner(SceneNode node);

    /// <summary>Hides or shows a node in the editing viewports.</summary>
    /// <param name="nodeId">The stable authored node id.</param>
    /// <param name="isHidden">The requested state.</param>
    public void SetHidden(NodeId nodeId, bool isHidden);
}
