// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Editor.World;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.Scenes.Authoring;

/// <summary>
/// The editing state of scene nodes that is not part of the scene: nodes hidden in the editor and nodes locked against
/// editing. Commands obey it; <see cref="Interaction.SceneNodeInteraction"/> keeps and persists it per project.
/// </summary>
public interface ISceneNodeInteraction
{
    /// <summary>Gets whether a node is hidden in the editing viewports.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <returns><see langword="true"/> when the node itself is hidden.</returns>
    public bool IsHidden(NodeReference node);

    /// <summary>Gets whether a node is locked against editing.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <returns><see langword="true"/> when the node itself is locked.</returns>
    public bool IsLocked(NodeReference node);

    /// <summary>Finds the closest locked node that protects a node from editing.</summary>
    /// <param name="node">The scene node.</param>
    /// <returns>The node itself or an ancestor that is locked; otherwise, <see langword="null"/>.</returns>
    public SceneNode? GetLockOwner(SceneNode node);

    /// <summary>Finds the closest locked node that protects a node, which an instance may have brought in, from editing.</summary>
    /// <param name="scene">The scene the node is in.</param>
    /// <param name="node">The node.</param>
    /// <returns>
    /// The node itself, an instanced ancestor on its path, or the instance root or one of its ancestors, that is locked;
    /// otherwise, <see langword="null"/>.
    /// </returns>
    public NodeReference? GetLockOwner(Scene scene, NodeReference node);

    /// <summary>Hides or shows a node in the editing viewports.</summary>
    /// <param name="node">The node: a node of the scene, or a node an instance brought in.</param>
    /// <param name="isHidden">The requested state.</param>
    public void SetHidden(NodeReference node, bool isHidden);
}
